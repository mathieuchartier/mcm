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

#ifndef _MIXER_HPP_
#define _MIXER_HPP_

#include <emmintrin.h>
#include "Util.hpp"
#include "Compressor.hpp"


template <const uint32_t fp_shift = 14>
class Mix1 {
public:
  static const int round = 1 << (fp_shift - 1);
  // Each mixer has its own set of weights.
  int w;
  // Current learn rate.
  int skew;
public:
  Mix1() {
    init();
  }

  void init() {
    w = (1 << fp_shift);
    skew = 0;
  }

  // Calculate and return prediction.
  ALWAYS_INLINE uint32_t p(int pr) const {
    return (pr * w + (skew << 12)) >> fp_shift;
  }

  // Neural network learn, assumes probs are stretched.
  ALWAYS_INLINE void update(int p0, int pr, uint32_t bit, uint32_t pshift = 12) {
    int err = ((bit << pshift) - pr) * 16;
    int round = 1 << (fp_shift - 1);
    w += (p0 * err + round) >> fp_shift;
    skew += ((skew << 12) + round) >> fp_shift;
  }
};

template <typename T, const uint32_t kWeights>
class Mixer {
public:
  // Each mixer has its own set of weights.
  T w_[kWeights];

  // Skew weight.
  int skew_;

  // Current learn rate.
  int learn_;
public:
  Mixer() {
    Init(12);
  }

  ALWAYS_INLINE static uint32_t NumWeights() {
    return kWeights;
  }

  ALWAYS_INLINE int GetLearn() const {
    return learn_;
  }

  ALWAYS_INLINE int NextLearn(size_t max_shift) {
    auto before = learn_;
    ++learn_;
    learn_ -= learn_ >> max_shift;
    return before;
  }

  ALWAYS_INLINE T GetWeight(uint32_t index) const {
    assert(index < kWeights);
    return w_[index];
  }

  ALWAYS_INLINE void SetWeight(uint32_t index, T weight) {
    assert(index < kWeights);
    w_[index] = weight;
  }

  void Init(int prob_shift, int extra = 0) {
    for (auto& cw : w_) {
      cw = static_cast<T>(((16 + extra) << prob_shift) / kWeights / 16);
    }
    // Last weight is skew.
    skew_ = 0;
    learn_ = 0;
  }

  // "Fast" version
  ALWAYS_INLINE int P(
    int prob_shift,
    int p0 = 0, int p1 = 0, int p2 = 0, int p3 = 0, int p4 = 0, int p5 = 0, int p6 = 0, int p7 = 0,
    int p8 = 0, int p9 = 0, int p10 = 0, int p11 = 0, int p12 = 0, int p13 = 0, int p14 = 0, int p15 = 0) const {
    int64_t ptotal = skew_;
    if (kWeights > 0) ptotal += p0 * static_cast<int>(GetWeight(0));
    if (kWeights > 1) ptotal += p1 * static_cast<int>(GetWeight(1));
    if (kWeights > 2) ptotal += p2 * static_cast<int>(GetWeight(2));
    if (kWeights > 3) ptotal += p3 * static_cast<int>(GetWeight(3));
    if (kWeights > 4) ptotal += p4 * static_cast<int>(GetWeight(4));
    if (kWeights > 5) ptotal += p5 * static_cast<int>(GetWeight(5));
    if (kWeights > 6) ptotal += p6 * static_cast<int>(GetWeight(6));
    if (kWeights > 7) ptotal += p7 * static_cast<int>(GetWeight(7));
    if (kWeights > 8) ptotal += p8 * static_cast<int>(GetWeight(8));
    if (kWeights > 9) ptotal += p9 * static_cast<int>(GetWeight(9));
    if (kWeights > 10) ptotal += p10 * static_cast<int>(GetWeight(10));
    if (kWeights > 11) ptotal += p11 * static_cast<int>(GetWeight(11));
    if (kWeights > 12) ptotal += p12 * static_cast<int>(GetWeight(12));
    if (kWeights > 13) ptotal += p13 * static_cast<int>(GetWeight(13));
    if (kWeights > 14) ptotal += p14 * static_cast<int>(GetWeight(14));
    if (kWeights > 15) ptotal += p15 * static_cast<int>(GetWeight(15));
    return ptotal >> prob_shift;
  }

