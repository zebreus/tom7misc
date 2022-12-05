
#ifndef _PACTOM_UTIL_H
#define _PACTOM_UTIL_H


#include <memory>

#include "pactom.h"

struct PacTomUtil {
  static std::unique_ptr<PacTom> Load(bool merge_dates);

  static void SetDatesFrom(PacTom *dest, const PacTom &other, int max_threads = 1);

  static void SortByDate(PacTom *dest);

};

#endif
