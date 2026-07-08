
#ifndef _TOWARD_INPUTS_H
#define _TOWARD_INPUTS_H

#include <memory>
#include <variant>
#include <cstdint>

// Abstract polling-based input.

struct Inputs {
  // Create the SDL-based input type. SDL must
  // have been initialized.
  static std::unique_ptr<Inputs> CreateSDL();

  // Signal to exit the application.
  struct Exit { };
  // No input event.
  struct None { };

  // With Unicode codepoints.
  static constexpr uint8_t MOD_CTRL = 0x01;
  static constexpr uint8_t MOD_ALT = 0x02;
  // Shift also affects the codepoint reported, e.g. you
  // get 'G' instead of 'g'.
  static constexpr uint8_t MOD_SHIFT = 0x04;

  // Not real codepoints, but this is what SDL returns.
  static constexpr uint32_t CP_LEFT = 0x40000050;
  static constexpr uint32_t CP_RIGHT = 0x4000004f;
  static constexpr uint32_t CP_HOME = 0x4000004a;
  static constexpr uint32_t CP_END = 0x4000004d;

  struct KeyDown { uint32_t codepoint; uint8_t modifiers; };
  struct KeyUp { uint32_t codepoint; uint8_t modifiers; };

  static constexpr uint8_t MOUSE_LEFT = 0x00;
  static constexpr uint8_t MOUSE_RIGHT = 0x01;
  static constexpr uint8_t MOUSE_MIDDLE = 0x02;
  // Simple click detection (mouse down).
  // A click also generates a change event.
  struct MouseClick { int x; int y; uint8_t button; };

  // Any time the mouse changes state, the new state.
  struct MouseChange { int x; int y; int dx; int dy; uint8_t button; };

  // Mousewheel is an impulse event.
  struct MouseWheel { int x; int y; bool up; };

  using Input = std::variant<None, Exit, KeyDown, KeyUp,
                             MouseChange,
                             MouseClick, MouseWheel>;

  virtual Input GetInput() = 0;
  virtual ~Inputs();
};


#endif