  ALWAYS_INLINE bool Update(int pr, uint32_t bit,
    uint32_t prob_shift = 12, int limit = 24, int delta_round = 250, int skew_learn = 1,
    int learn_mult = 31, size_t shift = 16,
    int p0 = 0, int p1 = 0, int p2 = 0, int p3 = 0, int p4 = 0, int p5 = 0, int p6 = 0, int p7 = 0,
    int p8 = 0, int p9 = 0, int p10 = 0, int p11 = 0, int p12 = 0, int p13 = 0, int p14 = 0, int p15 = 0) {
    const int64_t base_learn = static_cast<int64_t>(bit << prob_shift) - pr;
    // const int delta_round = (1 << shift) >> (prob_shift - delta);
    const int64_t err = base_learn * learn_mult;
    const bool ret = err < static_cast<int64_t>(-delta_round) || err > static_cast<int64_t>(delta_round);
    if (ret) {
      UpdateRec<0>(p0, err, shift);
      UpdateRec<1>(p1, err, shift);
      UpdateRec<2>(p2, err, shift);
      UpdateRec<3>(p3, err, shift);
      UpdateRec<4>(p4, err, shift);
      UpdateRec<5>(p5, err, shift);
      UpdateRec<6>(p6, err, shift);
      UpdateRec<7>(p7, err, shift);
      UpdateRec<8>(p8, err, shift);
      UpdateRec<9>(p9, err, shift);
      UpdateRec<10>(p10, err, shift);
      UpdateRec<11>(p11, err, shift);
      UpdateRec<12>(p12, err, shift);
      UpdateRec<13>(p13, err, shift);
      UpdateRec<14>(p14, err, shift);
      UpdateRec<15>(p15, err, shift);
      skew_ += err << skew_learn;
      learn_ += learn_ < limit;
    }
    return ret;
  }

private:
  template <const int kIndex>
  ALWAYS_INLINE void UpdateRec(int64_t p, int64_t err, size_t shift) {
    if (kWeights > kIndex) {
      w_[kIndex] += (err * p) >> shift;
    }
  }
};

template <const uint32_t weights, const uint32_t fp_shift = 16, const uint32_t wshift = 7>
class MMXMixer {
public:
  static const int round = 1 << (fp_shift - 1);
  // Each mixer has its own set of weights.
  __m128i w; // Add dummy skew weight at the end.
  int skew;
  // Current learn rate.
  int learn, pad1, pad2;
public:
  MMXMixer() {
    init();
  }

  ALWAYS_INLINE int getLearn() const {
    return learn;
  }

  short getWeight(uint32_t index) const {
    assert(index < weights);
    switch (index) {
    case 0: return _mm_extract_epi16(w, 0);
    case 1: return _mm_extract_epi16(w, 1);
    case 2: return _mm_extract_epi16(w, 2);
    case 3: return _mm_extract_epi16(w, 3);
    case 4: return _mm_extract_epi16(w, 4);
    case 5: return _mm_extract_epi16(w, 5);
    case 6: return _mm_extract_epi16(w, 6);
    case 7: return _mm_extract_epi16(w, 7);
    }
    return 0;
  }

  void init() {
    w = _mm_cvtsi32_si128(0);
    if (weights) {
      int iw = (1 << fp_shift) / weights;
      if (weights > 0) w = _mm_insert_epi16(w, iw, 0);
      if (weights > 1) w = _mm_insert_epi16(w, iw, 1);
      if (weights > 2) w = _mm_insert_epi16(w, iw, 2);
      if (weights > 3) w = _mm_insert_epi16(w, iw, 3);
      if (weights > 4) w = _mm_insert_epi16(w, iw, 4);
      if (weights > 5) w = _mm_insert_epi16(w, iw, 5);
      if (weights > 6) w = _mm_insert_epi16(w, iw, 6);
      if (weights > 7) w = _mm_insert_epi16(w, iw, 7);
    }
    skew = 0;
    learn = 192;
  }

  // Calculate and return prediction.
  ALWAYS_INLINE uint32_t p(__m128i probs) const {
    __m128i dp = _mm_madd_epi16(w, probs);
    // p0*w0+p1*w1, ...

    // SSE5: _mm_haddd_epi16?
    // 2 madd, 2 shuffles = ~8 clocks?
    dp = _mm_add_epi32(dp, _mm_shuffle_epi32(dp, shuffle<1, 0, 3, 2>::value));
    dp = _mm_add_epi32(dp, _mm_shuffle_epi32(dp, shuffle<2, 3, 0, 1>::value));

    return (_mm_cvtsi128_si32(dp) + (skew << wshift)) >> fp_shift;
  }

