/*	MCM file compressor

  Copyright (C) 2015, Google Inc.
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

#ifndef _MATCH_FINDER_HPP_
#define _MATCH_FINDER_HPP_

#include <algorithm>

#include "CyclicBuffer.hpp"
#include "Util.hpp"

// Generic match.
class Match {
public:
  ALWAYS_INLINE Match(size_t pos = 0, size_t len = 0) : pos_(pos), len_(len) {}
  ALWAYS_INLINE size_t Pos() const { return pos_; }
  ALWAYS_INLINE size_t Length() const { return len_; }

private:
  // Position, could be a distance back.
  size_t pos_;

  // Length of match.
  size_t len_;
};

// Generic match finder.
class MatchFinder {
public:
  MatchFinder(size_t min_match, size_t max_match)
    : min_match_(min_match),
    max_match_(max_match) {
  }
  // If non match len == 0 and match == 0 then we are done.
  // virtual Match FindNextMatch() = 0;
  // virtual size_t GetNonMatchLen() const = 0;
  ALWAYS_INLINE size_t MinMatch() const { return min_match_; }
  ALWAYS_INLINE size_t MaxMatch() const { return max_match_; }

private:
  const size_t min_match_;
  const size_t max_match_;

protected:
  size_t nonmatch_len_;
};

class BaseMatchFinder : public MatchFinder {
public:
  // Interface for template inheritance.
  size_t lookaheadPop() {
    abort();
    return 0;
  }
  size_t lookaheadSize() const {
    abort();
    return 0;
  }
  size_t lookahead(size_t idx) {
    abort();
    return 0;
  }
  size_t bufferPos() const {
    abort();
    return 0;
  }
  void bufferPush(size_t c) {
    abort();
  }
  bool nonmatchPush(size_t c) {
    abort();
    return false;
  }
  template <typename Stream>
  void refillRead(Stream& stream) {
    abort();
  }
};

class StreamingMatchFinder : public MatchFinder {
public:
  StreamingMatchFinder(size_t min_match, size_t max_match) : MatchFinder(min_match, max_match) {}
  template <typename Stream>
  void RefillRead(Stream& stream) {
    while (!lookahead_.Full()) {
      int c = stream.get();
      if (c == EOF) break;
      lookahead_.PushBack(c);
    }
  }
  // Return 0 to max_match_ as len.
  size_t MatchLen(size_t pos) {
    const size_t max_match = MaxMatch();
    size_t len = 0;
    for (;len < max_match && lookahead_[len] != buffer_[pos + len]; ++len);
    return len;
  }

  // Interface for template inheritance.
  size_t lookaheadMove() {
    const uint8_t c = lookahead_.Front();
    lookahead_.PopFront();
    buffer_.Push(c);
    return c;
  }
  size_t LookaheadSize() const {
    return lookahead_.Size();
  }
  size_t Lookahead(size_t idx) {
    return lookahead_[idx];
  }
  size_t BufferPos() const {
    return buffer_.Pos();
  }
  bool NonmatchPush(size_t c) {
    if (nonmatch_len_ >= nonmatch_.size()) {
      return false;
    }
    nonmatch_[nonmatch_len_++] = c;
    return true;
  }

protected:
  static const size_t kMaxNonMatch = 4 * KB;
  CyclicBuffer<uint8_t> buffer_;
  CyclicDeque<uint8_t> lookahead_;
  StaticArray<uint8_t, kMaxNonMatch> nonmatch_;
  size_t nonmatch_len_;
};

// Used for fast memory LZ compressors.
class MemoryMatchFinder : public MatchFinder {
public:
  MemoryMatchFinder(uint8_t* buffer, size_t buffer_size, size_t min_match, size_t max_match)
    : MatchFinder(min_match, max_match),
    buffer_(buffer),
    ptr_(buffer),
    buffer_end_(buffer + buffer_size),
    last_match_(buffer) {
  }

  // Return 0 to max_match_ as len.
  ALWAYS_INLINE size_t MatchLen(size_t pos, size_t lookahead_offset = 0) const {
    static constexpr bool kFastMatch = true;
    size_t len = 0;
    auto* lookahead_ptr = ptr_ + lookahead_offset;
    auto* buffer_ptr = buffer_ + pos;
    if (kFastMatch) {
      auto lookahead = *reinterpret_cast<const uint32_t*>(lookahead_ptr);
      if (lookahead != *reinterpret_cast<const uint32_t*>(buffer_ptr)) {
        return false;
      }
      len = sizeof(uint32_t);
    }
    const size_t max_match = std::min(BufferPos() - pos, LookaheadSize());
    while (len < max_match && lookahead_ptr[len] == buffer_ptr[len]) {
      ++len;
    }
    return len;
  }

  // Interface for template inheritance.
  size_t LookaheadMove() {
    return *(ptr_++);
  }

  void Skip(size_t count) {
    ptr_ += count;
  }

  size_t LookaheadSize() const {
    return buffer_end_ - ptr_;
  }

  size_t Lookahead(size_t idx) {
    return ptr_[idx];
  }

  size_t BufferPos() const {
    return ptr_ - buffer_;
  }

  size_t NonMatchLen() const {
    return nonmatch_len_;
  }

  bool NonMatchPush(size_t c) {
    nonmatch_len_++;
    return true;
  }

  void RefillRead() {}

protected:
  static const size_t kMaxNonMatch = 4 * KB;

  // Start of buffer.
  const uint8_t* buffer_;

  // Current position in buffer.
  const uint8_t* ptr_;

  // End of the buffer.
  const uint8_t* buffer_end_;

  // Where the last match was.
  const uint8_t* last_match_;
};

template <typename MF>
class FastMatchFinder : public MF {
public:
  template <class... Args>
  FastMatchFinder(size_t buffer_mask, size_t max_offset, Args... args)
    : MF(std::forward<Args>(args)...),
    mask_(buffer_mask),
    max_offset_(max_offset) {
    hash_table_.resize(mask_ + 1);
  }

  ALWAYS_INLINE size_t HashLookahead(size_t offset) {
    size_t h = 0;
    const auto min_match = MF::MinMatch();
    for (size_t i = 0; i < min_match; ++i) {
      h = hashFunc(MF::Lookahead(i), h);
    }
    return h;
  }

  Match FindNextMatch() {
    const auto min_match = MF::MinMatch();
    const auto max_match = MF::MaxMatch();
    MF::nonmatch_len_ = 0;
    for (;;) {
      MF::RefillRead();
      size_t remain = MF::LookaheadSize();
      if (remain >= min_match + 3) {
        size_t h = HashLookahead(0);
        auto* pos1 = &hash_table_[h & mask_];
        Match ret;
        size_t offset = MF::BufferPos() - *pos1;
        if (offset <= max_offset_) {
          size_t len = MF::MatchLen(*pos1);
          static constexpr size_t kOptMatch = 6;
          // Extend backwards.
          if (len >= min_match) {
            if (false && len <= kOptMatch) {
              // Try to optimize.
              for (size_t i = 1; i < len; ++i) {
                h = HashLookahead(i);
                auto* new_pos = &hash_table_[h & mask_];
                size_t new_offset = MF::BufferPos() - *new_pos;
                if (offset <= max_offset_) {
                  size_t new_len = MF::MatchLen(*new_pos);
                  if (new_len > len) {
                    len = new_len;
                  }
                }
              }
            }
            /*
            if (false) {
              size_t new_pos = *pos1;
              while (new_pos > 0 && len < max_match && nonmatch_len_ > 0 && len < offset) {
                --new_pos;
                if (buffer_[buffer_pos_ - 1] != buffer_[new_pos]) break;
                ++len;
                --buffer_pos_;
                --nonmatch_len_;
              }
            }*/
          }
          if (len >= min_match && len > ret.Length()) {
            ret = Match(offset, len);
          }
        }
        *pos1 = MF::BufferPos();
        if (ret.Length() > 0) return ret;
      } else if (remain == 0) {
        break;
      }
      auto c = MF::LookaheadMove();
      if (!MF::NonMatchPush(c)) {
        return Match();
      }
    }
    return Match();
  }

  ALWAYS_INLINE uint32_t hashFunc(uint32_t a, uint32_t b) const {
    b += a;
    b += rotate_left(b, 11);
    return b ^ (b >> 6);
  }

