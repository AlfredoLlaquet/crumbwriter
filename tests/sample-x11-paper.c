/* Read the most common color in a small patch of the actual X11 desktop. */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    enum { side = 5 };
    if (argc != 3)
        return 2;
    Display *display = XOpenDisplay(NULL);
    if (!display)
        return 1;

    int screen = DefaultScreen(display);
    XImage *image = XGetImage(display, RootWindow(display, screen),
                              atoi(argv[1]), atoi(argv[2]), side, side, AllPlanes, ZPixmap);
    if (!image) {
        XCloseDisplay(display);
        return 1;
    }

    unsigned long pixels[side * side];
    for (int i = 0; i < side * side; ++i)
        pixels[i] = XGetPixel(image, i % side, i / side);
    XDestroyImage(image);

    int best = 0;
    XColor color = {0};
    for (int i = 0; i < side * side; ++i) {
        int count = 0;
        for (int j = i; j < side * side; ++j)
            count += pixels[i] == pixels[j];
        if (count > best) {
            best = count;
            color.pixel = pixels[i];
        }
    }
    int ok = XQueryColor(display, DefaultColormap(display, screen), &color);
    XCloseDisplay(display);
    if (!ok)
        return 1;
    printf("%u %u %u\n", color.red / 257u, color.green / 257u, color.blue / 257u);
    return 0;
}