  // Neural network learn, assumes probs are stretched.
  ALWAYS_INLINE bool update(__m128i probs, int pr, uint32_t bit, uint32_t pshift = 12, uint32_t limit = 13) {
    int err = ((bit << pshift) - pr) * learn;
    bool ret = false;
    if (fastAbs(err) >= (round >> (pshift - 1))) {
      err >>= 3;
      probs = _mm_slli_epi32(probs, 3);
      if (err > 32767) err = 32767; // Make sure we don't overflow.
      if (err < -32768) err = -32768;
      // I think this works, we shall see.
      auto serr = static_cast<uint32_t>(static_cast<uint16_t>(err));
      __m128i verr = _mm_shuffle_epi32(_mm_cvtsi32_si128(serr | (serr << 16)), 0);
      // shift must be 16
      w = _mm_adds_epi16(w, _mm_mulhi_epi16(probs, verr));
      // Rounding, is this REALLY worth it???
      // w = _mm_adds_epi16(w, _mm_srli_epi16(_mm_mullo_epi16(probs, verr), 15));
      ret = true;
    }
    static const uint32_t sq_learn = 9;
    skew += (err + (1 << (sq_learn - 1))) >> sq_learn;
    learn -= learn > limit;
    return ret;
  }
};

// Very basic logistic mixer.
template <const uint32_t weights, const uint32_t fp_shift = 16, const uint32_t wshift = 7>
class FloatMixer {
public:
  static const int round = 1 << (fp_shift - 1);
  // Each mixer has its own set of weights.
  float w[weights + 1]; // Add dummy skew weight at the end.
  // Current learn rate.
  int learn;
public:
  FloatMixer() {
    init();
  }

  ALWAYS_INLINE int getLearn() const {
    return learn;
  }

  ALWAYS_INLINE float getWeight(uint32_t index) const {
    assert(index < weights);
    return w[index];
  }

  void init() {
    if (weights) {
      float fdiv = float(weights);
      for (auto& cw : w) {
        cw = 1.0f / fdiv;
      }
    }
    w[weights] = 0.0;
    learn = 192;
  }

  // "Fast" version
  ALWAYS_INLINE float p(
    float p0 = 0, float p1 = 0, float p2 = 0, float p3 = 0,
    float  p4 = 0, float p5 = 0, float p6 = 0, float p7 = 0) const {
    float ptotal = 0;
    if (weights > 0) ptotal += p0 * w[0];
    if (weights > 1) ptotal += p1 * w[1];
    if (weights > 2) ptotal += p2 * w[2];
    if (weights > 3) ptotal += p3 * w[3];
    if (weights > 4) ptotal += p4 * w[4];
    if (weights > 5) ptotal += p5 * w[5];
    if (weights > 6) ptotal += p6 * w[6];
    if (weights > 7) ptotal += p7 * w[7];
    ptotal += w[weights];
    return ptotal;
  }

  // "Fast" version
  ALWAYS_INLINE bool update(
    float p0, float p1, float p2, float p3, float p4, float p5, float p6, float p7,
    int pr, uint32_t bit, uint32_t pshift = 12, uint32_t limit = 13) {
    float err = float(((1 ^ bit) << pshift) - pr) * learn * (1.0f / 256.0f);
    // Branch is around 50 / 50.
    updateRec<0>(p0, err);
    updateRec<1>(p1, err);
    updateRec<2>(p2, err);
    updateRec<3>(p3, err);
    updateRec<4>(p4, err);
    updateRec<5>(p5, err);
    updateRec<6>(p6, err);
    updateRec<7>(p7, err);
    updateRec<7>(1.0f, err);
    learn -= learn > static_cast<int>(limit);
    return true;
  }

private:
  template <const int index>
  ALWAYS_INLINE void updateRec(float p, float err) {
    if (weights > index) {
      w[index] += p * err;
    }
  }
};

template <typename Mixer>
class MixerArray {
  std::vector<Mixer> mixers_;
  Mixer* cur_mixers_;
public:
  template <typename... Args>
  void Init(size_t count, Args... args) {
    mixers_.resize(count);
    for (auto& m : mixers_) {
      m.Init(args...);
    }
    SetContext(0);
  }

  ALWAYS_INLINE size_t Size() const {
    return mixers_.size();
  }

  ALWAYS_INLINE void SetContext(size_t ctx) {
    cur_mixers_ = &mixers_[ctx];
  }

  ALWAYS_INLINE size_t GetContext() const {
    return cur_mixers_ - &mixers_[0];
  }

  ALWAYS_INLINE Mixer* GetMixer() {
    return cur_mixers_;
  }

  ALWAYS_INLINE Mixer* GetMixer(size_t idx) {
    return &mixers_[idx];
  }
};

#endif
