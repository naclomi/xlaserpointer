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

struct Coordinate {
   int x, y;
};

int check_event(cairo_surface_t *sfc, int block)
{
   char keybuf[8];
   KeySym key;
   XEvent e;

   for (;;)
   {
      if (block || XPending(cairo_xlib_surface_get_display(sfc))) {
         XNextEvent(cairo_xlib_surface_get_display(sfc), &e);
      } else {
         return 0;
      }

      // switch (e.type)
      // {
      //    case ButtonPress:
      //       return -e.xbutton.button;
      //    case KeyPress:
      //       XLookupString(&e.xkey, keybuf, sizeof(keybuf), &key, NULL);
      //       return key;
      //    default:
      //       fprintf(stderr, "Dropping unhandled XEevent.type = %d.\n", e.type);
      //       return 0;
      // }
   }
}


void draw(cairo_t *cr, const std::vector<Coordinate> &coords) {
   int d = 10;

   cairo_save (cr);
   cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.0);
   cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
   cairo_paint (cr);
   cairo_restore (cr);

   cairo_set_source_rgba(cr, 1.0, 0.0, 0.0, 0.75);

   float size = 2;
   float max_size = 7;
   for (Coordinate c : coords) {
      cairo_arc (cr, c.x, c.y, size, 0., 2 * M_PI);
      cairo_fill(cr);
      if (size < max_size) {
         size += 1;
      }
   }
}

int main() {
   Display *d;
   if ((d = XOpenDisplay(NULL)) == NULL) {
      exit(1);
   }

   int xi_opcode, xi_event, error;
   if (!XQueryExtension(d, "XInputExtension", &xi_opcode, &xi_event, &error)) {
     fprintf(stderr, "Error: XInput extension is not supported!\n");
     return 1;
   }

   int major = 2;
   int minor = 0;
   int retval = XIQueryVersion(d, &major, &minor);
   if (retval != Success) {
     fprintf(stderr, "Error: XInput 2.0 is not supported (ancient X11?)\n");
     return 1;
   }


   Window root = DefaultRootWindow(d);
   int default_screen = XDefaultScreen(d);

   int screen_w = DisplayWidth(d, default_screen);
   int screen_h = DisplayHeight(d, default_screen);

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
   XISelectEvents(d, root, evmasks, 1);



   XSetWindowAttributes attrs;
   attrs.override_redirect = true;

   XVisualInfo vinfo;
   if (!XMatchVisualInfo(d, DefaultScreen(d), 32, TrueColor, &vinfo)) {
      printf("No visual found supporting 32 bit color, terminating\n");
      exit(EXIT_FAILURE);
   }
   // these next three lines add 32 bit depth, remove if you dont need and change the flags below
   attrs.colormap = XCreateColormap(d, root, vinfo.visual, AllocNone);
   attrs.background_pixel = 0;
   attrs.border_pixel = 0;

   // Window XCreateWindow(
   //     Display *display, Window parent,
   //     int x, int y, unsigned int width, unsigned int height, unsigned int border_width,
   //     int depth, unsigned int class, 
   //     Visual *visual,
   //     unsigned long valuemask, XSetWindowAttributes *attributes
   // );
   Window overlay = XCreateWindow(
      d, root,
      0, 0, screen_w, screen_h, 0,
      vinfo.depth, InputOutput, 
      vinfo.visual,
      CWOverrideRedirect | CWColormap | CWBackPixel | CWBorderPixel, &attrs
   );

   // XSelectInput(d, overlay, NoEventMask);

   XserverRegion region = XFixesCreateRegion(d, 0, 0);
   XFixesSetWindowShapeRegion(d, overlay, ShapeBounding, 0, 0, 0);
   XFixesSetWindowShapeRegion(d, overlay, ShapeInput, 0, 0, region);
   XFixesDestroyRegion(d, region);

   XFixesHideCursor(d, overlay);

   XMapWindow(d, overlay);

   cairo_surface_t* surf = cairo_xlib_surface_create(d, overlay,
                               vinfo.visual,
                               screen_w, screen_h);
   cairo_t* cr = cairo_create(surf);

   unsigned int mask = 0;

   Window unused_w;
   int unused_i;
   XEvent event;

   std::vector<Coordinate> pointer_history(10);
   int cooldown = 0;
   for (;;) {
      for (int i=0; i<pointer_history.size()-1; i++) {
         pointer_history[i] = pointer_history[i+1];
      }
      Coordinate current;
      XQueryPointer(d, root,
         &unused_w, &unused_w,
         &current.x, &current.y,
         &unused_i, &unused_i,
         &mask);
      pointer_history[pointer_history.size()-1] = current;
      draw(cr, pointer_history);
      XFlush(d);

      if (cooldown == 0){
         XNextEvent(d, &event);
         cooldown = 5;
      } else {
         std::this_thread::sleep_for(std::chrono::milliseconds(10));
         cooldown--;
      }
      // TODO: exit event
   }

   cairo_destroy(cr);
   cairo_surface_destroy(surf);

   XUnmapWindow(d, overlay);
   XCloseDisplay(d);
   return 0;
}