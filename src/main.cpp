/*
 * xlaserpointer
 * Linux utility to change the curser into a strobey laser pointer for
 * screencasts and presentations.
 *
 * Using code adatped from:
 *   - https://stackoverflow.com/questions/21780789/x11-draw-on-overlay-window
 *   - https://stackoverflow.com/questions/62448181/how-do-i-monitor-mouse-movement-events-in-all-windows-not-just-one-on-x11
 *   - https://stackoverflow.com/questions/8592292/how-to-quit-the-blocking-of-xlibs-xnextevent
 * 
 * Copyright (C) 2020, Naomi Alterman
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <assert.h>
#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/XInput2.h>

#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>

#include <string>
#include <iostream>
#include <deque>
#include <chrono>
#include <thread>
#include <cmath>
#include <csignal>

#include "argagg.hpp"
#include "csscolorparser.hpp"

#define _NET_WM_STATE_REMOVE        0    // remove/unset property
#define _NET_WM_STATE_ADD           1    // add/set property
#define _NET_WM_STATE_TOGGLE        2    // toggle property

#include <map>
static std::map<int, std::string> eventNames = {
   {2, "KeyPress"},
   {3, "KeyRelease"},
   {4, "ButtonPress"},
   {5, "ButtonRelease"},
   {6, "MotionNotify"},
   {7, "EnterNotify"},
   {8, "LeaveNotify"},
   {9, "FocusIn"},
   {10, "FocusOut"},
   {11, "KeymapNotify"},
   {12, "Expose"},
   {13, "GraphicsExpose"},
   {14, "NoExpose"},
   {15, "VisibilityNotify"},
   {16, "CreateNotify"},
   {17, "DestroyNotify"},
   {18, "UnmapNotify"},
   {19, "MapNotify"},
   {20, "MapRequest"},
   {21, "ReparentNotify"},
   {22, "ConfigureNotify"},
   {23, "ConfigureRequest"},
   {24, "GravityNotify"},
   {25, "ResizeRequest"},
   {26, "CirculateNotify"},
   {27, "CirculateRequest"},
   {28, "PropertyNotify"},
   {29, "SelectionClear"},
   {30, "SelectionRequest"},
   {31, "SelectionNotify"},
   {32, "ColormapNotify"},
   {33, "ClientMessage"},
   {34, "MappingNotify"},
   {35, "GenericEvent"},
   {36, "LASTEvent"}
};

struct WindowContext {
   Display *d;
   Window root;
   Window overlay;
   XVisualInfo vinfo;
   int screen_w, screen_h;
   int xi_opcode, xi_event;
   int x11_fd;
};

struct CairoContext {
   cairo_surface_t* surf;
   cairo_t* cr;
};

struct Coordinate {
   int x, y;
   bool operator==(const Coordinate& rhs) const {
       return (x == rhs.x) && (y == rhs.y);
   }
};

struct Color {
   double r, g, b, a;
};

bool shouldExit = false;
void signalHandler(int signum) {
   if (shouldExit) {
      exit(signum);  
   } else {
      shouldExit = true;
   }
}

void draw(cairo_t *cr, const std::deque<Coordinate> &coords, double size, const Color &color) {
   cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.0);
   cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
   cairo_paint (cr);

   cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
   cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

   double size_step = size/7.0;
   double cur_size = size - size_step*5;
   for (Coordinate c : coords) {
      cairo_move_to (cr, c.x, c.y);
      cairo_arc (cr, c.x, c.y, cur_size, 0., 2 * M_PI);
      if (cur_size < size) {
         cur_size += size_step;
      }
   }
   cairo_fill(cr);
}

WindowContext initialize_xlib(){
   WindowContext ctx;
   if ((ctx.d = XOpenDisplay(NULL)) == NULL) {
      exit(1);
   }

   int error;
   if (!XQueryExtension(ctx.d, "XInputExtension", &ctx.xi_opcode, &ctx.xi_event, &error)) {
     fprintf(stderr, "Error: XInput extension is not supported!\n");
     exit(1);
   }

   int major = 2;
   int minor = 0;
   int retval = XIQueryVersion(ctx.d, &major, &minor);
   if (retval != Success) {
     fprintf(stderr, "Error: XInput 2.0 is not supported (ancient X11?)\n");
     exit(1);
   }
   
   ctx.root = DefaultRootWindow(ctx.d);
   ctx.x11_fd = ConnectionNumber(ctx.d);

   return ctx;
}

void initialize_window(WindowContext &ctx){
   int default_screen = XDefaultScreen(ctx.d);

   ctx.screen_w = DisplayWidth(ctx.d, default_screen);
   ctx.screen_h = DisplayHeight(ctx.d, default_screen);

   XSetWindowAttributes attrs;
   attrs.override_redirect = true;

   if (!XMatchVisualInfo(ctx.d, DefaultScreen(ctx.d), 32, TrueColor, &ctx.vinfo)) {
      printf("No visual found supporting 32 bit color, terminating\n");
      exit(EXIT_FAILURE);
   }
   attrs.colormap = XCreateColormap(ctx.d, ctx.root, ctx.vinfo.visual, AllocNone);
   attrs.background_pixel = 0;
   attrs.border_pixel = 0;

   ctx.overlay = XCreateWindow(
      ctx.d, ctx.root,
      0, 0, ctx.screen_w, ctx.screen_h, 0,
      ctx.vinfo.depth, InputOutput, 
      ctx.vinfo.visual,
      CWOverrideRedirect | CWColormap | CWBackPixel | CWBorderPixel, &attrs
   );

   // TODO: play with this, try to get notified when new windows show up
   XSelectInput(ctx.d, ctx.root, SubstructureNotifyMask);

   XserverRegion region = XFixesCreateRegion(ctx.d, 0, 0);
   XFixesSetWindowShapeRegion(ctx.d, ctx.overlay, ShapeBounding, 0, 0, 0);
   XFixesSetWindowShapeRegion(ctx.d, ctx.overlay, ShapeInput, 0, 0, region);
   XFixesDestroyRegion(ctx.d, region);

   XMapWindow(ctx.d, ctx.overlay);
}

void initialize_xinput_capture(WindowContext &ctx) {
   /*
   * Set mask to receive XI_RawMotion events. Because it's raw,
   * XWarpPointer() events are not included, you can use XI_Motion
   * instead.
   */
   unsigned char mask_bytes[(XI_LASTEVENT + 7) / 8] = {0};  /* must be zeroed! */
   XISetMask(mask_bytes, XI_RawMotion);

   /* Set mask to receive events from all master devices */
   XIEventMask evmasks[1];
   /* You can use XIAllDevices for XWarpPointer() */
   evmasks[0].deviceid = XIAllMasterDevices;
   evmasks[0].mask_len = sizeof(mask_bytes);
   evmasks[0].mask = mask_bytes;
   XISelectEvents(ctx.d, ctx.root, evmasks, 1);
}

