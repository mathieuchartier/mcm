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

#include "LZ.hpp"

#include <cstdint>
#include <fstream>

#ifdef WIN32
#include <intrin.h>
#endif

// #define USE_LZ4
#ifdef USE_LZ4
#include "lz4.h"
#endif

// #define USE_LZSSE
#ifdef USE_LZSSE
#include "lzsse4.h"
#endif

#include <mmintrin.h>

size_t LZ4::getMaxExpansion(size_t in_size) {
#ifdef USE_LZ4
  return LZ4_COMPRESSBOUND(in_size);
#else
  return 0;
#endif
}

size_t LZ4::compress(uint8_t* in, uint8_t* out, size_t count) {
#ifdef USE_LZ4
  return LZ4_compress(reinterpret_cast<char*>(in), reinterpret_cast<char*>(out), count);
#else
  return 0;
#endif
}

void LZ4::decompress(uint8_t* in, uint8_t* out, size_t count) {
#ifdef USE_LZ4
  LZ4_decompress_fast(reinterpret_cast<char*>(in), reinterpret_cast<char*>(out), static_cast<int>(count));
#endif
}

size_t LZSSE::getMaxExpansion(size_t in_size) {
  return in_size * 2;
}

size_t LZSSE::compress(uint8_t* in, uint8_t* out, size_t count) {
#ifdef USE_LZSSE
  auto* parse_state = LZSSE4_MakeFastParseState();
  auto ret = LZSSE4_CompressFast(parse_state, reinterpret_cast<char*>(in), count, reinterpret_cast<char*>(out) + sizeof(uint32_t), getMaxExpansion(count));
  *reinterpret_cast<uint32_t*>(out) = ret;
  LZSSE4_FreeFastParseState(parse_state);
  return ret;
#else
  return 0;
#endif
}

void LZSSE::decompress(uint8_t* in, uint8_t* out, size_t count) {
#ifdef USE_LZSSE
  auto len = *reinterpret_cast<uint32_t*>(in);
  LZSSE4_Decompress(reinterpret_cast<char*>(in) + 4, len, reinterpret_cast<char*>(out), count);
#endif
}

#if 0
size_t MemoryLZ::getMatchLen(byte* m1, byte* m2, byte* limit1) {
  byte* start = m1;
  while (m1 < limit1) {
    uint32_t diff = *reinterpret_cast<uint32_t*>(m1) ^ *reinterpret_cast<uint32_t*>(m2);
    if (UNLIKELY(diff)) {
      unsigned long idx = 0;
      // TODO: Fix _BitScanForward(&idx, diff);
      m1 += idx >> 3;
      break;
    }
    m1 += sizeof(uint32_t);
    m2 += sizeof(uint32_t);
  }
  return m1 - start;
}
#endif

size_t VRolz::getMaxExpansion(size_t in_size) {
  return in_size * 4 / 3;
}

uint32_t VRolz::getMatchLen(uint8_t* m1, uint8_t* m2, uint32_t max_len) {
  uint32_t len = 0;
  while (len <= max_len && m1[len] == m2[len]) {
    ++len;
  }
  return len;
}

size_t VRolz::compressBytes(uint8_t* in, uint8_t* out, size_t count) {
  // Match encoding format.
  // 2 bits index (0 = char, 1 = rolz1, 2 = rolz2, 3 = rolz3??
  // <char> | 3 bits index, 5 bits len.

  // Skip the first 4 bytes for good measure.
  uint8_t* out_ptr = out;
  uint8_t* in_ptr = in;
  uint8_t* limit = in + count;
  *out_ptr++ = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    if (in_ptr < limit) {
      *out_ptr++ = *in_ptr++;
    }
  }
  uint32_t bpos = 0;
  uint8_t cmatch = 0;
  uint8_t buffer[4];
  while (in_ptr < limit) {
    uint32_t prev = *reinterpret_cast<uint32_t*>(in_ptr - sizeof(uint32_t));
    // Try the tables.
    uint32_t best_match = 0;
    uint32_t best_offset = *in_ptr;
    uint32_t best_len = 1;
    uint32_t max_match = std::min(static_cast<uint32_t>(limit - in_ptr), kMaxMatch);
    auto& ord1 = order1[prev >> 24];
    for (uint32_t i = 0; i < ord1.size(); ++i) {
      auto* match_ptr = in + ord1[i];
      uint32_t len = getMatchLen(in_ptr, match_ptr, std::min(max_match, static_cast<uint32_t>(in - match_ptr)));
      if (len > best_len) {
        best_match = 1;
        best_len = len;
        best_offset = i;
      }
    }
    auto& ord2 = order2[prev >> 16];
    for (uint32_t i = 0; i < ord2.size(); ++i) {
      auto* match_ptr = in + ord2[i];
      uint32_t len = getMatchLen(in_ptr, match_ptr, std::min(max_match, static_cast<uint32_t>(in - match_ptr)));
      if (len > best_len) {
        best_match = 2;
        best_len = len;
        best_offset = i;
      }
    }
    if (best_len >= kMinMatch) {
      // Encode match
      in_ptr += best_len;
      cmatch = (cmatch << 2) | best_match;
      buffer[bpos++] = ((best_len - kMinMatch) << 3) | best_offset;
    } else {
      cmatch <<= 2;
      addHash(in, in_ptr - in, prev);
      buffer[bpos++] = *in_ptr++;
    }
    if (bpos >= 4) {
      *out_ptr++ = cmatch;
      for (uint32_t i = 0; i < bpos; ++i) {
        *out_ptr++ = buffer[i];
      }
      cmatch = 0;
      bpos = 0;
    }
  }
  return out_ptr - out;
}

