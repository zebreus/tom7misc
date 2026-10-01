
#include "escapex.h"

#include "SDL_events.h"
#include "SDL_video.h"

#include "sdl/sdlutil.h"
#include "drawable.h"

SDL_Surface *screen = nullptr;

/* XXX should be bools */
bool have_network = false;
bool have_audio = false;

bool HandleVideoEvent(Drawable *parent, const SDL_Event &event) {
  switch (event.type) {
  case SDL_VIDEORESIZE: {
    SDL_ResizeEvent *re = (SDL_ResizeEvent*)&event;
    screen = sdlutil::makescreen(re->w, re->h);
    if (parent) {
      parent->ScreenResize();
      parent->Draw();
      SDL_Flip(screen);
    }
    return true;
  }

  case SDL_VIDEOEXPOSE:
    if (parent) {
      parent->Draw();
      SDL_Flip(screen);
    }
    return true;

  default:
    return false;
  }
}
