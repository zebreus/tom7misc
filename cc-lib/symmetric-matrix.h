
#ifndef _CC_LIB_SYMMETRIC_MATRIX_H
#define _CC_LIB_SYMMETRIC_MATRIX_H

#include <cstddef>
#include <vector>
#include <utility>

// Symmetric, square matrix. At(x, y) == At(y, x).
template<class T>
struct SymmetricMatrix {
  SymmetricMatrix() : SymmetricMatrix(0) {}
  explicit inline SymmetricMatrix(size_t width, T t = {});

  size_t Width() const { return width; }
  size_t Height() const { return width; }
  size_t Size() const { return width; }

  inline T &At(int row, int col);
  inline const T &At(int row, int col) const;

 private:
  size_t width = 0;
  std::vector<T> values;
};


// Template implementations follow.

template<class T>
SymmetricMatrix<T>::SymmetricMatrix(size_t w, T t) :
  width(w), values((w * (w + 1)) / 2, t) {}

template<class T>
T &SymmetricMatrix<T>::At(int row, int col) {
  if (row < col) {
    std::swap(row, col);
  }

  size_t idx = row * (row + 1) / 2 + col;
  return values[idx];
}

template<class T>
const T &SymmetricMatrix<T>::At(int row, int col) const {
  if (row < col) {
    std::swap(row, col);
  }

  size_t idx = row * (row + 1) / 2 + col;
  return values[idx];
}


#endif
