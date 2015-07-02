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
