/*	MCM file compressor

  Copyright (C) 2016, Google Inc.
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

#ifndef _REORDER_HPP_
#define _REORDER_HPP_

#include <algorithm>

#include "Util.hpp"

template <typename Type, size_t kCount>
class ReorderMap {
public:
  ReorderMap() {
    for (size_t i = 0; i < kCount; ++i) {
      forward_[i] = inverse_[i] = i;
    }
  }
  void Copy(Type* reorder) {
    std::copy_n(reorder, kCount, inverse_);
    int count[kCount] = {};
    for (size_t i = 0; i < kCount; ++i) {
      forward_[inverse_[i]] = i;
      check(++count[inverse_[i]] == 1);
    }
  }
  Type operator[](size_t i) const {
    return forward_[i];
  }
  Type Backward(size_t i) const {
    return inverse_[i];
  }

private:
  Type forward_[kCount];
  Type inverse_[kCount];
};

#endif
