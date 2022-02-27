
#include <dlfcn.h>
#include <stdio.h>
#include <cstdint>

int main(int argc, char **argv) {

  void *dl_handle = nullptr;

  if (dl_handle != nullptr)
    dlclose(dl_handle);

  dl_handle = dlopen("./compu-tmp.so", RTLD_NOW);
  if (dl_handle == nullptr) {
    printf("dlopen failed\n");
    char *error = dlerror();
    if (error != nullptr) {
      printf("error: %s\n", error);
    }

    return -1;
  }

  dlerror();

  uint8_t (*get_byte)(int) =
    (uint8_t (*)(int))dlsym(dl_handle, "get_byte_dl");

  char *error = dlerror();
  if (error != nullptr) {
    printf("dlsym failed: %s\n", error);
    return -1;
  }

  printf("symbol loaded");

  printf("get_byte 7: %d\n", (*get_byte)(7));

  dlclose(dl_handle);

  return 0;
}


