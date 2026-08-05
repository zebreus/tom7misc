
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <windef.h>
#include <windows.h>
#include <wingdi.h>
#include <winnt.h>

#include "ansi.h"
#include "base/logging.h"
#include "image.h"
#include "util.h"
#include "png.h"

#include "base/print.h"

void CheckClipboard(HWND hwnd, int &counter,
                    std::unique_ptr<ImageRGBA> &prev_image) {
  Print("Clipboard event...\n");
  if (!IsClipboardFormatAvailable(CF_DIB)) return;

  if (!OpenClipboard(hwnd)) return;

  HANDLE h_data = GetClipboardData(CF_DIB);
  if (h_data) {
    void *p_data = GlobalLock(h_data);
    size_t size = GlobalSize(h_data);

    if (p_data && size >= sizeof(BITMAPINFOHEADER)) {
      BITMAPINFOHEADER *bih = (BITMAPINFOHEADER *)p_data;
      int colors = 0;
      if (bih->biBitCount <= 8) {
        colors = bih->biClrUsed ? bih->biClrUsed : (1 << bih->biBitCount);
      } else if (bih->biCompression == BI_BITFIELDS &&
                 bih->biSize == sizeof(BITMAPINFOHEADER)) {
        colors = 3;
      }

      uint32_t offset = 14 + bih->biSize + colors * sizeof(RGBQUAD);

      std::vector<uint8_t> bmp;
      bmp.resize(14 + size);
      bmp[0] = 'B';
      bmp[1] = 'M';
      uint32_t file_size = (uint32_t)bmp.size();
      memcpy(&bmp[2], &file_size, 4);
      uint32_t reserved = 0;
      memcpy(&bmp[6], &reserved, 4);
      memcpy(&bmp[10], &offset, 4);

      memcpy(bmp.data() + 14, p_data, size);

      std::unique_ptr<ImageRGBA> img(ImageRGBA::LoadFromMemory(bmp));
      if (img.get() == nullptr) {
        Print("Couldn't decode BMP.\n");
      } else {
        if (prev_image.get() == nullptr || !(*prev_image == *img)) {
          std::string filename = std::format("{}.png", counter++);
          std::vector<uint8_t> png = PNG::EncodeInMemory(*img, 9);
          if (img->Save(filename)) {
            Print("Saved {}\n", filename);
            prev_image = std::move(img);
            img.reset(nullptr);
          } else {
            Print("Failed to save {}\n", filename);
          }
        }
      }
    }
    GlobalUnlock(h_data);
  }
  CloseClipboard();
}

int main(int argc, char **argv) {
  ANSI::Init();

  HWND hwnd = CreateWindowExA(0, "STATIC", "clip_listener", 0, 0, 0, 0, 0,
                              HWND_MESSAGE, NULL, NULL, NULL);
  if (!hwnd) {
    Print("Failed to create message window.\n");
    return 1;
  }

  if (!AddClipboardFormatListener(hwnd)) {
    Print("Failed to add clipboard listener.\n");
    return 1;
  }

  Print("Listening for images on clipboard...\n");

  int counter = 1;
  std::unique_ptr<ImageRGBA> prev_image;

  // Check immediately on startup just in case there's already an image.
  // CheckClipboard(hwnd, counter, prev_image);

  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0) > 0) {
    if (msg.message == WM_CLIPBOARDUPDATE) {
      CheckClipboard(hwnd, counter, prev_image);
    }
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  RemoveClipboardFormatListener(hwnd);
  return 0;
}

