
#include <initializer_list>
#include <cstdio>
#include <cstdint>

int main(int argc, char **argv) {

  for (int target : { 2, 325, 9375, 28178, 450775, 9780504, 1795265022 }) {
    printf("// %d\n"
           "  uint64_t redc%d =\n",
           target, target);

    int depth = 0;
    uint64_t base = 1;
    while (target != 0) {
      if (target & 1) {
        printf("AddMod(redc%llu, ", base);
        depth++;
      }
      base <<= 1;
      target >>= 1;
    }
    for (int i = 0; i < depth; i++) printf(", n)");
    printf(";\n\n");
  }

  return 0;
}
