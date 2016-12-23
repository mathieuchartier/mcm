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

#ifndef _WORD_MODEL_HPP_
#define _WORD_MODEL_HPP_

#include "Reorder.hpp"
#include "UTF8.hpp"

class WordModel {
public:
  // Hashes.
  uint64_t prev;
  uint64_t h1, h2;

  // UTF decoder.
  UTF8Decoder<false> decoder;

  // Length of the model.
  size_t len;
  static const size_t kMaxLen = 31;

  // Transform table.
  static const uint32_t transform_table_size = 256;
  uint32_t transform[transform_table_size];

  uint32_t opt_var_ = 0;
  size_t* opts_ = nullptr;

  void setOpt(uint32_t n) {
    opt_var_ = n;
  }

  void SetOpts(size_t* opts) {
    opts_ = opts;
  }

  ALWAYS_INLINE uint32_t& trans(char c) {
    uint32_t index = (uint32_t)(uint8_t)c;
    check(index < transform_table_size);
    return transform[index];
  }

  WordModel() {}

  void Init(const ReorderMap<uint8_t, 256>& reorder) {
    uint32_t index = 0;
    for (auto& t : transform) t = transform_table_size;
    for (uint32_t i = 'a'; i <= 'z'; ++i) {
      transform[reorder[i]] = index++;
    }
    for (uint32_t i = 'A'; i <= 'Z'; ++i) {
      transform[reorder[i]] = transform[reorder[MakeLowerCase(static_cast<int>(i))]];
    }
    // 6,38,92,3
    trans(reorder[6]) = index++;
    trans(reorder[38]) = index++;
    trans(reorder[92]) = index++;
    trans(reorder[3]) = index++;
    // trans('"') = index++;
    // trans('&') = index++;
    // trans('<') = index++;
    // trans('{') = index++;
    // trans(3) = index++;
    // trans(41) = index++;
    // trans(33) = index++;
    // trans('[') = index++;
    // trans(3) = index++;
    // trans(38) = index++;
    // trans(6) = index++;
    // trans(92) = index++;

#if 0
    trans('À') = trans('à') = index++;
    trans('Á') = trans('á') = index++;
    trans('Â') = trans('â') = index++;
    trans('Ã') = trans('ã') = index++;
    trans('Ä') = trans('ä') = index++;
    trans('Å') = trans('å') = index++;
    trans('Æ') = trans('æ') = index++;

    trans('Ç') = trans('ç') = index++;

    trans('È') = trans('è') = index++;
    trans('É') = trans('é') = index++;
    trans('Ê') = trans('ê') = index++;
    trans('Ë') = trans('ë') = index++;

    trans('Ì') = trans('ì') = index++;
    trans('Í') = trans('í') = index++;
    trans('Î') = trans('î') = index++;
    trans('Ï') = trans('ï') = index++;

    trans('È') = trans('è') = index++;
    trans('É') = trans('é') = index++;
    trans('Ê') = trans('ê') = index++;
    trans('Ë') = trans('ë') = index++;

    trans('Ð') = index++;
    trans('ð') = index++;
    trans('Ñ') = trans('ñ') = index++;

    trans('Ò') = trans('ò') = index++;
    trans('Ó') = trans('ó') = index++;
    trans('Ô') = trans('ô') = index++;
    trans('Ö') = trans('ö') = index++;
    trans('Õ') = trans('õ') = index++;

    trans('Ò') = trans('ò') = index++;
    trans('Ó') = trans('ó') = index++;
    trans('Ô') = trans('ô') = index++;
    trans('Ö') = trans('ö') = index++;

    trans('Ù') = trans('ù') = index++;
    trans('Ú') = trans('ú') = index++;
    trans('Û') = trans('û') = index++;
    trans('Ü') = trans('ü') = index++;
#endif
    for (size_t i = 128; i < 256; ++i) {
      if (transform[reorder[i]] == transform_table_size) {
        transform[reorder[i]] = index++;
      }
    }

    len = 0;
    prev = 0;
    reset();
    decoder.init();
  }

  ALWAYS_INLINE void reset() {
    h1 = 0x1F20239A;
    h2 = 0xBE5FD47A;
    len = 0;
  }

  ALWAYS_INLINE uint32_t getHash() const {
    auto ret = (h1 * 15) ^ (h2 * 41);
    ret ^= ret >> 4;
    return ret;
  }

