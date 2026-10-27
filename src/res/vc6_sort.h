#pragma once

#include <algorithm>
#include <cstddef>
#include <utility>

namespace ragexe::res {

// The std::sort of the client's Visual C++ 6 library, which several loaders call
// (C3dModelRes's faces, ClientState's tables). It is not stable, so equal elements end
// in the order this exact algorithm leaves them:
// - up to 16 elements: an insertion sort;
// - more: quicksort with the median of the first, middle and last elements as the pivot,
//   recursing into the smaller part and leaving parts of 16 or fewer unsorted, then an
//   insertion sort of the first 16 and an unguarded insertion of each one after.
namespace vc6 {

template <typename T, typename Less>
void UnguardedInsert(T* i, Less less) {
  T v = std::move(*i);
  T* p = i;
  for (; less(v, *(p - 1)); --p) *p = std::move(*(p - 1));
  *p = std::move(v);
}

template <typename T, typename Less>
void InsertionSort(T* first, T* last, Less less) {
  if (first == last) return;
  for (T* i = first + 1; i != last; ++i) {
    if (less(*i, *first)) {
      T v = std::move(*i);
      std::move_backward(first, i, i + 1);
      *first = std::move(v);
    } else {
      UnguardedInsert(i, less);
    }
  }
}

template <typename T, typename Less>
T Median(const T& a, const T& b, const T& c, Less less) {
  if (less(a, b)) {
    if (less(b, c)) return b;
    return less(a, c) ? c : a;
  }
  if (less(a, c)) return a;
  return less(b, c) ? c : b;
}

template <typename T, typename Less>
void QuickSortRange(T* first, T* last, Less less) {
  for (std::ptrdiff_t n = last - first; n > 16; n = last - first) {
    const T pivot = Median(*first, first[n / 2], *(last - 1), less);
    T* i = first;
    T* j = last;
    for (;;) {
      while (less(*i, pivot)) ++i;
      do {
        --j;
      } while (less(pivot, *j));
      if (j <= i) break;
      std::swap(*i, *j);
      ++i;
    }
    if (i - first < last - i) {
      QuickSortRange(first, i, less);
      first = i;
    } else {
      QuickSortRange(i, last, less);
      last = i;
    }
  }
}

}  // namespace vc6

template <typename T, typename Less>
void Vc6Sort(T* first, T* last, Less less) {
  if (last - first <= 16) {
    vc6::InsertionSort(first, last, less);
    return;
  }
  vc6::QuickSortRange(first, last, less);
  vc6::InsertionSort(first, first + 16, less);
  for (T* i = first + 16; i != last; ++i) vc6::UnguardedInsert(i, less);
}

}  // namespace ragexe::res