void VRolz::addHash(uint8_t* in, uint32_t pos, uint32_t prev) {
  uint32_t old1 = order1[prev >> 24].add(pos);
  // Extend old 1 into order 2 match.
  if (old1 > sizeof(uint32_t)) {
    prev = *reinterpret_cast<uint32_t*>(in + old1 - sizeof(uint32_t));
    order2[prev >> 16].add(old1);
  }
}

void VRolz::decompressBytes(uint8_t* in, uint8_t* out, size_t count) {
  return;
}

void CMRolz::init() {
  lookahead_.Resize(4 * KB);
  buffer_.Resize(16 * MB);
  for (size_t i = 0; i < 256; ++i) {
    for (size_t j = 0; j < kNumberRolzEntries; ++j)
      entries_[i][j].set(0, 0);
    mtf_[i].init(256);
  }

  // Init CM.
  table_.build(0);
  mixers_.resize(256);
  for (auto& mixer : mixers_) mixer.Init(12);
  NSStateMap<kShift> sm;
  sm.build();
  unsigned short initial_probs[][kNumStates] = {
    {1895,1286,725,499,357,303,156,155,154,117,107,117,98,66,125,64,51,107,78,74,66,68,47,61,56,61,77,46,43,59,40,41,28,22,37,42,37,33,25,29,40,42,26,47,64,31,39,0,0,1,19,6,20,1058,391,195,265,194,240,132,107,125,151,113,110,91,90,95,56,105,300,22,831,997,1248,719,1194,159,156,1381,689,581,476,400,403,388,372,360,377,1802,626,740,664,1708,1141,1012,973,780,883,713,1816,1381,1621,1528,1865,2123,2456,2201,2565,2822,3017,2301,1766,1681,1472,1082,983,2585,1504,1909,2058,2844,1611,1349,2973,3084,2293,3283,2350,1689,3093,2502,1759,3351,2638,3395,3450,3430,3552,3374,3536,3560,2203,1412,3112,3591,3673,3588,1939,1529,2819,3655,3643,3731,3764,2350,3943,2640,3962,2619,3166,2244,1949,2579,2873,1683,2512,1876,3197,3712,1678,3099,3020,3308,1671,2608,1843,3487,3465,2304,3384,3577,3689,3671,3691,1861,3809,2346,1243,3790,3868,2764,2330,3795,3850,3864,3903,3933,3963,3818,3720,3908,3899,1950,3964,3924,3954,3960,4091,2509,4089,2512,4087,2783,2073,4084,2656,2455,3104,2222,3683,2815,3304,2268,1759,2878,3295,3253,2094,2254,2267,2303,3201,3013,1860,2471,2396,2311,3345,3731,3705,3709,2179,3580,3350,2332,4009,3996,3989,4032,4007,4023,2937,4008,4095,2048,},
    {2065,1488,826,573,462,381,254,263,197,158,175,57,107,95,95,104,89,69,76,86,83,61,44,64,49,53,63,46,80,29,57,28,55,35,41,33,43,42,37,57,20,35,53,25,11,10,29,16,16,9,27,15,17,1459,370,266,306,333,253,202,152,115,151,212,135,142,148,128,93,102,810,80,1314,2025,2116,846,2617,189,195,1539,775,651,586,526,456,419,400,335,407,2075,710,678,810,1889,1219,1059,891,785,933,859,2125,1325,1680,1445,1761,2054,2635,2366,2499,2835,2996,2167,1536,1676,1342,1198,874,2695,1548,2002,2400,2904,1517,1281,2981,3177,2402,3366,2235,1535,3171,2282,1681,3201,2525,3405,3438,3542,3535,3510,3501,3514,2019,1518,3151,3598,3618,3597,1904,1542,2903,3630,3655,3671,3761,2054,3895,2512,3935,2451,3159,2323,2223,2722,3020,2033,2557,2441,3333,3707,1993,3154,3352,3576,2153,2849,1992,3625,3629,2459,3643,3703,3703,3769,3753,2274,3860,2421,1565,3859,3877,2580,2061,3781,3807,3864,3931,3907,3924,3807,3835,3852,3910,2197,3903,3946,3962,3975,4068,2662,4062,2662,4052,2696,2080,4067,2645,2424,2010,2325,3186,1931,2033,2514,831,2116,2060,2148,1988,1528,1034,938,2016,1837,1916,1512,1536,1553,2036,2841,2827,3000,2444,2571,2151,2078,4067,4067,4063,4079,4077,4075,3493,4081,4095,2048,},
  };
  for (auto& c : order1_) c = 0;
  for (auto& c : order2_) c = 0;
  for (auto& c : order1p_) c = 0;
  for (auto& c : order2p_) c = 0;
  for (auto& c : order1l_) c = 0;
  for (auto& c : order2l_) c = 0;
  for (uint32_t i = 0; i < kNumStates; ++i) {
    for (uint32_t j = 0; j < 2; ++j) {
      state_trans_[i][j] = sm.getTransition(i, j);
    }
    for (size_t mdl = 0; mdl < 2; ++mdl) {
      probs_[mdl][i].setP(initial_probs[mdl][i]);
    }
  }
  owhash_ = 0;
}

