
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

  using Input = std::variant<None, Exit, KeyDown, KeyUp>;

  virtual Input GetInput() = 0;
  virtual ~Inputs();
};


#endif
