//Código default dado por el profe Ochoa

//gcc x11_keys.c -o x11_keys -lX11

//--------------- código------------------

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <stdio.h>

int main(void) {
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Cannot open display\n");
        return 1;
    }

    int screen = DefaultScreen(display);

    Window window = XCreateSimpleWindow(
        display,
        RootWindow(display, screen),
        10, 10, 400, 200,
        1,
        BlackPixel(display, screen),
        WhitePixel(display, screen)
    );

    XSelectInput(display, window, ExposureMask | KeyPressMask);
    XMapWindow(display, window);

    XEvent event;

    while (1) {
        XNextEvent(display, &event);

        if (event.type == KeyPress) {
            KeySym keysym = XLookupKeysym(&event.xkey, 0);

            char *name = XKeysymToString(keysym);
            if (name) {
                printf("Key pressed: %s\n", name);
            } else {
                printf("Unknown key\n");
            }

            if (keysym == XK_Escape)
                break;
        }
    }

    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 0;
}