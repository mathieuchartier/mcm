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


template <const size_t fp_shift = 14>
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
	forceinline size_t p(int pr) const {
		return (pr * w + (skew << 12)) >> fp_shift;
	}

	// Neural network learn, assumes probs are stretched.
	forceinline void update(int p0, int pr, size_t bit, size_t pshift = 12) {
		int err = ((bit << pshift) - pr) * 16;
		int round = 1 << (fp_shift - 1);
		w += (p0 * err + round) >> fp_shift;
		skew += ((skew << 12) + round) >> fp_shift;
	}
};

template <typename T, const size_t weights, const size_t fp_shift = 16, const size_t wshift = 7>
class Mixer {
public:
	static const int round = 1 << (fp_shift - 1);
	// Each mixer has its own set of weights.
	T w[weights + 1]; // Add dummy skew weight at the end.
	// Current learn rate.
	int learn;
public:
	Mixer() {
		init();
	}

	forceinline static size_t size() {
		return weights + 1;
	}

	forceinline static size_t shift() {
		return fp_shift;
	}

	forceinline int getLearn() const {
		return learn;
	}

	forceinline T getWeight(size_t index) const {
		assert(index <= weights);
		return w[index];
	}

	void init(size_t learn_rate = 366) {
		if (weights != 0) {
			int wdiv = weights;
			for (auto& cw : w) {
				cw = T((1 << fp_shift) / wdiv);
			}
		}
		w[weights] = 0;
		learn = learn_rate;
	}

	// Calculate and return prediction.
	forceinline size_t p(int* probs) const {
		int ptotal = 0;
		for (size_t i = 0; i < weights; ++i) {
			ptotal += probs[i] * int(w[i]);
		}
		ptotal += w[weights] << wshift;
		return ptotal >> fp_shift;
	}

	// "Fast" version
	forceinline size_t p(
		int p0 = 0, int p1 = 0, int p2 = 0, int p3 = 0,
		int p4 = 0, int p5 = 0, int p6 = 0, int p7 = 0) const {
		int ptotal = 0;
		if (weights > 0) ptotal += p0 * static_cast<int>(w[0]);
		if (weights > 1) ptotal += p1 * static_cast<int>(w[1]);
		if (weights > 2) ptotal += p2 * static_cast<int>(w[2]);
		if (weights > 3) ptotal += p3 * static_cast<int>(w[3]);
		if (weights > 4) ptotal += p4 * static_cast<int>(w[4]);
		if (weights > 5) ptotal += p5 * static_cast<int>(w[5]);
		if (weights > 6) ptotal += p6 * static_cast<int>(w[6]);
		if (weights > 7) ptotal += p7 * static_cast<int>(w[7]);
		ptotal += w[weights] << wshift;
		return ptotal >> fp_shift;
	}

	// Neural network learn, assumes probs are stretched.
	forceinline void update(int* probs, int pr, size_t bit, size_t pshift = 12, size_t limit = 13) {
		int err = ((bit << pshift) - pr) * learn;
		T round = 1 << (fp_shift - 1);
		for (size_t i = 0; i < weights; ++i) {
			w[i] += (probs[i] * err + round) >> fp_shift;
		}
		static const size_t sq_learn = 9;
		w[weights] += (err + (1 << (sq_learn - 1))) >> sq_learn;
		learn -= learn > limit;
	}

	// "Fast" version
	forceinline bool update(
			int p0, int p1, int p2, int p3, int p4, int p5, int p6, int p7, 
			int pr, size_t bit, size_t pshift = 12, size_t limit = 13) {		
		int err = ((bit << pshift) - pr) * learn;
		
		bool ret = false;

		// Branch is around 50 / 50.
		if (err < -(round >> (pshift - 1)) || err > (round >> (pshift - 1))) {
			updateRec<0>(p0, err);
			updateRec<1>(p1, err);
			updateRec<2>(p2, err);
			updateRec<3>(p3, err);
			updateRec<4>(p4, err);
			updateRec<5>(p5, err);
			updateRec<6>(p6, err);
			updateRec<7>(p7, err);
			ret = true;
		}
		
		static const size_t sq_learn = 9;
		w[weights] += ((err >> (sq_learn - 1)) + 1) >> 1;
		learn -= learn > static_cast<int>(limit);
		return ret;
	}

private:
	template <const int index>
	forceinline void updateRec(int p, int err) {
		if (weights > index) {
			w[index] += (p * err + round) >> fp_shift;
			// if (w[index] < 0) w[index] = 0;
		}
	}
};

template <const size_t weights, const size_t fp_shift = 16, const size_t wshift = 7>
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

	forceinline int getLearn() const {
		return learn;
	}

	short getWeight(size_t index) const {
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
	forceinline size_t p(__m128i probs) const {
		__m128i dp = _mm_madd_epi16(w, probs);
		// p0*w0+p1*w1, ...

		// SSE5: _mm_haddd_epi16?
		// 2 madd, 2 shuffles = ~8 clocks?
		dp = _mm_add_epi32(dp, _mm_shuffle_epi32(dp, shuffle<1, 0, 3, 2>::value));
		dp = _mm_add_epi32(dp, _mm_shuffle_epi32(dp, shuffle<2, 3, 0, 1>::value));

		return (_mm_cvtsi128_si32(dp) + (skew << wshift)) >> fp_shift;
	}

	// Neural network learn, assumes probs are stretched.
	forceinline bool update(__m128i probs, int pr, size_t bit, size_t pshift = 12, size_t limit = 13) {
		int err = ((bit << pshift) - pr) * learn;
		bool ret = false;
		if (fastAbs(err) >= (round >> (pshift - 1))) {
			err >>= 3;
			probs = _mm_slli_epi32(probs, 3);
			if (err > 32767) err = 32767; // Make sure we don't overflow.
			if (err < -32768) err = -32768;
			// I think this works, we shall see.
			auto serr = static_cast<size_t>(static_cast<uint16_t>(err));
			__m128i verr = _mm_shuffle_epi32(_mm_cvtsi32_si128(serr | (serr << 16)), 0);
			// shift must be 16
			w = _mm_adds_epi16(w, _mm_mulhi_epi16(probs, verr));
			// Rounding, is this REALLY worth it???
			// w = _mm_adds_epi16(w, _mm_srli_epi16(_mm_mullo_epi16(probs, verr), 15));
			ret = true;
		}
		static const size_t sq_learn = 9;
		skew += (err + (1 << (sq_learn - 1))) >> sq_learn;
		learn -= learn > limit;
		return ret;
	}
};

// Very basic logistic mixer.
template <const size_t weights, const size_t fp_shift = 16, const size_t wshift = 7>
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

	forceinline int getLearn() const {
		return learn;
	}

	forceinline float getWeight(size_t index) const {
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
	forceinline float p(
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
	forceinline bool update(
			float p0, float p1, float p2, float p3, float p4, float p5, float p6, float p7, 
			int pr, size_t bit, size_t pshift = 12, size_t limit = 13) {		
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
	forceinline void updateRec(float p, float err) {
		if (weights > index) {
			w[index] += p * err;
		}
	}
};

#endif
