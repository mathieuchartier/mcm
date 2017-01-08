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

#ifndef GD_HPP_
#define GD_HPP_

#include <sstream>
#include <vector>

class SquaredPredictor {
public:
  template <typename Error>
  Error OptimizeError(Error error) {
    return error;
  }

  template <typename Error, typename Predictor, typename Input>
  Error Cost(const Predictor& predictor, const Input* inputs, Input actual) const {
    const Error delta = predictor.Predict(inputs) - actual;
    return delta * delta;
  }

  template <typename Error, typename Delta, typename Input>
  void Update(const Input input, Delta* delta, Error error) const {
    *delta += error * input;
  }
};

class LogPredictor {
public:
  template <typename Error>
  Error OptimizeError(Error error) {
    return 1.0f / (error > 0.0f ? (1.0f + error) : (-1.0f - error));
  }

  template <typename Error, typename Predictor, typename Input>
  Error Cost(const Predictor& predictor, const Input* inputs, Input actual) const {
    const Error delta = predictor.Predict(inputs) - actual;
    return std::log2f(Input(1.0) + std::abs(delta) * Input(32726.0));
  }

  template <typename Error, typename Delta, typename Input>
  void Update(const Input input, Delta* delta, Error opt_error) const {
    *delta += input * opt_error;
  }
};

template <typename Weight, typename Acc, typename PredictorFunc>
class LinearPredictor {
public:
  template <typename Input>
  Acc Cost(const Input* inputs, Acc actual) const {
    return f_.Cost<Acc>(*this, inputs, actual);
 }

  template <typename Input>
  Acc AverageCost(Input* inputs, Acc* actual, size_t num_samples) const {
    Acc acc = {};
    for (size_t i = 0; i < num_samples; ++i) {
      acc += Cost(inputs + i * w_.size(), actual[i]);
    }
    return acc / double(num_samples);
  }

  template <typename Input>
  Acc Predict(Input* inputs) const {
    Acc acc = {};
    for (size_t i = 0; i < w_.size(); ++i) {
      acc += inputs[i] * w_[i];
    }
    return acc;
  }

  // error is Predict - Actual.
  template <typename Input>
  void Update(Input* inputs, Acc actual, Input* delta) {
    auto error = Predict(inputs) - actual;
    auto opt_error = f_.OptimizeError(error);
    for (size_t i = 0; i < w_.size(); ++i)  {
      f_.Update(inputs[i], &delta[i], opt_error);
    }
  }

  // error is Predict - Actual.
  template <typename Input>
  void UpdateAll(Input* inputs, Acc* actual, Input* delta, size_t num_samples) {
    for (size_t i = 0; i < num_samples; ++i) {
      Update(inputs + i * w_.size(), actual[i], delta);
    }
    for (size_t i = 0; i < w_.size(); ++i) {
      delta[i] /= double(num_samples);
    }
  }

  std::string DumpWeights() const {
    std::ostringstream oss;
    for (auto w : w_) oss << w << ",";
    return oss.str();
  }

  template <typename Input>
  void UpdateWeights(Input* delta, Input alpha) {
    for (size_t i = 0; i < w_.size(); ++i) {
      w_[i] -= delta[i] * alpha;
    }
  }

  LinearPredictor(size_t n) : w_(n, 0.0f) {}

  void SetWeight(size_t i, Weight w) {
    w_[i] = w;
  }

  Weight GetWeight(size_t i) const {
    return w_[i];
  }

private:
  std::vector<Weight> w_;  // theta
  PredictorFunc f_;
};

template <typename T>
inline void GradientDescent(T* samples, size_t num_samples, size_t num_weights) {
}
  
#endif
