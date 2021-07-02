
#ifndef _CC_LIB_FONTS_ISLAND_FINDER_H
#define _CC_LIB_FONTS_ISLAND_FINDER_H

#include <utility>
#include <map>
#include <vector>
#include <cstdint>
#include <tuple>

#include "image.h"


// Island Finder takes a 1-bit bitmap image and breaks it into nested
// equivalence classes ("islands") of connected components. The typical
// application is processing font glyphs.


struct IslandFinder {
  // The input bitmap is 8-bit, but we only treat it as 1-bit (0 =
  // sea, >0 = land). The image is automatically padded with two
  // pixels of sea on all sides, so Width() below will be
  // bitmap.Width() + 4.
  // 
  // Typical usage is to immediately call Fill and then work
  // with the results of GetMaps.
  explicit IslandFinder(const ImageA &bitmap);

  // Usually you want to immediately run Fill after constructing.
  void Fill();
  
  int Height() const;
  int Width() const;

  std::pair<int, int> GetXY(int index) const;
  int Index(int x, int y) const;

  bool IsLand(int x, int y) const;

  // Information about a pixel, which should be eventually
  // shared with the full equivalence class.
  struct Info {
    Info(int d, int pc) : depth(d), parent_class(pc) {}
    int depth = -1;
    // Note: need to resolve this with GetClass, as the
    // parent could be unioned with something later (is
    // it actually possible? well in any case, it is right
    // to look up the class).

    int parent_class = -1;
  };

  // private?
  bool Visited(int idx) const;

  Info GetInfo(int idx) const;

  // Return the equivalence class that this index currently
  // belongs in. Not const because it performs path compression.
  int GetClass(int idx);

  // Get the output of this process as three components:
  //  - Two bitmaps the size of the original bitmap:
  //      (removing the padding used for internal purposes).
  //    - Depth map. Value is the depth.
  //    - Equivalence classes. Value is arbitrary unique id,
  //      though depth zero will be equivalence class 0.
  //  - A map from each equivalence class in the second image
  //    to its parent equivalence class. The exception is 0,
  //    which has no parent and does not appear in the map.
  // Since the output is 8-bit, there can only be up to 255
  // in each case.
  std::tuple<ImageA, ImageA, std::map<uint8_t, uint8_t>> GetMaps();

  // During or after Fill(), use this to generate a visualization of
  // that process. Just for debugging.
  ImageRGBA DebugBitmap();

private:
  // Pre-processed bitmap with pixels >0 for land, =0 for sea. The
  // image must be padded such that it has two pixels of sea on every
  // side.
  const ImageA img;
  const int radix;

  // Same size as image. Gives the pixel's current equivalence
  // class; each one starts in its own (value -1) to begin.
  // Union-find-like algorithm.
  std::vector<int> eqclass;

  // For each pixel, nullopt if we have not yet visited it.
  // If we have visited, its depth (will never change) and
  // parent pixel (must canonicalize via GetClass).
  std::vector<std::optional<Info>> classinfo;

  void SetInfo(int idx, int depth, int parent);
  void Union(int aidx, int bidx);
  
  static ImageA Preprocess(const ImageA &bitmap);
};


#endif
