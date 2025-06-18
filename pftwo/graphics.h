#ifndef _PFTWO_GRAPHICS_H
#define _PFTWO_GRAPHICS_H

#include <cstdint>
#include <vector>

#include "SDL.h"
#include "SDL_video.h"

// XXX This code is confused because for a long time I didn't understand
// that SDL surfaces store their pixels in a platform-specific order. It's
// not an endianness thing. Fix this so that things called ARGB or RGBA
// are really in that byte order, and so that the blit functions that
// use SDL surfaces respect this.

// TODO: Use ImageRGBA instead.

// assumes ARGB, surfaces exactly the same size, etc.
void CopyARGB(const std::vector<uint8_t> &argb, SDL_Surface *surface);

void BlitARGB(const std::vector<uint8_t> &argb, int w, int h,
              int x, int y, SDL_Surface *surface);

void HalveARGB(const std::vector<uint8_t> &argb, int width, int height,
               SDL_Surface *surface);

void BlitARGBHalf(const std::vector<uint8_t> &argb, int width, int height,
                  int xpos, int ypos,
                  SDL_Surface *surface);

// The ones below this line treat RGBA correctly.

// Blit at 2X size. Treats alpha channel as being always 0xFF.
void BlitRGBA2x(const std::vector<uint8_t> &rgba, int w, int h,
                int x, int y, SDL_Surface *surface);

// Same as above, 1:1.
void BlitRGBA(const std::vector<uint8_t> &rgba, int w, int h,
              int x, int y, SDL_Surface *surface);

// Implied alpha=0xFF.
void SetPixelRGB(int x, int y, uint8_t r, uint8_t g, uint8_t b,
                 SDL_Surface *surface);

#endif
