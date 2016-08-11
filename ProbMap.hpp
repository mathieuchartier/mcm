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

  // Get stretched prob.
  int GetP(size_t index) const {
    return probs_[index].getP();
  }

  void Update(size_t index, size_t bit, size_t update = 9) {
    probs_[index].update(bit, update);
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
