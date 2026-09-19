
#ifndef _CC_LIB_BIGNUM_BIG_VEC_H
#define _CC_LIB_BIGNUM_BIG_VEC_H

#include <string_view>
#include <utility>

#include "bignum/big.h"

// Exact 2D rational vector.
struct BigVecQ2 {
  BigRat x, y;

  BigVecQ2() : x(0), y(0) {}
  BigVecQ2(BigRat x, BigRat y) :
    x(std::move(x)), y(std::move(y)) {}
  BigVecQ2(std::string_view sx, std::string_view sy) :
    x(sx), y(sy) {}

  BigVecQ2 operator+(const BigVecQ2 &o) const {
    return BigVecQ2{
      BigRat::Plus(x, o.x),
      BigRat::Plus(y, o.y),
    };
  }
  BigVecQ2 operator-(const BigVecQ2 &o) const {
    return BigVecQ2{
      BigRat::Minus(x, o.x),
      BigRat::Minus(y, o.y),
    };
  }
  BigVecQ2 operator*(const BigRat &s) const {
    return {BigRat::Times(x, s), BigRat::Times(y, s)};
  }
  BigVecQ2 operator/(const BigRat &s) const {
    return {BigRat::Div(x, s), BigRat::Div(y, s)};
  }

  static BigRat Dot(const BigVecQ2 &a, const BigVecQ2 &b) {
    return BigRat::Plus(BigRat::Times(a.x, b.x), BigRat::Times(a.y, b.y));
  }
};


// Exact 3D rational vector.
struct BigVecQ3 {
  BigRat x, y, z;

  BigVecQ3() : x(0), y(0), z(0) {}
  BigVecQ3(BigRat x, BigRat y, BigRat z) :
    x(std::move(x)), y(std::move(y)), z(std::move(z)) {}
  BigVecQ3(std::string_view sx, std::string_view sy, std::string_view sz) :
    x(sx), y(sy), z(sz) {}

  BigVecQ3 operator+(const BigVecQ3 &o) const {
    return {
      BigRat::Plus(x, o.x),
      BigRat::Plus(y, o.y),
      BigRat::Plus(z, o.z),
    };
  }
  BigVecQ3 operator-(const BigVecQ3 &o) const {
    return {
      BigRat::Minus(x, o.x),
      BigRat::Minus(y, o.y),
      BigRat::Minus(z, o.z)
    };
  }
  BigVecQ3 operator*(const BigRat &s) const {
    return {BigRat::Times(x, s), BigRat::Times(y, s), BigRat::Times(z, s)};
  }
  BigVecQ3 operator/(const BigRat &s) const {
    return {BigRat::Div(x, s), BigRat::Div(y, s), BigRat::Div(z, s)};
  }

  static BigRat Dot(const BigVecQ3 &a, const BigVecQ3 &b) {
    return BigRat::Plus(
        BigRat::Plus(BigRat::Times(a.x, b.x), BigRat::Times(a.y, b.y)),
        BigRat::Times(a.z, b.z));
  }
  static BigVecQ3 Cross(const BigVecQ3 &a, const BigVecQ3 &b) {
    return BigVecQ3{
      BigRat::Minus(BigRat::Times(a.y, b.z), BigRat::Times(a.z, b.y)),
      BigRat::Minus(BigRat::Times(a.z, b.x), BigRat::Times(a.x, b.z)),
      BigRat::Minus(BigRat::Times(a.x, b.y), BigRat::Times(a.y, b.x)),
    };
  }
  static BigRat Det(const BigVecQ3 &a, const BigVecQ3 &b, const BigVecQ3 &c) {
    return Dot(a, Cross(b, c));
  }
};

#endif
