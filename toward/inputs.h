
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
  struct KeyDown { uint32_t codepoint; uint8_t modifiers; };
  struct KeyUp { uint32_t codepoint; uint8_t modifiers; };

  static constexpr uint8_t MOUSE_LEFT = 0x00;
  static constexpr uint8_t MOUSE_RIGHT = 0x01;
  static constexpr uint8_t MOUSE_MIDDLE = 0x02;
  // Simple click detection.
  struct MouseClick { int x; int y; uint8_t button; };

  // TODO: For dragging, etc.
  // struct MouseChange { int x; int y; uint8_t button; };

  struct MouseWheel { bool up; };

  using Input = std::variant<None, Exit, KeyDown, KeyUp,
                             MouseClick, MouseWheel>;

  virtual Input GetInput() = 0;
  virtual ~Inputs();
};


#endif
