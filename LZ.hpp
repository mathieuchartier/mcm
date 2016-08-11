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

#ifndef _LZ_HPP_
#define _LZ_HPP_

#include "CyclicBuffer.hpp"
#include "HashTable.hpp"
#include "Log.hpp"
#include "MatchFinder.hpp"
#include "Mixer.hpp"
#include "Model.hpp"
#include "Range.hpp"
#include "StateMap.hpp"

template <typename MF, typename Encoder, typename Decoder>
class StreamingLZCombo : public Compressor {
public:
  StreamingLZCombo() {}
  virtual void compress(Stream* in, Stream* out, uint64_t max_count = 0xFFFFFFFFFFFFFFFF) {
    BufferedStreamReader<4 * KB> sin(in);
    BufferedStreamWriter<4 * KB> sout(out);
    Encoder enc;
    MF match_finder;
    while (max_count > 0) {
      auto match = match_finder.findNextMatch(sin);
      auto nm_len = enc.encodeNonMatch(match_finder);
      if (match.length() > 0) {
        enc.encodeMatch(match);
      } else if (nm_len == 0) {
        break;  // Done processing.
      }
    }
    enc.flush(sout);
  }
  virtual void decompress(Stream* in, Stream* out, uint64_t max_count = 0xFFFFFFFFFFFFFFFF) {
    BufferedStreamReader<4 * KB> sin(in);
    Decoder decoder;
    while (max_count > 0) {
      auto match = decoder.decodeMatch(in);
      auto nm_len = decoder.non_match_len;
      if (match.length() == 0) {
        break;
      }
    }
  }

protected:
};

template <const size_t kMatchBits, const size_t kDistBits>
class SimpleEncoder {
  static const size_t kTotalBits = 1u + kMatchBits + kDistBits;
  static const size_t kRoundedBits = RoundUp(kTotalBits, 8u);
  static const size_t kDistMask = (1u << kDistBits) - 1;
  static_assert(kRoundedBits % 8 == 0, "must be aligned");
public:
  template <typename SOut>
  void encodeMatch(SOut& sout, const Match& match) {
    size_t w = 1u;
    w = (w << kMatchBits) + match.Length();
    w = (w << kDistBits) + match.Pos();
    w <<= kRoundedBits - kTotalBits;
    if (kRoundedBits >= 24) sout.put((w >> 24) & 0xFF);
    if (kRoundedBits >= 16) sout.put((w >> 16) & 0xFF);
    if (kRoundedBits >= 8) sout.put((w >> 8) & 0xFF);
    if (kRoundedBits >= 0) sout.put((w >> 0) & 0xFF);
  }
  // Non match is always before the match.
  template <typename SIn>
  void decodeMatch(SIn& in, Match* out_match, size_t* out_non_match_len, uint8_t* out_non_match) {
    uint8_t b = in.get();
    const auto match_flag = b & 0x80;
    if (match_flag) {
      *out_non_match_len = 0;
      b ^= match_flag;
      size_t w = b;
      if (kRoundedBits >= 8) w = (w << 8) | in.get();
      if (kRoundedBits >= 16) w = (w << 8) | in.get();
      if (kRoundedBits >= 24) w = (w << 8) | in.get();
      *out_match = Match(w >> kDistBits, w & kDistMask);
    } else {
      // Read non match len.
      *out_non_match_len = b;
      for (size_t i = 0; i < b; ++i) {
        out_non_match[i] = in.get();
      }
    }
  }
  template <typename SOut, typename MF>
  void encodeNonMatch(SOut& sout, MF& match_finder) {
    // sout. match_finder.nonMatchLength();
  }
};

class CMRolz : public Compressor {
  // SS table
  static const uint32_t kShift = 12;
  static const int kMaxValue = 1 << kShift;
  static const int kMinST = -kMaxValue / 2;
  static const int kMaxST = kMaxValue / 2;
  typedef ss_table<short, kMaxValue, kMinST, kMaxST, 8> SSTable;
  typedef fastBitModel<int, kShift, 9, 30> StationaryModel;

  struct Entry {
    void set(uint32_t pos, uint32_t hash) {
      pos_ = pos;
      hash_ = hash;
    }
    uint32_t pos_;
    uint32_t hash_;
  };

