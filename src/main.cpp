/*
 * Help from:
 *   - https://stackoverflow.com/questions/21780789/x11-draw-on-overlay-window
 *   - https://stackoverflow.com/questions/62448181/how-do-i-monitor-mouse-movement-events-in-all-windows-not-just-one-on-x11
 */
#include <assert.h>
#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/X.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/XInput2.h>

#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>

#include <vector>
#include <chrono>
#include <thread>
#include <math.h>

struct WindowContext {
   Display *d;
   Window root;
   Window overlay;
   XVisualInfo vinfo;
   int screen_w, screen_h;
};

struct CairoContext {
   cairo_surface_t* surf;
   cairo_t* cr;
};

struct Coordinate {
   int x, y;
};

struct Color {
   double r, g, b, a;
};

void draw(cairo_t *cr, const std::vector<Coordinate> &coords, double size, const Color &color) {
   cairo_save (cr);
   cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.0);
   cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
   cairo_paint (cr);
   cairo_restore (cr);

   cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);

   double size_step = size/7.0;
   double cur_size = size - size_step*5;
   for (Coordinate c : coords) {
      cairo_arc (cr, c.x, c.y, cur_size, 0., 2 * M_PI);
      cairo_fill(cr);
      if (cur_size < size) {
         cur_size += size_step;
      }
   }
}

WindowContext initialize_xlib(){
   WindowContext ctx;
   if ((ctx.d = XOpenDisplay(NULL)) == NULL) {
      exit(1);
   }

   int xi_opcode, xi_event, error;
   if (!XQueryExtension(ctx.d, "XInputExtension", &xi_opcode, &xi_event, &error)) {
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
   // these next three lines add 32 bit depth, remove if you dont need and change the flags below
   attrs.colormap = XCreateColormap(ctx.d, ctx.root, ctx.vinfo.visual, AllocNone);
   attrs.background_pixel = 0;
   attrs.border_pixel = 0;

   // Window XCreateWindow(
   //     Display *display, Window parent,
   //     int x, int y, unsigned int width, unsigned int height, unsigned int border_width,
   //     int depth, unsigned int class, 
   //     Visual *visual,
   //     unsigned long valuemask, XSetWindowAttributes *attributes
   // );
   ctx.overlay = XCreateWindow(
      ctx.d, ctx.root,
      0, 0, ctx.screen_w, ctx.screen_h, 0,
      ctx.vinfo.depth, InputOutput, 
      ctx.vinfo.visual,
      CWOverrideRedirect | CWColormap | CWBackPixel | CWBorderPixel, &attrs
   );

   XserverRegion region = XFixesCreateRegion(ctx.d, 0, 0);
   XFixesSetWindowShapeRegion(ctx.d, ctx.overlay, ShapeBounding, 0, 0, 0);
   XFixesSetWindowShapeRegion(ctx.d, ctx.overlay, ShapeInput, 0, 0, region);
   XFixesDestroyRegion(ctx.d, region);

   XFixesHideCursor(ctx.d, ctx.overlay);

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
   XISetMask(mask_bytes, XI_RawButtonPress);
   XISetMask(mask_bytes, XI_RawKeyPress);

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

int main() {
   WindowContext ctx = initialize_xlib();
   initialize_window(ctx);
   initialize_xinput_capture(ctx);
   CairoContext cairoCtx = initialize_cairo(ctx);


   double ptr_size = 7.0;
   Color ptr_color = {1,0,0,.75};
   int trail_length = 10;

   std::vector<Coordinate> pointer_history(trail_length);
   int cooldown = 0;
   for (;;) {
      for (int i=0; i<pointer_history.size()-1; i++) {
         pointer_history[i] = pointer_history[i+1];
      }
      Coordinate current = getPointerCoords(ctx);
      pointer_history[pointer_history.size()-1] = current;
      draw(cairoCtx.cr, pointer_history, ptr_size, ptr_color);
      XFlush(ctx.d);

      if (cooldown == 0){
         XEvent event;
         XNextEvent(ctx.d, &event);
         cooldown = trail_length;
      } else {
         std::this_thread::sleep_for(std::chrono::milliseconds(10));
         cooldown--;
      }
      // TODO: exit event
   }

   cairo_destroy(cairoCtx.cr);
   cairo_surface_destroy(cairoCtx.surf);

   XUnmapWindow(ctx.d, ctx.overlay);
   XCloseDisplay(ctx.d);
   return 0;
}