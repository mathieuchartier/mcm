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

#ifndef _SSE_HPP_
#define _SSE_HPP_

#include "Mixer.hpp"
#include "Model.hpp"

// template <size_t kProbBits, size_t kStemBits = 5, class StationaryModel = fastBitModel<int, kProbBits, 8, 30>>
template <size_t kProbBits, size_t kStemBits = 5, class StationaryModel = bitLearnModel<kProbBits, 8, 30>>
class SSE {
  static const size_t kStems = (1 << kStemBits) + 1;
  static const size_t kMaxP = 1 << kProbBits;
  static const size_t kProbMask = (1 << (kProbBits - kStemBits)) - 1;
  static const size_t kRound = 1 << (kProbBits - kStemBits - 1);
  size_t pw = 0;
  size_t opt = 0;
  size_t count = 0;
public:
  std::vector<StationaryModel> models;

  void setOpt(size_t var) {
    opt = var;
  }

  template <typename Table>
  void init(size_t num_ctx, const Table* table) {
    pw = opt = count = 0;
    check(num_ctx > 0);
    models.resize(num_ctx * kStems);
    for (size_t i = 0; i < kStems; ++i) {
      auto& m = models[i];
      int p = std::min(static_cast<uint32_t>(i << (kProbBits - kStemBits)), static_cast<uint32_t>(kMaxP));
      m.init(table != nullptr ? table->sq(p - 2048) : p);
    }
    size_t ctx = 1;
    while (ctx * 2 <= num_ctx) {
      std::copy(&models[0], &models[kStems * ctx], &models[0] + kStems * ctx);
      ctx *= 2;
    }
    std::copy(&models[0], &models[kStems * (num_ctx - ctx)], &models[0] + ctx * kStems);
  }

  ALWAYS_INLINE int p(size_t p, size_t ctx) {
    dcheck(p < kMaxP);
    const size_t idx = p >> (kProbBits - kStemBits);
    dcheck(idx < kStems);
    const size_t s1 = ctx * kStems + idx;
    const size_t mask = p & kProbMask;
    pw = s1 + (mask >> (kProbBits - kStemBits - 1));
    return (models[s1].getP() * (1 + kProbMask - mask) + models[s1 + 1].getP() * mask) >> (kProbBits - kStemBits);
  }

  ALWAYS_INLINE void update(size_t bit) {
#if 0
    // 4 bits to 9 bits.
    const size_t delta1 = 4 * KB * opt;
    const size_t delta2 = delta1 + 0x10 * KB;
    const size_t delta3 = delta2 + 0x100 * KB;
    const size_t delta4 = delta3 + 0x1000 * KB;
    const size_t delta5 = delta4 + 0x10000 * KB;
    const size_t update = 3 +
      (count > delta1) +
      (count > delta2) +
      (count > delta3) +
      (count > delta4) +
      (count > delta5);
    count++;
    models[pw].update(bit, update);
#endif
    models[pw].update(bit);
  }
};

class SimpleBaseModel {
  static const uint32_t kShift = 8;
  static const uint32_t kShiftMask = (1u << kShift) - 1;
public:
  uint32_t GetP() const {
    return p_ >> kShift;
  }

  void Update(size_t bit, size_t rate) {
    p_ += (bit - p_) & kShiftMask;
  }

private:
  uint32_t p_;
};

template <size_t kProbBits, size_t kStemBits = 5>
class NSSE {
  static const size_t kStems = (1 << kStemBits) + 1;
  static const size_t kMaxP = 1 << kProbBits;
  static const size_t kProbMask = (1 << (kProbBits - kStemBits)) - 1;
  static const size_t kRound = 1 << (kProbBits - kStemBits - 1);
  size_t pw = 0;
  size_t opt = 0;
  size_t count = 0;
  using StationaryModel = bitLearnModel<kProbBits, 8, 30>;
  // using StationaryModel = fastBitModel<int, kProbBits, 9, 30>;
public:
  std::vector<StationaryModel> models;

  void setOpt(size_t var) {
    opt = var;
  }

  template <typename Table>
  void init(size_t num_ctx, const Table* table) {
    pw = count = 0;
    check(num_ctx > 0);
    models.resize(num_ctx * kStems);
    for (size_t i = 0; i < kStems; ++i) {
      auto& m = models[i];
      int p = std::min(static_cast<uint32_t>(i << (kProbBits - kStemBits)), static_cast<uint32_t>(kMaxP));
      m.init(table != nullptr ? table->sq(p - 2048) : p);
    }
    size_t ctx = 1;
    while (ctx * 2 <= num_ctx) {
      std::copy(&models[0], &models[kStems * ctx], &models[kStems * ctx]);
      ctx *= 2;
    }
    std::copy(&models[0], &models[kStems * (num_ctx - ctx)], &models[ctx * kStems]);
  }

