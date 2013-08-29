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

#ifndef _MODEL_HPP_
#define _MODEL_HPP_

#include "Compressor.hpp"
#include <assert.h>
#pragma warning(disable : 4146)

#pragma pack(push)
#pragma pack(1)
template <typename T, const int max>
class floatBitModel
{
	float f;
public:
	floatBitModel()
	{
		init();
	}

	void init()
	{
		f = 0.5f;
	}

	inline void update(T bit, T dummy)
	{
		f += ((float)(bit^1) - f) * 0.02;
		if (f < 0.001) f = 0.001;
		if (f > 0.999) f = 0.999;
	}

	inline size_t getP() const
	{
		return (size_t)(f * (float)max);
	}
};

// Count stored in high bits
#pragma pack(push)
#pragma pack(1)

// Bit probability model (should be rather fast).
template <typename T, const size_t _shift, const size_t _learn_rate = 5, const size_t _bits = 15>
class safeBitModel {
protected:
	T p;
	static const size_t pmax = (1 << _bits) - 1;
public:
	static const size_t shift = _shift;
	static const size_t learn_rate = _learn_rate;
	static const size_t max = 1 << shift;

	void init() {
		p = pmax / 2;
	}

	safeBitModel() {
		init();
	}

	inline void update(T bit) {
		int round = 1 << (_learn_rate - 1);
		p += ((static_cast<int>(bit) << _bits) - static_cast<int>(p) + round) >> _learn_rate;
	}

	inline size_t getP() const {
		size_t ret = p >> (_bits - shift);
		ret += ret == 0;
		return ret;
	}
};

// Bit probability model (should be rather fast).
template <typename T, const size_t _shift, const size_t _learn_rate = 5, const size_t _bits = 15>
class fastBitModel {
protected:
	T p;
	static const size_t pmax = (1 << _bits) - 1;
public:
	static const size_t shift = _shift;
	static const size_t learn_rate = _learn_rate;
	static const size_t max = 1 << shift;

	void init() {
		p = pmax / 2;
	}

	fastBitModel() {
		init();
	}

	inline void update(T bit) {
		int round = 1 << (_learn_rate - 1);
		p += ((static_cast<int>(bit) << _bits) - static_cast<int>(p) + round) >> _learn_rate;
	}

	inline void setP(size_t new_p) {
		p = new_p << (_bits - shift);
	}

	inline size_t getP() const {
		return p >> (_bits - shift);
	}
};

// Bit probability model (should be rather fast).
template <typename T, const size_t _shift, const size_t _learn_rate = 5>
class fastBitSTModel {
protected:
	T p;
public:
	static const size_t shift = _shift;
	static const size_t learn_rate = _learn_rate;
	static const size_t max = 1 << shift;

	void init() {
		p = 0;
	}

	fastBitSTModel() {
		init();
	}

	template <typename Table>
	inline void update(T bit, Table& table) {
		return;
		// Calculate err first.
		int err = (static_cast<int>(bit) << shift) - table.sq(getSTP());
		p += err >> 10;
		const T limit = 2048 << shift; // (_bits - shift); 
		if (p < -limit) p = -limit;
		if (p > limit) p = limit;
	}

	template <typename Table>
	inline void setP(size_t new_p, Table& table) {
		p = table.st(new_p) << shift; 
	}

	// Return the stretched probability.
	inline size_t getSTP() const {
		return p + (1 << shift - 1) >> shift;
	}
};

// Bit probability model (should be rather fast).
template <typename T, const size_t _shift, const size_t _learn_rate = 5, const size_t _bits = 15>
class fastBitSTAModel {
protected:
	T p;
	static const size_t pmax = (1 << _bits) - 1;
public:
	static const size_t shift = _shift;
	static const size_t learn_rate = _learn_rate;
	static const size_t max = 1 << shift;

	void init() {
		p = pmax / 2;
	}

	fastBitSTAModel() {
		init();
	}