  static const size_t kMinMatch = 6;
  static const size_t kMaxMatch = 255;
  static const size_t kNumberRolzEntries = 256;
  CyclicDeque<uint8_t> lookahead_;
  CyclicBuffer<uint8_t> buffer_;
  Entry entries_[256][kNumberRolzEntries];
  MTF<uint8_t> mtf_[256];
  // CM
  typedef Mixer<int, 2> CMMixer;
  std::vector<CMMixer> mixers_;
  static const uint32_t kNumStates = 256;
  StationaryModel probs_[2][kNumStates];
  uint8_t state_trans_[kNumStates][2];
  uint8_t order1_[256 * 256];
  uint8_t order2_[256 * 256 * 256];
  uint8_t order1p_[256 * 256];
  uint8_t order2p_[256 * 256 * 256];
  uint8_t order1l_[256 * 256];
  uint8_t order2l_[256 * 256 * 256];
  uint32_t owhash_;
  // Range coder.
  Range7 ent_;
  SSTable table_;
  ALWAYS_INLINE uint32_t hashFunc(uint32_t a, uint32_t b) const {
    b += a;
    b += rotate_left(b, 11);
    return b ^ (b >> 6);
  }

private:
  template <const bool decode, typename TStream>
  size_t processByte(TStream& stream, uint8_t* ctx1, uint8_t* ctx2, uint32_t c = 0) {
    if (!decode) {
      c <<= 24;
    }
    size_t o0 = 1;
    while ((o0 & 256) == 0) {
      size_t bit = processBit<decode>(stream, decode ? 0 : c >> 31, o0, ctx1, ctx2);
      o0 = o0 * 2 + bit;
      c <<= 1;
    }
    return o0 ^ 256;
  }
  ALWAYS_INLINE uint8_t nextState(uint8_t state, uint32_t bit, uint32_t ctx) {
    probs_[ctx][state].update(bit, 9);
    return state_trans_[state][bit];
  }

  ALWAYS_INLINE int getP(uint8_t state, uint32_t ctx) const {
    return table_.st(probs_[ctx][state].getP());
  }
  template <const bool decode, typename TStream>
  size_t processBit(TStream& stream, size_t bit, size_t o0, uint8_t* ctx1, uint8_t* ctx2) {
    auto s0 = ctx1[o0];
    int p0 = getP(s0, 0);
    auto s1 = ctx2[o0];
    int p1 = getP(s1, 1);
    auto* const cur_mixer = &mixers_[o0];
    int stp = 0; // cur_mixer->p(9, p0, p1);
    int p = table_.sq(stp); // Mix probabilities.
    if (decode) {
      bit = ent_.getDecodedBit(p, kShift);
    }
    dcheck(bit < 2);
    const bool ret = true; // cur_mixer->update(p, bit, kShift, 28, 1, p0, p1);
    if (ret) {
      ctx1[o0] = nextState(s0, bit, 0);
      ctx2[o0] = nextState(s1, bit, 1);
    }
    if (decode) {
      ent_.Normalize(stream);
    } else {
      ent_.encode(stream, bit, p, kShift);
    }
    return bit;
  }

public:
  void init();
  virtual void compress(Stream* in_stream, Stream* out_stream);
  virtual void decompress(Stream* in_stream, Stream* out_stream);
};

// variable order rolz.
class VRolz {
  static const uint32_t kMinMatch = 2U;
  static const uint32_t kMaxMatch = 0xFU + kMinMatch;
public:
  template<uint32_t kSize>
  class RolzTable {
  public:
    RolzTable() {
      init();
    }

    void init() {
      pos_ = 0;
      for (auto& s : slots_) {
        s = 0;
      }
    }

    ALWAYS_INLINE uint32_t add(uint32_t pos) {
      uint32_t old = slots_[pos_];
      if (++pos_ == kSize) {
        pos_ = 0;
      }
      slots_[pos_] = pos;
      return old;
    }

    ALWAYS_INLINE uint32_t operator[](uint32_t index) {
      return slots_[index];
    }

    ALWAYS_INLINE uint32_t size() {
      return kSize;
    }

  private:
    uint32_t pos_;
    uint32_t slots_[kSize];
  };

  RolzTable<16> order1[0x100];
  RolzTable<16> order2[0x10000];

  void addHash(uint8_t* in, uint32_t pos, uint32_t prev);
  uint32_t getMatchLen(uint8_t* m1, uint8_t* m2, uint32_t max_len);
  virtual size_t getMaxExpansion(size_t in_size);
  virtual size_t compressBytes(uint8_t* in, uint8_t* out, size_t count);
  virtual void decompressBytes(uint8_t* in, uint8_t* out, size_t count);
};

