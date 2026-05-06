
#ifndef _CC_LIB_UNION_FIND_H
#define _CC_LIB_UNION_FIND_H

#include <vector>

struct UnionFind {
  explicit UnionFind(int size) : arr(size, -1) {}

  inline int Find(int a);

  void Union(int a, int b) {
    int fa = Find(a), fb = Find(b);
    if (fa != fb) arr[fa] = fb;
  }

  int Size() const { return arr.size(); }

  // return every element to its own equivalence class.
  void Reset() {
    for (int &x : arr) x = -1;
  }

 private:
  std::vector<int> arr;
};


// Implementations follow.

// Although the standard recursive path compression
// is adorably simple, it runs the risk of exceeding
// C++ stack limits (the chain can be arbitrarily
// deep the first time). Take two passes.
int UnionFind::Find(int a) {
  int root = a;
  while (arr[root] != -1) {
    root = arr[root];
  }

  int cur = a;
  while (cur != root) {
    int next = arr[cur];
    arr[cur] = root;
    cur = next;
  }

  return root;
}

#endif