CairoContext initialize_cairo(WindowContext &ctx) {
   CairoContext cairoCtx;
   cairoCtx.surf = cairo_xlib_surface_create(ctx.d, ctx.overlay,
      ctx.vinfo.visual, ctx.screen_w, ctx.screen_h);
   cairoCtx.cr = cairo_create(cairoCtx.surf);
   return cairoCtx;
}

Coordinate getPointerCoords(WindowContext &ctx) {
   Window unused_w;
   int unused_i;
   Coordinate current;
   XQueryPointer(ctx.d, ctx.root,
      &unused_w, &unused_w,
      &current.x, &current.y,
      &unused_i, &unused_i,
      (unsigned int *) &unused_i);
   return current;
}

int main(int argc, const char **argv) {
   double ptr_size = 7.0;
   Color ptr_color = {1,0,0,1};
   int trail_length = 10;

   argagg::parser argparser {{
      { "help", {"-h", "--help"},
      "shows this help message", 0},
      { "color", {"-c", "--color"},
      "color of the laser pointer (default: red)", 1},
      { "size", {"-s", "--size"},
      "radius of the laser pointer in pixels (default: 7)", 1},
      { "trail", {"-t", "--trail"},
      "length of pointer trail (default: 10)", 1},
      { "showcursor", {"--cursor"},
      "don't hide the default X11 cursor", 0},
   }};
   argagg::parser_results args;
   try {
      args = argparser.parse(argc, argv);
   } catch (const std::exception& e) {
      std::cerr << e.what() << '\n';
      return EXIT_FAILURE;
   }

   if (args["help"]) {
      argagg::fmt_ostream fmt(std::cerr);
      fmt << "Usage: xlaserpointer [options]\n" <<
             "Change the curser into a strobey laser pointer for "
             "screencasts and presentations\n" << argparser;
      return EXIT_SUCCESS;
   }
   
   // TODO: validation
   if (args["size"]) {
      ptr_size = args["size"];
   }

   if (args["trail"]) {
      trail_length = args["trail"];
   }

   if (args["color"]) {
      std::string color_str = args["color"];
      if (auto color = CSSColorParser::parse(color_str)){      
         ptr_color.r = (*color).r/255.0;
         ptr_color.g = (*color).g/255.0;
         ptr_color.b = (*color).b/255.0;
         ptr_color.a = (*color).a;
      }
   }

   signal(SIGINT, signalHandler);
   WindowContext ctx = initialize_xlib();
   initialize_window(ctx);
   initialize_xinput_capture(ctx);
   CairoContext cairoCtx = initialize_cairo(ctx);

   if (!args["showcursor"]) {
      XFixesHideCursor(ctx.d, ctx.overlay);
   }

   std::deque<Coordinate> pointer_history(1);
   int cooldown = 0;
   fd_set in_fds;
   struct timeval tv;

   while (!shouldExit) {
      Coordinate current = getPointerCoords(ctx);
      bool changed = current == pointer_history.back();
      pointer_history.push_back(current);
      if (pointer_history.size() > trail_length) {
         pointer_history.pop_front();
      }

      draw(cairoCtx.cr, pointer_history, ptr_size, ptr_color);
      XFlush(ctx.d);

      if (cooldown == 0){


         // TODO TODO TODO:
         // ugh mouse events don't show up if we drag or popup menu,
         // so we have to set a timer for periodic wakeups
         // find some way to monitor for mouse movements so we don't
         // have to do this
         bool potential_overlap = false;

         // FD_ZERO(&in_fds);
         // FD_SET(ctx.x11_fd, &in_fds);
         // tv.tv_usec = 100000;
         // tv.tv_sec = 0;
         std::cout << "+" << std::endl;
         // int num_ready_fds = select(ctx.x11_fd + 1, &in_fds, NULL, NULL, &tv);
         // if (num_ready_fds > 0) {
            XEvent event;
            while(XEventsQueued(ctx.d, QueuedAlready) > 0) {
               XNextEvent(ctx.d, &event);
               if (event.type != 35) {
                  std::cout << eventNames[event.type] << std::endl;
               }
               if (event.type == CreateNotify) {
                  potential_overlap = true;
               }
            }

         // } else if (num_ready_fds == 0) {
         //    // timeout
         //    std::cout << "timeout" << std::endl;   
         // } else {
         //    // select() error
         //    std::cout << "error " << errno << std::endl;   
         // }
         std::cout << "-" << std::endl;

         if (potential_overlap) {
            // TODO:
            // This is a sketchy hack to make sure our overlay appears
            // on top of menu/popup windows. Find some way to monitor
            // events and only do so when necessary:
            XUnmapWindow(ctx.d, ctx.overlay);
            XMapWindow(ctx.d, ctx.overlay);
         }

         cooldown = trail_length;

      } else {
         cooldown--;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
   }

   cairo_destroy(cairoCtx.cr);
   cairo_surface_destroy(cairoCtx.surf);

   XUnmapWindow(ctx.d, ctx.overlay);
   XCloseDisplay(ctx.d);
   return 0;
}