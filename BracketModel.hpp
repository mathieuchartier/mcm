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

#ifndef _BRACKET_MODEL_HPP_
#define _BRACKET_MODEL_HPP_

#include <algorithm>

#include "Reorder.hpp"
#include "UTF8.hpp"

// Keep track of last special char + o0
class LastSpecialCharModel {
public:
  bool important_map_[256] = {};
  uint8_t last_important_char_ = 0;
  size_t* opts_ = 0;

  void Init(const ReorderMap<uint8_t, 256>& reorder) {
    last_important_char_ = 0;
    std::fill_n(important_map_, 256, false);
  }

  void SetOpts(size_t* opts) {
    opts_ = opts;
  }

  void Update(uint8_t c) {
    if (important_map_[c]) {
      last_important_char_ = c;
    }
  }

  uint32_t GetHash() const {
    uint32_t hash = static_cast<uint32_t>(last_important_char_) * 71231;
    return hash + 123119912;
  }
};

class BracketModel {
private:
  static constexpr size_t kStackSize = 256;
  uint8_t stack_[kStackSize];
  bool special_map_[256];
  size_t stack_pos_ = 0;
  uint32_t len_ = 0;
  uint32_t last_char_ = 0;
  size_t* opts_ = 0;
  uint32_t last_notable_ = 0;
  ReorderMap<uint8_t, 256> reorder_;

  void Push(uint8_t c) {
    if (stack_pos_ >= kStackSize) {
      stack_pos_ = 0;
    }
    stack_[stack_pos_++] = c;
  }

  uint8_t StackPop() {
    return (stack_pos_ == 0) ? 0 : stack_[--stack_pos_];
  }

  uint8_t StackTop() const {
    return stack_pos_ == 0 ? '\0' : stack_[stack_pos_ - 1];
  }

public:
  void Update(uint8_t c) {
    const int enabled_brackets = 0xF;
    if (
      (c == reorder_['['] && (enabled_brackets & 1)) ||
      (c == reorder_['('] && (enabled_brackets & 2)) ||
      (c == reorder_['{'] && (enabled_brackets & 4)) ||
      (c == reorder_['<'] && (enabled_brackets & 8)) ||
      special_map_[c]
      ) {
      len_ = 0;
      Push(c);
    } else if (c == reorder_[']'] && (enabled_brackets & 1)) {
      len_ = 0;
      if (StackPop() != reorder_['[']) {
        stack_pos_ = 0;
      }
    } else if (c == reorder_[')'] && (enabled_brackets & 2)) {
      len_ = 0;
      if (StackPop() != reorder_['(']) {
        stack_pos_ = 0;
      }
    } else if (c == reorder_['}'] && (enabled_brackets & 4)) {
      len_ = 0;
      if (StackPop() != reorder_['{']) {
        stack_pos_ = 0;
      }
    } else if (c == reorder_['>'] && (enabled_brackets & 8)) {
      len_ = 0;
      if (StackPop() != reorder_['<']) {
        stack_pos_ = 0;
      }
    } else {
      ++len_;
    }
    last_char_ = c;
  }

  void SetOpts(size_t* opts) {
    opts_ = opts;
  }

  uint32_t GetHash() const {
    uint32_t hash = last_char_;
    hash = (hash << 8) | StackTop();
    // hash = (hash << 8) | last_notable_;
    hash = (hash << 1) | std::min(len_, 1u);
    return hash;
  }

  void Init(const ReorderMap<uint8_t, 256>& reorder) {
    stack_pos_ = 0;
    len_ = 0;
    last_char_ = 0;
    last_notable_ = 0;
    reorder_ = reorder;
    std::fill_n(special_map_, 256, false);
    uint8_t chars[] = { 42,62,36,92,34,72,10 };
    for (auto c : chars) special_map_[reorder[c]] = true;
    // for (size_t i = 0; i < 10; ++i) if (opts_[i]) special_map_[reorder[opts_[i]]] = true;
  }
};

#endif