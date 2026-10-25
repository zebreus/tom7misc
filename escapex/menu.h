
#ifndef _ESCAPE_MENU_H
#define _ESCAPE_MENU_H

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "SDL_events.h"
#include "SDL_video.h"
#include "chars.h"
#include "drawable.h"

/* menus are essentially what are often called
   "forms" in GUI lingo */

enum class InputResultKind {
  NOTHING,
  /* go to the next or previous item */
  NEXT, PREV,
  /* the key is rejected as illegal */
  REJECT,
  /* some action has changed the display
     (implied by next,prev) */
  UPDATED,
  /* confirm or cancel the entire menu */
  OK, CANCEL,
  /* quit the program */
  QUIT,
};

struct InputResult {
  InputResultKind k;
  virtual InputResultKind kind() { return k; }
  explicit InputResult(InputResultKind i) : k(i) {}
};

/* XXX add a way of getting defaults */
/* (mostly) abstract base class of menuitems,
   which are "controls" in GUI lingo */
struct MenuItem {
  /* perhaps multi-line explanation of what this
     control sets. */
  std::string explanation;

  /* how many pixels to indent by.
     this amount is interpreted as being automatically
     added to the width in size and the x value for
     draw. */
  int indent = 0;

  /* if the control is disabled, it should not
     be focusable and should draw differently */
  bool disabled = false;

  /* parent menu */
  struct Menu *container = nullptr;

  /* stuff that all menuitems have */

  /* some controls can be focused, some not */
  virtual bool Focusable() { return !disabled; }

  virtual void Size(int &width, int &height) = 0;
  virtual void Draw(int x, int y, int focused) = 0;

  /* mouse click relative to menuitem */
  virtual InputResult Click(int x, int y) {
    /* XXX send key 'enter' */
    return InputResult(InputResultKind::REJECT);
  }

  /* displayed at the bottom of the screen to
     explain how to use the control */
  virtual std::string HelpText() = 0;

  /* some default behavior here */
  /* process a keypress.
     assume e.type = SDL_KEYDOWN */
  virtual InputResult Key(SDL_Event e);

  MenuItem() {}
  virtual ~MenuItem() {}
};

/* unselectable labels */
struct Label : public MenuItem {
  std::string text;
  virtual bool Focusable() { return false; }
  virtual std::string HelpText() { return ""; }
  virtual void Draw(int x, int y, int f);
  virtual void Size(int &w, int &h);
  virtual ~Label() {}
};

/* empty space */
struct VSpace : public MenuItem {
  int height = 0;
  explicit VSpace(int n) : height(n) {}
  virtual bool Focusable() { return false; }
  virtual std::string HelpText() { return ""; }
  virtual void Draw(int x, int y, int f) { }
  virtual void Size(int &w, int &h) {
    w = 1;
    h = height;
  }
  virtual ~VSpace() {}
};

struct TextInput : public MenuItem {
  std::string question;
  std::string input;
  /* immediately accept when pressing 'enter'? */
  bool accept_on_enter = false;

  std::string HelpText() override {
    return "Enter a single line of text.";
  }
  void Draw(int x, int y, int) override;
  void Size(int &w, int &h) override;
  InputResult Key(SDL_Event e) override;

  TextInput() {}

 protected:
  virtual void DrawCh(int x, int y, int, char passwordchar = 0);

 private:
  /* cursor before nth character in input */
  /* XXX probably want to check that the cursor is
     still inside 'input', because input can be
     changed externally */
  [[maybe_unused]] int cursor = 0;
};

struct TextPassword : public TextInput {
  std::string HelpText() override {
    return "Enter a password.";
  }
  void Draw(int x, int y, int i) override {
    DrawCh(x, y, i, '*');
  }
};

struct Toggle : public MenuItem {
  std::string question;
  bool checked = false;

  std::string HelpText() override {
    return "Press " BLUE "enter" POP " or "
      BLUE "space" POP " to toggle.";
  }
  void Draw(int x, int y, int) override;
  void Size(int &w, int &h) override;
  InputResult Key(SDL_Event e) override;
  InputResult Click(int, int) override;
};

struct Slider : public MenuItem {
  std::string question;
  /* labels over lowest and highest points in slider */
  std::string low;
  std::string high;

  /* inclusive */
  int lowest = 0;
  int highest = 0;

  /* for best accuracy, nsegs should be
     (highest - lowest + 1). This directly affects
     the width. Therefore nsegs should be at
     least length(low) + length(hi) + 1 */
  int nsegs = 0;

  std::string HelpText() override {
    return "Press " BLUE "left" POP " or " BLUE "right" POP
           " to change the setting.";
  }

  Slider(int lows, int highs, int segs);
  void Draw(int x, int y, int f) override;
  void Size(int &w, int &h) override;
  InputResult Key(SDL_Event e) override;
  InputResult Click(int, int) override;

  /* selected position along line (value in the inclusive
     interval (lowest, highest)) */
  int pos = 0;

 private:
  /* scrollbar graphic as string. has nseg segments */
  std::string scrollbar;
};

struct Okay : public MenuItem {
  std::string text;
  int *ptr = nullptr;
  int myval = 0;

  std::string HelpText() override {
    return "Press " BLUE "enter" POP " to confirm.";
  }

  Okay() {}

  Okay(std::string text, int *ptr = nullptr, int myval = 0)
    : text(std::move(text)), ptr(ptr), myval(myval) {}

  void Draw(int x, int y, int f) override;
  void Size(int &w, int &h) override;

  InputResult Key(SDL_Event e) override;

  InputResult Click(int, int) override {
    Activate();
    return InputResult(InputResultKind::OK);
  }

 protected:
  virtual void Activate() {
    /* set pointer if applicable */
    if (ptr) *ptr = myval;
  }
};

struct Cancel : public MenuItem {
  std::string text = "Cancel";

  std::string HelpText() override {
    return "Press " BLUE "enter" POP " to cancel. "
           GREY "(" BLUE "esc" POP " also cancels at any time.)";
  }

  void Draw(int x, int y, int f) override;
  void Size(int &w, int &h) override;
  InputResult Key(SDL_Event e) override;

  InputResult Click(int, int) override {
    return InputResult(InputResultKind::CANCEL);
  }
};


/* menus */

struct Menu : public Drawable {
  /* create a menu from some items. After this point,
     it's not possible to add or remove items. If
     'fullscreen' is true, the menu uses the whole screen.
     Otherwise, it stretches to accomodate its contents
     and is centered on the screen. */
  /* does not take ownership of the item pointers
     or the list cells */
  static std::unique_ptr<Menu> Create(Drawable *below,
                                      std::string title,
                                      std::vector<MenuItem *> items,
                                      bool fullscreen);

  void Draw() override;
  void ScreenResize() override;

  /* enter modal mode and return the final result of
     the menu. expect OK, CANCEL, or QUIT */
  InputResultKind Menuize();

  /* all menuitems will still be available */
  virtual ~Menu();

  int alpha = 200;
  /* set negative to get centered behavior */
  int yoffset = -1;

 private:
  Menu() {}

  void Redraw();
  bool SkipOK(int);
  void FixUp(int);
  void NextFocus(int dir);
  InputResult ClickSelect(int x, int y);

  Drawable *below = nullptr;
  std::string title;
  std::vector<MenuItem *> items;
  bool fullscreen = false;
  int selected = 0;
  int skip = 0;

  int posx = 0, posy = 0;
  int w = 0, h = 0, stath = 0;
  SDL_Surface *alpharect = nullptr;
};

#endif