  ALWAYS_INLINE int p(size_t p, size_t ctx) {
    dcheck(p < kMaxP);
    const size_t idx = p >> (kProbBits - kStemBits);
    dcheck(idx < kStems);
    const size_t s1 = ctx * kStems + idx;
    size_t mask = p & kProbMask;
    pw = s1 + (mask >> (kProbBits - kStemBits - 1));
    // return (models[s1].getP() * (1 + kProbMask - mask) + models[s1 + 1].getP() * mask) >> (kProbBits - kStemBits);
    int p0 = models[s1].getP();
    int p1 = models[s1 + 1].getP();
    return p0 + (((p1 - p0) * mask + kRound) >> (kProbBits - kStemBits));
    // return (models[s1].getP() * (1 + kProbMask - mask) + models[s1 + 1].getP() * mask) >> (kProbBits - kStemBits);
  }

  ALWAYS_INLINE void update(size_t bit) {
    // models[pw].update(bit, 7);
    models[pw].update(bit);
  }
};

template <size_t kProbBits, size_t kStemBits = 5>
class MixSSE {
  static const size_t kWeights = 3;
  using SSEMixer = Mixer<int, kWeights>;
  SSEMixer* cur_ = nullptr;
  int sp = 0;
  static const int kMaxValue = 1 << kProbBits;
  static const int kMinST = -kMaxValue / 2;
  static const int kMaxST = kMaxValue / 2;

  int sq_[kMaxST - kMinST];
  int* sq_ptr_;
  int sq_p_;
  int st_p_;
  int opt_;
public:
  MixerArray<SSEMixer> mixers_;

  void setOpt(size_t var) {
    opt_ = var;
  }

  template <typename Table>
  void init(size_t num_ctx, const Table* table) {
    mixers_.Init(num_ctx, 382, true);
    sq_ptr_ = &sq_[-kMinST];
    for (int p = kMinST; p <= kMaxST; ++p) {
      sq_ptr_[p] = table->sq(p);
    }
  }

  ALWAYS_INLINE int p(size_t p, size_t ctx) {
    mixers_.SetContext(ctx);
    // int stp = static_cast<int>(p) + kMinST;
    int stp = mixers_.GetMixer()->P(9, st_p_ = (static_cast<int>(p) + kMinST), kMaxST, kMinST);
    stp = Clamp(stp, kMinST, kMaxST - 1);
    sq_p_ = sq_ptr_[stp];
    return sq_p_;
  }

  ALWAYS_INLINE void update(size_t bit) {
    // models[pw].update(bit, 7);
    // mixers_.GetMixer()->Update(sq_p_, bit, kProbBits, 28, 1, st_p_, kMaxST, kMinST);
  }
};

template <size_t kProbBits, size_t kStemBits = 5, class StationaryModel = fastBitModel<int, kProbBits, 8, 30>>
class FastSSE {
  static const size_t kStems = 1 << kStemBits;
  static const size_t kMaxP = 1 << kProbBits;
  size_t pw = 0;
  size_t opt = 0;
public:
  std::vector<StationaryModel> models;

  template <typename Table>
  void init(size_t num_ctx, const Table* table) {
    check(num_ctx > 0);
    models.resize(num_ctx * kStems);
    for (size_t i = 0; i < kStems; ++i) {
      auto& m = models[i];
      int p = static_cast<uint32_t>(i << (kProbBits - kStemBits)) +
        static_cast<uint32_t>(1U << (kProbBits - kStemBits - 1));
      m.init(table != nullptr ? table->sq(p - 2048) : p);
    }
    size_t ctx = 1;
    while (ctx * 2 <= num_ctx) {
      std::copy(&models[0], &models[kStems * ctx], &models[kStems * ctx]);
      ctx *= 2;
    }
    std::copy(&models[0], &models[kStems * (num_ctx - ctx)], &models[ctx * kStems]);
  }

  ALWAYS_INLINE int p(size_t p, size_t ctx) {
    dcheck(p < kMaxP);
    const size_t idx = p >> (kProbBits - kStemBits);
    dcheck(idx < kStems);
    return models[pw = ctx * kStems + idx].getP();
  }

  ALWAYS_INLINE void update(size_t bit) {
    models[pw].update(bit);
  }
};

#endif
