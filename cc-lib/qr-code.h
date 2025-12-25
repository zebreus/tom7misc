
#ifndef _CC_LIB_QR_CODE_H
#define _CC_LIB_QR_CODE_H

#include <string_view>

#include "image.h"

struct QRCode {
  // TODO: ECC settings, etc.
  // Using 'true' for white and 'false' for black.
  static Image1 Text(std::string_view str);

  // Adds a white border. This can be useful if displaying on a dark
  // background.
  static Image1 AddBorder(const Image1 &qr, int pixels);

  // Since you almost always want to do this.
  static ImageRGBA ToRGBA(const Image1 &qr, int scale = 1);

 private:
  // All static.
  QRCode() = delete;
};

#endif
