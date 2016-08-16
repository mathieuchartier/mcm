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

#pragma once

#include "LZ.hpp"

static inline uint8_t* WriteMatch(uint8_t* out, const uint8_t* in, size_t non_match_len, size_t match_len, uint16_t offset) {
  assert(match_len <= 15);
  assert(non_match_len <= 15);
  *(out++) = (match_len << 4) + non_match_len;
  copy16bytes(out, in);
  out += non_match_len;
  if (match_len > 0) {
    *reinterpret_cast<uint16_t*>(out) = offset;
    out += sizeof(uint16_t);
  }
  return out;
}

template <class MatchFinder>
size_t LZ16<MatchFinder>::compress(uint8_t* in, uint8_t* out, size_t count) {
  constexpr bool kStats = true;
  constexpr size_t kMatchLens = 256;
  const uint8_t* limit = in + count;
  const uint8_t* in_ptr = in;
  uint8_t* out_ptr = out;
  // uint8_t* buffer, size_t buffer_size, size_t min_match, size_t max_match
  MatchFinder mf(0xFFFFF, 0xFFFF + kMinMatch, in, count, kMinMatch, kMaxMatch);
  // Stats
  size_t short_offsets = 0, long_offsets = 0, len_bytes = 0, skip_bytes = 0, offset_bytes = 0;
  size_t non_match_lens[kMaxNonMatch + 1] = {}, match_lens[kMatchLens + 1] = {};

  while (in_ptr < limit) {
    auto match = mf.FindNextMatch();
    assert(match.Pos() >= kMinMatch);
    auto non_match_len = mf.NonMatchLen();
#if 0
    if (kIsDebugBuild) {
      assert(match.Pos() + match.Length() <= in_ptr - in + non_match_len);
      for (size_t i = 0; i < match.Length(); ++i) {
        assert(in_ptr - match.Pos() == in_ptr[i + non_match_len]);
      }
    }
#endif
    do {
      size_t len = 0, pos = 0, nm_len = kMaxNonMatch;
      if (non_match_len <= kMaxNonMatch) {
        len = match.Length();
        pos = match.Pos() - kMinMatch;
        nm_len = non_match_len;
        if (kStats) {
          if (pos <= 255) ++short_offsets;
          else ++long_offsets;
        }
      }
      if (kStats) {
        ++len_bytes;
        skip_bytes += nm_len;
        if (len) offset_bytes += 2;
        ++non_match_lens[nm_len];
        ++match_lens[std::min(len, kMatchLens)];
      }
      do {
        auto cur_len = std::min(kMaxMatch, len);
        out_ptr = WriteMatch(out_ptr, in_ptr, nm_len, cur_len, pos);
        in_ptr += nm_len + cur_len;
        non_match_len -= nm_len;
        mf.Skip(cur_len);
        len -= cur_len;
        nm_len = 0;
      } while (len >= kMinMatch);
    } while (non_match_len > 0);
  }
  if (kStats) {
    std::cout << "short=" << short_offsets << " long=" << long_offsets << " len=" << len_bytes << " skip=" << skip_bytes << " offset=" << offset_bytes << std::endl;
    for (size_t i = 0; i <= kMatchLens; ++i) {
      std::cout << "match " << i << " = " << match_lens[i] << std::endl;
    }
    for (size_t i = 0; i <= kMaxNonMatch; ++i) {
      std::cout << "non-match " << i << " = " << non_match_lens[i] << std::endl;
    }
    constexpr size_t kBits = 4;
    size_t bits[kBits] = {};
    for (size_t i = 0; i <= kMaxNonMatch; ++i) {
      for (size_t j = 1; j <= kBits; ++j) {
        size_t m = (1 << j) - 1;
        bits[j - 1] += non_match_lens[i] * std::max((i + m - 1) / m, size_t(1)) * j;
      }

    }
    for (size_t i = 0; i < kBits; ++i) {
      std::cout << "non match bytes for bits " << i + 1 << ": " << bits[i] / kBitsPerByte << std::endl;
    }
  }
  return out_ptr - out;
}

template <class MatchFinder>
void LZ16<MatchFinder>::decompress(uint8_t* in, uint8_t* out, size_t count) {
  const uint8_t* limit = out + count;
  int len;
  if (count == 0) return;
  do {
    uint32_t lens = *(uint8_t*)in;
    ++in;
    uint32_t cur_len;
    cur_len = lens & 0xF; lens >>= 4;
    // if (cur_len) {
    copy16bytes(out, in); in += cur_len; out += cur_len;
    // }
    cur_len = lens; // & 0xF; lens >>= 4;
#if 0
    size_t have_len = cur_len != 0;
    auto offset = *reinterpret_cast<uint16_t*>(in) & -have_len;
    copy16bytes(out, out - offset); in += (have_len << 1); out += cur_len;
#else
    if (cur_len) {
      auto offset = *reinterpret_cast<uint16_t*>(in);
      copy16bytes(out, out - offset - kMinMatch); in += 2; out += cur_len;
    }
#endif
  } while (out < limit);
}
