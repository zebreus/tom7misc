
#include <tuple>
#include <cmath>
#include <string>
#include <cstdint>

#include "threadutil.h"
#include "image.h"
#include "color-util.h"
#include "base/stringprintf.h"

using namespace std;
using uint32 = uint32_t;

// Baffling numbers are like complex numbers, but with three terms: a
// + bi + cj. Yes, this is known to "not work" (in the sense that
// there exists no such algebra that is associative), but we don't
// need associativity. Plus, who's gonna stop me, FROBENIUS??

//   *  1  i  j
//   1  1  i  j
//   i  i -1  A
//   j  j  B  C


template<int AR, int AI, int AJ,
         int BR, int BI, int BJ,
         int CR, int CI, int CJ>
struct baffling_tmpl {
  using baffling = baffling_tmpl<AR, AI, AJ,
                                 BR, BI, BJ,
                                 CR, CI, CJ>;

  float rpart = 0.0f, ipart = 0.0f, jpart = 0.0f;

  baffling_tmpl() {}
  baffling_tmpl(float r, float i, float j) : rpart(r), ipart(i), jpart(j) {}

  float Abs() const { return sqrtf(rpart * rpart +
                                   ipart * ipart +
                                   jpart * jpart); }

  float Re() const { return rpart; }
  float Im() const { return ipart; }
  float Jm() const { return jpart; }
  std::tuple<float, float, float> Parts() const {
    return std::make_tuple(rpart, ipart, jpart);
  }

  inline baffling &operator +=(const baffling &rhs) {
    rpart += rhs.rpart;
    ipart += rhs.ipart;
    jpart += rhs.jpart;
    return *this;
  }

  inline baffling &operator -=(const baffling &rhs) {
    rpart -= rhs.rpart;
    ipart -= rhs.ipart;
    jpart -= rhs.jpart;
    return *this;
  }

  inline baffling &operator +=(const float &rhs) {
    rpart += rhs;
    return *this;
  }

  inline baffling &operator -=(const float &rhs) {
    rpart -= rhs;
    return *this;
  }

  inline baffling &operator *=(const float &rhs) {
    rpart *= rhs;
    ipart *= rhs;
    jpart *= rhs;
    return *this;
  }

  inline baffling operator +(const baffling &b) {
    return baffling(this->rpart + b.rpart, this->ipart + b.ipart, this->jpart + b.jpart);
  }

  inline baffling operator -(const baffling &b) {
    return baffling(this->rpart - b.rpart, this->ipart - b.ipart, this->jpart - b.jpart);
  }

  inline baffling operator *(const baffling &b) {
    const baffling &a = *this;
    // (ar + ai * i + aj * j)(br + bi * i + bj * j) =
    // 9 terms:
    // (1)  ar * br     + ar * bi * i   + ar * bj * j   +
    // (2)  ai * br * i + ai * bi * i^2 + ai * bj * ij  +
    // (3)  aj * br * j + aj * bi * ji  + aj * bj * j^2
    //
    // Which collects to (and this is of course fishy, because
    // I'm assuming associativity):

    // (1)  ar * br  +
    // (2)  (ar * bi + ai * br) * i  +
    // (3)  (ar * bj + aj * br) * j  +
    // (4)  (ai * bj) * ij  +
    // (5)  (aj * bi) * ji  +
    // (6)  (ai * bi) * i^2  +
    // (7)  (aj * bj) * j^2

    float term1 = a.rpart * b.rpart;
    float term2 = a.rpart * b.ipart + a.ipart * b.rpart;
    float term3 = a.rpart * b.jpart + a.jpart * b.rpart;
    float term4 = a.ipart * b.jpart;
    float term5 = a.jpart * b.ipart;
    float term6 = a.ipart * b.ipart;
    float term7 = a.jpart * b.jpart;

    // (1) and (6)
    float r = term1 - term6;

    //   *  1  i  j
    //   1  1  i  j
    //   i  i -1  A
    //   j  j  B  C
    if constexpr (AR != 0) r += AR * term4;
    if constexpr (BR != 0) r += BR * term5;
    if constexpr (CR != 0) r += CR * term7;

    float i = term2;
    if constexpr (AI != 0) i += AI * term4;
    if constexpr (BI != 0) i += BI * term5;
    if constexpr (CI != 0) i += CI * term7;

    float j = term3;
    if constexpr (AJ != 0) j += AJ * term4;
    if constexpr (BJ != 0) j += BJ * term5;
    if constexpr (CJ != 0) j += CJ * term7;

    return baffling(r, i, j);
  }

// Operations with scalars.

  inline baffling operator +(const float &s) {
    return baffling(this->rpart + s, this->ipart, this->jpart);
  }

  inline baffling operator -(const float &s) {
    return baffling(this->rpart - s, this->ipart, this->jpart);
  }

  inline baffling operator *(const float &s) {
    // This is the same thing we get with s + 0i + 0j, but saving several
    // terms that don't do anything.
    return baffling(this->rpart * s, this->ipart * s, this->jpart * s);
  }

  // unary negation; same as multiplication by scalar -1.
  inline baffling operator -() {
    return baffling(-this->rpart, -this->ipart, -this->jpart);
  }


  // TODO: Division, etc.

  //   *  1  i  j
  //   1  1  i  j
  //   i  i -1  1
  //   j  j  1 -1

 private:
};

using baffling =
  baffling_tmpl<0, 1, 0,
                0, 0, 1,
                -1, 0, 0>;

static void Mandelbrot() {
  static constexpr int SIZE = 1024;
  static constexpr int NUM_THREADS = 12;
  static constexpr int MAX_ITERS = 32;

  // static constexpr float XMIN = -2.3f, XMAX = 1.3f;
  // static constexpr float YMIN = -1.8f, YMAX = 1.8f;
  static constexpr float XMIN = -2.0f, XMAX = 2.0f;
  static constexpr float YMIN = -2.0f, YMAX = 2.0f;

  static constexpr float WIDTH = XMAX - XMIN;
  static constexpr float HEIGHT = YMAX - YMIN;

  int idx = 0;
  for (float jplane = -2.0; jplane <= 2.0; jplane += 0.025f) {
    ImageRGBA img(SIZE, SIZE);

    ParallelComp2D(
        SIZE, SIZE,
        [jplane, &img](int yp, int xp) {

          float x = (xp / (float)SIZE) * WIDTH + XMIN;
          float y = ((SIZE - yp) / (float)SIZE) * HEIGHT + YMIN;

          baffling z(0, 0, 0);
          baffling c(x, y, jplane);

          for (int i = 0; i < MAX_ITERS; i++) {
            if (z.Abs() > 2.0f) {
              // Escaped. The number of iterations gives the pixel.
              float f = i / (float)MAX_ITERS;
              uint32 color = ColorUtil::LinearGradient32(
                  ColorUtil::HEATED_METAL, f);
              img.SetPixel32(xp, yp, color);
              return;
            }

            z = z * z + c;
          }

          // Possibly in the set.
          img.SetPixel32(xp, yp, 0xFFFFFFFF);
        },
        NUM_THREADS);

    img.Save(StringPrintf("baffling-mandelbrot-%d.png", idx));
    idx++;
  }
}


int main(int argc, char **argv) {
  Mandelbrot();

  return 0;
}
