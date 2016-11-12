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

#ifndef PROB_MAP_HPP_
#define PROB_MAP_HPP_

template <class Predictor, size_t kProbs>
class DynamicProbMap {
public:
  Predictor probs_[kProbs];

  size_t GetUpdater(size_t bit) const {
    return bit;
  }

  // Get stretched prob.
  int GetP(size_t index) const {
    return probs_[index].getP();
  }

  template <typename Table>
  void Update(size_t index, size_t bit_updater, const Table& table, size_t update = 9) {
    probs_[index].update(bit_updater, update);
  }

  template <typename Table>
  void SetP(size_t index, int p, Table& t) {
    probs_[index].setP(p);
  }

  template <typename Table>
  void UpdateAll(Table& table) {}

  template <typename Table>
  int GetSTP(size_t index, Table& t) const {
    return t.st(GetP(index));
  }
};

// Keeps track of stretched probabilities.
// format is: <learn:8><prob:32><stp:16>
template <size_t kProbs>
class FastAdaptiveProbMap {
public:
  static constexpr size_t kPShift = 31;
  static constexpr size_t kProbBits = 12;
  uint64_t probs_[kProbs];
  static constexpr size_t kLearnShift = 32;
  static constexpr size_t kSTPShift = kLearnShift + 8;
public:
  size_t GetUpdater(size_t bit) const {
    return (bit << kPShift);
  }
  
  template <typename Table>
  ALWAYS_INLINE void SetP(size_t index, int p, Table& t) {
    Set(index, p << (kPShift - kProbBits), 9, t.st(p));
  }

  template <typename Table>
  ALWAYS_INLINE void Update(size_t index, size_t bit_updater, const Table& table, size_t dummy = 0) {
    uint64_t p = probs_[index];
    p >>= 16;
    const uint32_t lower = static_cast<uint32_t>(p);
    const uint8_t learn = static_cast<uint8_t>(p >> kLearnShift);
    p += (bit_updater - lower) >> learn;
    const uint16_t st = table.st(lower >> (kPShift - kProbBits));
    p = (p << 16) | st;
    probs_[index] = p;
  }

  ALWAYS_INLINE int GetP(size_t index) const {
    uint32_t p = probs_[index] >> 16;
    return p >> (kPShift - kProbBits);
  }

  template <typename Table>
  ALWAYS_INLINE int GetSTP(size_t index, const Table& table) const {
    return static_cast<int16_t>(static_cast<uint16_t>(probs_[index]));
  }

  void SetLearn(size_t index, size_t learn) {
    auto p = probs_[index];
    p &= 0xFFFFFFFFFFFF;
    p |= (learn << 48);
    probs_[index] = p;
  }

private:
  ALWAYS_INLINE void Set(size_t index, uint32_t p, uint8_t learn, int16_t stp) {
    uint64_t acc = learn;
    acc = (acc << 32) | p;
    acc = (acc << 16) | static_cast<uint16_t>(stp);
    probs_[index] = acc;
  }
};

template <class Predictor, size_t kProbs>
class FastProbMap {
  Predictor probs_[kProbs];
  int stp_[kProbs];
public:
  // Get stretched prob.
  int GetP(size_t index) const {
    return probs_[index].getP();
  }

  template <typename Table>
  void UpdateAll(Table& table) {
    for (size_t i = 0; i < kProbs; ++i) {
      stp_[i] = table.st(GetP(i));
    }
  }

  void Update(size_t index, size_t bit, size_t update = 9) {
    probs_[index].update(bit, update);
  }

  template <typename Table>
  void SetP(size_t index, int p, Table& t) {
    probs_[index].setP(p);
    stp_[index] = t.st(p);
  }

  template <typename Table>
  int GetSTP(size_t index, Table& t) const {
    return stp_[index];
  }
};

#endif
