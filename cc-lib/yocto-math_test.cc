
#include "yocto-math.h"

#include <cstdlib>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

#define CHECK_FEQ(a, b) do {                                      \
  auto fa = (a), fb = (b);                                        \
  CHECK(std::abs(fa - fb) < 1.0e-6)                               \
    << "Expected equal, but got:\n"                               \
    << #a << " = " << fa << "\n"                                  \
    << #b << " = " << fb << "\n";                                 \
  } while (false)

static void TestVectors() {
  yocto::vec3f a = {1.0f, 2.0f, 3.0f};
  yocto::vec3f b = {4.0f, 5.0f, 6.0f};
  yocto::vec3f c = a + b;

  CHECK_FEQ(c.x, 5.0f);
  CHECK_FEQ(c.y, 7.0f);
  CHECK_FEQ(c.z, 9.0f);

  float dot_prod = yocto::dot(a, b);
  CHECK_FEQ(dot_prod, 1.0f * 4.0f + 2.0f * 5.0f + 3.0f * 6.0f);
}

static void TestMatrices() {
  yocto::mat3f ident;
  yocto::vec3f v = {1.0f, 2.0f, 3.0f};
  yocto::vec3f res = ident * v;

  CHECK_FEQ(res.x, 1.0f);
  CHECK_FEQ(res.y, 2.0f);
  CHECK_FEQ(res.z, 3.0f);
}

static void TestQuaternions() {
  yocto::quat4f q1 = {0.0f, 0.0f, 0.0f, 1.0f};
  yocto::quat4f q2 = {1.0f, 0.0f, 0.0f, 0.0f};
  yocto::quat4f q3 = q1 * q2;

  CHECK_FEQ(q3.x, 1.0f);
  CHECK_FEQ(q3.y, 0.0f);
  CHECK_FEQ(q3.z, 0.0f);
  CHECK_FEQ(q3.w, 0.0f);
}

static void TestFrames() {
  yocto::vec3f t = {10.0f, 20.0f, 30.0f};
  yocto::frame3f f = yocto::translation_frame(t);
  yocto::vec3f p = {1.0f, 2.0f, 3.0f};
  yocto::vec3f pt = yocto::transform_point(f, p);

  CHECK_FEQ(pt.x, 11.0f);
  CHECK_FEQ(pt.y, 22.0f);
  CHECK_FEQ(pt.z, 33.0f);
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestVectors();
  TestMatrices();
  TestQuaternions();
  TestFrames();

  Print("OK\n");
  return 0;
}


