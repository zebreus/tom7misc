
#include "prompt.h"

#include <memory>
#include <string>
#include <string_view>

#include "chars.h"
#include "draw.h"
#include "drawable.h"
#include "menu.h"
#include "sdl/font.h"

/* XXX implement in terms of menu class?
   textinput is meant to be the same */

Prompt *Prompt::Create() {
  Prompt *pp = new Prompt();
  pp->below = 0;
  return pp;
}

Prompt::~Prompt() {}

string Prompt::Ask(Drawable *b, string_view t, string_view d) {
  std::unique_ptr<Prompt> pp{Prompt::Create()};
  pp->title = t;
  pp->below = b;
  pp->input = d;

  return pp->select();
}

string Prompt::select() {
  TextInput inp;
  inp.question = title;
  inp.input = input;
  inp.explanation = explanation;

  VSpace spacer((int)(fon->height * 1.5f));

  Okay ok;
  ok.text = "Accept";

  Cancel can;

  std::unique_ptr<Menu> mm =
    Menu::Create(below, GREY "Input Required",
                 {&inp, &spacer, &ok, &can},
                 false);
  InputResultKind res = mm->menuize();

  if (res == InputResultKind::OK) {
    return inp.input;
  } else return ""; /* ?? */
}
