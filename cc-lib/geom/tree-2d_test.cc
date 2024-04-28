
#include "tree-2d.h"

#include <string>

#include "base/logging.h"

static void TestInts() {
  Tree2D<int, std::string> tree;

  using Pos = Tree2D<int, std::string>::Pos;

  std::vector<std::tuple<int, int, std::string>> points = {
    {0, 4, "a"},
    {-1, -1, "b"},
    {100, 300, "c"},
    {100, 301, "d"},
    {100, 299, "e"},
    {-100, 300, "f"},
    {-100, 299, "g"},
    {-100, 301, "h"},
    {-99, 300, "i"},
    {-99, 299, "j"},
    {-99, 301, "k"},
    {-101, 300, "l"},
    {-101, 299, "m"},
    {-101, 301, "n"},
  };

  for (const auto &[x, y, s] : points) {
    tree.Insert(x, y, s);
  }

  tree.DebugPrint();

  for (const auto &[x, y, s] : points) {

    for (int radius : {1, 2, 10}) {
      for (int d : {0, 1, -1}) {
        for (bool dir : {false, true}) {
          int qx = x + (dir ? d * radius : 0.0);
          int qy = y + (dir ? 0.0 : d * radius);
          std::vector<std::tuple<Pos, std::string, double>> near =
          tree.LookUp(std::make_pair(qx, qy),
                      radius);
          bool has = [&]() {
              for (const auto &[p, ss, dist] : near) {
                const auto &[xx, yy] = p;
                if (xx == x && yy == y && ss == s)
                  return true;
              }
              return false;
            }();

          CHECK(has) << "Didn't find point " << s << " with radius "
                     << radius << ". The query was (" << qx << ","
                     << qy << "). Num near: " << near.size();
        }
      }
    }

  }
}

int main(int argc, char **argv) {

  TestInts();

  printf("OK");
  return 0;
}
