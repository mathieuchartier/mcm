/*	MCM file compressor

  Copyright (C) 2013, Google Inc.
  Authors: Mathieu Chartier

  LICENSE

    This file is part of the MCM file compressor.

    MCM is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    MCM is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with MCM.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _LOG_HPP_
#define _LOG_HPP_

#include <cmath>

// squash = ln(p/(1-p))
// stretch = squash^-1 = 1/(1+e^-x)

inline double squash(double p) {
  if (p < 0.0001) return -999.0;
  if (p > 0.9999) return 999.0;
  return (double)std::log((double)p / (1.0 - (double)p));
}

inline double stretch(double p) {
  return 1.0 / double(1.0 + exp((double)-p));
}

inline int roundint(double p) {
  return int(p + 0.5);
}

static int squash_init(int d, int opt = 0) {
  if (d >= 2047) return 4095;
  if (d <= -2047) return 0;
  double k = (4096 - opt) / (double(1 + exp(-(double(d) / (128 + 15 * 10 + 6)))));
  return int(k);
}

// Squash - stretch table
template <typename T, int denom, int minInt, int maxInt, int FP>
struct ss_table {
  static const int total = maxInt - minInt;
  static_assert((total & (total - 1)) == 0, "must be power of 2");
  T stretch_table_[denom];
  static const size_t kFastTableMask = 8 * total - 1;
  T squash_table_fast_[kFastTableMask + 1];
public:
  // probability = p / Denom		
  void build(size_t* opts) {
    T squash_table_[total];
    T* squash_ptr_ = &squash_table_[0 - minInt];
    // From paq9a
    const size_t num_stems = 32 + 1;
    int stems[num_stems] = {
      1,2,4,6,19,25,38,71,82,128,210,323,497,778,1142,1526,
      // 15 3 8 0 3 0 0 0 0 0 0 0 0 0 0 0
      // 1,1,2,2,3,4,5,6,11,19,20,20,27,31,42,53,61,82,93,107,161,196,258,314,426,486,684,810,1048,1246,1521,1724,
      // 1,1,2,3,4,5,6,13,19,22,25,32,38,55,71,77,82,105,128,169,210,267,323,410,497,638,778,960,1142,1334,1526,1787,
      2047,
    };
    for (int i = num_stems / 2 + 1; i < num_stems; ++i) {
      stems[i] = 4096 - stems[num_stems - 1 - i];
    }
    // Interpolate between stems.
    const int stem_divisor = total / (num_stems - 1);
    for (int i = minInt; i < maxInt; ++i) {
      const int pos = i - minInt;
      const int stem_idx = pos / stem_divisor;
      const int stem_frac = pos % stem_divisor;
      squash_table_[pos] =
        (stems[stem_idx] * (stem_divisor - stem_frac) + stems[stem_idx + 1] * stem_frac + stem_divisor / 2) / stem_divisor;
      squash_table_[pos] = Clamp(squash_table_[pos], 1, 4095);
    }
    int pi = 0;
    // Inverse squash function.
    for (int x = minInt; x < maxInt; ++x) {
      // squashPtr[x] = squash_init(x, opts[0]);
      int i = squash_ptr_[x];
      squash_ptr_[x] = Clamp(squash_ptr_[x], 1, 4095);
      for (int j = pi; j < i; ++j) {
        stretch_table_[j] = x;
      }
      pi = i;
    }
    for (int x = pi; x < total; ++x) {
      stretch_table_[x] = 2047;
    }
    for (int i = 0; i <= kFastTableMask; ++i) {
      int p = i;
      if (p >= kFastTableMask / 2) p = i - static_cast<int>(kFastTableMask + 1);
      if (p <= minInt) p = 1;
      else if (p >= maxInt) p = denom - 1;
      else p = squash_ptr_[p];
      squash_table_fast_[i] = p;
    }
  }

  const T* getStretchPtr() const {
    return stretch_table_;
  }

  // 0 <= p < denom
  ALWAYS_INLINE int st(uint32_t p) const {
    return stretch_table_[p];
  }

  // minInt <= p < maxInt
  ALWAYS_INLINE uint32_t sq(int p) const {
    if (p <= minInt) return 1;
    if (p >= maxInt) return denom - 1;
    return sqfast(p);
  }

  ALWAYS_INLINE uint32_t squnsafe(int p) const {
    dcheck(p >= minInt);
    dcheck(p < maxInt);
    return sqfast(p);
  }

  ALWAYS_INLINE uint32_t sqfast(int p) const {
    return squash_table_fast_[static_cast<uint32_t>(p) & kFastTableMask];
  }
};

#endif