private:
  const size_t mask_;
  const size_t max_offset_;
  std::vector<uint32_t> hash_table_;
  std::vector<uint32_t> hash_table2_;
};

#if 0
class GreedyMatchFinder : public MemoryMatchFinder {
public:
  uint32_t opt;

  void init(uint8_t* in, const uint8_t* limit);
  GreedyMatchFinder();
  Match FindNextMatch();
  ALWAYS_INLINE hash_t HashFunc(uint32_t a, hash_t b) {
    b += a;
    b += rotate_left(b, 6);
    return b ^ (b >> 23);
  }

private:
  static const uint32_t kMinMatch = 4;
  static const uint32_t kMaxDist = 0xFFFF;
  // This is probably not very efficient.
  class Entry {
  public:
    Entry() {
      init();
    }
    void init() {
      pos_ = std::numeric_limits<uint32_t>::max() - kMaxDist * 2;
      hash_ = 0;
    }
    ALWAYS_INLINE static uint32_t getHash(uint32_t word, uint32_t slot) {
      return slot | (word & ~0xFFU);
    }
    ALWAYS_INLINE static uint32_t getLen(uint32_t word) {
      return word & 0xFF;
    }
    ALWAYS_INLINE static uint32_t buildWord(uint32_t h, uint32_t len) {
      return (h & ~0xFFU) | len;
    }
    ALWAYS_INLINE void setHash(uint32_t h) {
      hash_ = h;
    }

    uint32_t pos_;
    uint32_t hash_;
  };
  std::vector<Entry> hash_storage_;
  uint32_t hash_mask_;
  Entry* hash_table_;
};
#endif

#endif