void CMRolz::compress(Stream* in_stream, Stream* out_stream) {
  BufferedStreamReader<4 * KB> sin(in_stream);
  BufferedStreamWriter<4 * KB> sout(out_stream);
  init();
  ent_ = Range7();
  size_t num_match = 0;
  for (;;) {
    while (!lookahead_.Full()) {
      auto c = sin.get();
      if (c == EOF) break;
      lookahead_.PushBack(static_cast<uint8_t>(c));
    }
    if (lookahead_.Size() >= kMinMatch) {
      uint32_t h = 0x97654321;
      for (size_t order = 0; order < kMinMatch; ++order) {
        h = hashFunc(lookahead_[order], h);
      }
      size_t ctx = buffer_[buffer_.Pos() - 1];
      size_t best_len = 0;
      size_t best_idx = 0;
      for (size_t i = 0; i < kNumberRolzEntries; ++i) {
        auto& e = entries_[ctx][i];
        if (e.hash_ == h) {
          uint32_t pos = e.pos_;
          // Check the match.
          size_t match_len = 0;
          while (match_len < lookahead_.Size() && match_len < kMaxMatch) {
            if (buffer_[pos + match_len] != lookahead_[match_len]) break;
            ++match_len;
          }
          if (match_len > best_len) {
            best_len = match_len;
            best_idx = i;
          }
        }
      }
      if (best_len >= kMinMatch) {
        processBit<false>(sout, 0, 0, order1_ + (owhash_ & 0xFF) * 256, order2_ + (owhash_ & 0xFFFF) * 256);
        processByte<false>(sout, order1p_ + (owhash_ & 0xFF) * 256, order2p_ + (owhash_ & 0xFFFF) * 256, best_idx);
        processByte<false>(sout, order1l_ + (owhash_ & 0xFF) * 256, order2l_ + (owhash_ & 0xFFFF) * 256, best_len - kMinMatch);
        entries_[ctx][best_idx].pos_ = buffer_.Pos();
        size_t mtf_idx = mtf_[ctx].find(static_cast<uint8_t>(best_idx));
        mtf_[ctx].moveToFront(mtf_idx);
        for (size_t i = 0; i < best_len; ++i) {
          buffer_.Push(lookahead_.Front());
          lookahead_.PopFront();
        }
        ++num_match;
        // Encode match, update pos.
        continue;
      }
      // No match, update oldest.
      // mtf_.
      entries_[ctx][mtf_[ctx].back()].set(buffer_.Pos(), h);
      mtf_[ctx].moveToFront(mtf_[ctx].size() - 1);
    } else if (lookahead_.Empty()) {
      break;
    }
    auto c = lookahead_.Front();
    processBit<false>(sout, 1, 0, order1_ + (owhash_ & 0xFF) * 256, order2_ + (owhash_ & 0xFFFF) * 256);
    processByte<false>(sout, order1_ + (owhash_ & 0xFF) * 256, order2_ + (owhash_ & 0xFFFF) * 256, c);
    buffer_.Push(c);
    owhash_ = (owhash_ << 8) | c;
    lookahead_.PopFront();
  }
  ent_.flush(sout);
  std::cout << std::endl << "Num match= " << num_match << std::endl;
}

void CMRolz::decompress(Stream* in_stream, Stream* out_stream) {
  BufferedStreamReader<4 * KB> sin(in_stream);
  ent_.initDecoder(sin);
  for (;;) {
    int c = in_stream->get();
    if (c == EOF) break;
    out_stream->put(c);
  }
}