/*
class FastMatchFinder : public MemoryMatchFinder {
public:
  void init(byte* in, const byte* limit);
  FastMatchFinder();
  Match findNextMatch() {
    non_match_ptr_ = in_ptr_;
    const byte* match_ptr = nullptr;
    auto lookahead = *reinterpret_cast<const uint32_t*>(in_ptr_);
    size_t dist;
    while (true) {
      const uint32_t hash_index = (lookahead * 190807U) >> 16;
      // const uint32_t hash_index = (lookahead * 2654435761U) >> 20;
      assert(hash_index <= hash_mask_);
      match_ptr = in_ + hash_table_[hash_index];
      hash_table_[hash_index] = static_cast<uint32_t>(in_ptr_ - in_);
      dist = in_ptr_ - match_ptr;
      if (dist - kMinMatch <= kMaxDist - kMinMatch) {
        if (*reinterpret_cast<const uint32_t*>(match_ptr) == lookahead) {
          break;
        }
      }
      lookahead = (lookahead >> 8) | (static_cast<uint32_t>(*(in_ptr_ + sizeof(lookahead))) << 24);
      if (++in_ptr_ >= limit_) {
        // assert(in_ptr_ == limit_);
        non_match_len_ = in_ptr_ - non_match_ptr_;
        return Match();
      }
    }
    non_match_len_ = in_ptr_ - non_match_ptr_;
    // Improve the match.
    size_t len = sizeof(lookahead);
    while (len < dist) {
      typedef unsigned long ulong;
      auto diff = *reinterpret_cast<const ulong*>(in_ptr_ + len) ^ *reinterpret_cast<const ulong*>(match_ptr + len);
      if (UNLIKELY(diff)) {
        ulong idx = 0;
#ifdef WIN32
        // TODO
        _BitScanForward(&idx, diff);
#endif
        len += idx >> 3;
        break;
      }
      len += sizeof(diff);
    }
    len = std::min(len, dist);
    len = std::min(len, static_cast<size_t>(limit_ - in_ptr_));
    assert(match_ptr + len <= in_ptr_);
    assert(in_ptr_ + len <= limit_);
    // Verify match.
    for (uint32_t i = 0; i < len; ++i) {
      assert(in_ptr_[i] == match_ptr[i]);
    }
    Match match;
    match.setDist(dist);
    match.setLength(len);
    in_ptr_ += len;
    return match;
  }

private:
  static const uint32_t kMaxDist = 0xFFFF;
  static const uint32_t kMatchCount = 1;
  static const uint32_t kMinMatch = 4;

  std::vector<uint32_t> hash_storage_;
  uint32_t hash_mask_;
  uint32_t* hash_table_;
};
*/

/*
class LZFast : public MemoryMatchFinder {
public:
  uint32_t opt;
  LZFast() : MemoryMatchFinder(3, 256), opt(0) {
  }
  virtual void setOpt(uint32_t new_opt) {
    opt = new_opt;
  }
  byte* flushNonMatch(byte* out_ptr, const byte* in_ptr, size_t non_match_len);
  virtual size_t getMaxExpansion(size_t in_size);
  virtual size_t compressBytes(byte* in, byte* out, size_t count);
  virtual void decompressBytes(byte* in, byte* out, size_t count);
  template<uint32_t pos>
  ALWAYS_INLINE static byte* processMatch(byte matches, uint32_t lengths, byte* out, byte** in);

private:
  static const bool kCountMatches = true;
  // GreedyMatchFinder match_finder_;
  std::vector<uint32_t> non_matches_;
  // Match format:
  // <byte> top bit = set -> match
  // match len = low bits
  static const size_t kMatchShift = 7;
  static const size_t kMatchFlag = 1U << kMatchShift;
  static const size_t kMinMatch = 4;
  // static const uint32_t kMaxMatch = (0xFF ^ kMatchFlag) + kMinMatch;
  static const size_t kMinNonMatch = 1;
#if 1
  static const size_t kMaxMatch = 0x7F + kMinMatch;
  static const size_t kMaxNonMatch = 0x7F + kMinNonMatch;
#elif 0
  static const size_t kMaxMatch = 16; // 0xF + kMinMatch;
  static const size_t kMaxNonMatch = 16;
#else
  static const size_t kMaxMatch = 16 + kMinMatch ;
  static const size_t kMaxNonMatch = 16 + kMinNonMatch;
#endif
  static const size_t non_match_bits = 2;
  static const size_t extra_match_bits = 5;
};
*/

class LZ4 : public MemoryCompressor {
public:
  virtual size_t getMaxExpansion(size_t in_size);
  virtual size_t compress(uint8_t* in, uint8_t* out, size_t count);
  virtual void decompress(uint8_t* in, uint8_t* out, size_t count);
};

class LZSSE : public MemoryCompressor {
public:
  virtual size_t getMaxExpansion(size_t in_size);
  virtual size_t compress(uint8_t* in, uint8_t* out, size_t count);
  virtual void decompress(uint8_t* in, uint8_t* out, size_t count);
};

// Very fast lz that always copies 16 bytes.
template <class MatchFinder>
class LZ16 : public MemoryCompressor {
  static constexpr size_t kMinMatch = 5;
  static constexpr size_t kMaxMatch = 15;
  static constexpr size_t kMaxNonMatch = 15;
public:
  virtual size_t getMaxExpansion(size_t in_size) {
    return in_size * 2;
  }
  virtual size_t compress(uint8_t* in, uint8_t* out, size_t count);
  virtual void decompress(uint8_t* in, uint8_t* out, size_t count);
};

#endif