  ALWAYS_INLINE uint32_t getPrevHash() const {
    return prev;
  }

  ALWAYS_INLINE uint32_t getMixedHash() const {
    auto ret = getHash();
    if (len < 2) {
      ret ^= getPrevHash();
    }
    return ret;
  }

  ALWAYS_INLINE uint32_t get01Hash() const {
    return getHash() ^ getPrevHash();
  }

  ALWAYS_INLINE size_t getLength() const {
    return len;
  }

  // Return true if just finished word.
  bool update(uint8_t c) {
    const auto cur = transform[c];
    if (LIKELY(cur != transform_table_size)) {
      h1 = HashFunc(cur, h1);
      h2 = h1 * 24;
      len += len < 16;
    } else if (len) {
      prev = rotate_left(getHash() * 21, 14);
      reset();
      return true;
    }
    return false;
  }

  ALWAYS_INLINE uint64_t HashFunc(uint64_t c, uint64_t h) {
    /*
    h ^= (h * (1 + 24 * 1)) >> 24;
    // h *= 61;
    h += c;
    h += rotate_left(h, 12);
    return h ^ (h >> 8);
    */
    return (h * 43) + c;
  }
};

class DictXMLModel : public WordModel {
  
  // 40 82 6
public:
  void Init(const ReorderMap<uint8_t, 256>& reorder) {
    last_char_ = 0;
    dict_remain_ = 0;
    escape_ = reorder[0x3];
    upper1_ = reorder[0x4];
    upper2_ = reorder[0x6];
    WordModel::Init(reorder);
  }

  void update(uint8_t c) {
    if (c >= 128) {
      if (last_char_ == escape_) {
        WordModel::update(c);
      } else if (dict_remain_ == 0) {
        if (c < 128 + 40) {
          dict_remain_ = 1;
        } else if (c < 128 + 40 + 82) {
          dict_remain_ = 2;
        } else {
          dict_remain_ = 3;
        }
      } else {
        --dict_remain_;
      }
    } else {
      dict_remain_ = 0;
    }
    last_char_ = c;
    WordModel::update(c);
  }

  uint32_t getMixedHash() {
    auto ret = WordModel::getMixedHash();
    /* if (dict_remain_) {
      ret ^= dict_remain_ * 12931991;
    } */
    return ret;
  }

private:
  size_t dict_remain_;
  uint8_t last_char_;
  uint8_t escape_ = 0;
  uint8_t upper1_ = 0;
  uint8_t upper2_ = 0;
};

class XMLWordModel : public WordModel {
public:
  void init(const ReorderMap<uint8_t, 256>& reorder) {
    for (auto& c : stack_) c = 0;
    stack_pos_ = 0;
    last_symbol_ = 0;
    tag_ = kTagNone;
    WordModel::Init(reorder);
  }

  void update(uint8_t c) {
    bool update = WordModel::update(c);
    if (c == '<') {
      last_symbol_ = c;
      tag_ = kTagOpen;
    } else if (c == '/' && last_symbol_ == '<') {
      last_symbol_ = c;
      tag_ = kTagClose;
    } else if (update) {
      if (tag_ == kTagOpen) {
        // st_[stack_pos_ & kStackMask] = s;
        stack_[stack_pos_++ & kStackMask] = prev;
      } else if (tag_ == kTagClose && c == '>') {
        if (stack_[--stack_pos_ & kStackMask] != prev) {
          stack_pos_ = 0;
        } else {
          int x = 2;
        }
      }
      // Done word.
      tag_ = kTagNone;
      //			s = "";
    }
    // s += c;
  }

  uint32_t getMixedHash() {
    auto ret = WordModel::getMixedHash();
    if (tag_ == kTagClose) {
      ret ^= stack_[(stack_pos_ - 1) & kStackMask] * 3;
    }
    return ret;
  }

private:
  enum CurrentTag {
    kTagOpen,
    kTagClose,
    kTagNone,
  };
  // std::string s;
  CurrentTag tag_;
  static const uint32_t kStackMask = 255;
  uint32_t stack_[kStackMask + 1];
  // std::string st_[kStackMask + 1];
  uint32_t stack_pos_;
  uint8_t last_symbol_ = 0;
};

#endif