	inline void update(T bit) {
		int round = 1 << (_learn_rate - 1);
		p += ((static_cast<int>(bit) << _bits) - static_cast<int>(p) + 00) >> _learn_rate;
	}

	inline void setP(size_t new_p) {
		p = new_p << (_bits - shift);
	}

	inline int getSTP() const {
		return (p >> (_bits - shift)) - 2048;
	}
};

template <typename T, const size_t _shift, const size_t _learn_rate = 5>
class fastBitStretchedModel : public fastBitModel<T, _shift, _learn_rate> {
public:
	static const size_t shift = _shift;
	static const size_t learn_rate = _learn_rate;
	static const size_t max = 1 << shift;

	inline size_t getP() const {
		return getP() - (1 << (shift - 1));
	}
};

#pragma pack(pop)

// Semistationary model.
template <typename T>
class fastCountModel {
	T n[2];
public:
	inline size_t getN(size_t i) const {
		return n[i];
	}

	inline size_t getTotal() const {
		return n[0] + n[1];
	}

	fastCountModel() {
		init();
	}

	void init() {
		n[0] = n[1] = 0;
	}

	void update(size_t bit) {
		n[bit] += n[bit] < 0xFF;
		n[1 ^ bit] = n[1 ^ bit] / 2 + (n[1 ^ bit] != 0);
	}

	inline size_t getP() const {
		size_t a = getN(0);
		size_t b = getN(1);
		if (!a && !b) return 1 << 11;
		if (!a) return 0;
		if (!b) return (1 << 12) - 1;
		return (a << 12) / (a + b);
	}
};


template <typename Predictor, const size_t max>
class bitContextModel {
	static const size_t bits = _bitSize<max - 1>::value;
	Predictor pred[max];
public:
	void init() {
		for (auto& mdl : pred) mdl.init();
	}

	// Returns the cost of a symbol.
	template <typename CostTable>
	inline size_t cost(const CostTable& table, size_t sym, size_t limit = max) {
		assert(limit <= max);
		assert(sym < limit);
		size_t ctx = 1, total = 0;
		for (size_t bit = bits - 1; bit != size_t(-1); --bit) {
			if ((sym >> bit | 1) << bit < limit) {
				size_t b = (sym >> bit) & 1;
				total += table.cost(pred[ctx].getP(), b);
				ctx += ctx + b;
			}
		}
		return total;
	}

	template <typename TEnt, typename TStream>
	inline void encode(TEnt& ent, TStream& stream, size_t sym, size_t limit = max) {
		size_t ctx = 1;
		assert(limit <= max);
		assert(sym < limit);
		for (size_t bit = bits - 1; bit != size_t(-1); --bit)
			if ((sym >> bit | 1) << bit < limit) {
				size_t b = (sym >> bit) & 1;
				ent.encode(stream, b, pred[ctx].getP(), Predictor::shift);
				pred[ctx].update(b);
				ctx += ctx + b;
			}
	}

	inline void update(size_t sym, size_t limit = max) {
		size_t ctx = 1;
		assert(limit <= max);
		assert(sym < limit);
		for (size_t bit = bits - 1; bit != size_t(-1); --bit)
			if ((sym >> bit | 1) << bit < limit) {
				size_t b = (sym >> bit) & 1;
				pred[ctx].update(b);
				ctx += ctx + b;
			}
	}

	template <typename TEnt, typename TStream>
	inline size_t decode(TEnt& ent, TStream& stream, size_t limit = max) {
		size_t ctx = 1, sym = 0;
		assert(limit <= max);
		assert(sym < limit);
		for (size_t bit = bits - 1; bit != size_t(-1); --bit) {
			if ((sym >> bit | 1) << bit < limit) {
				size_t b = ent.decode(stream, pred[ctx].getP(), Predictor::shift);
				sym |= b << bit;
				pred[ctx].update(b);
				ctx += ctx + b;
			}
		}
		return sym;
	}
};

#pragma pack(pop)

#endif