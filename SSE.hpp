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

#include "Model.hpp"

class SSE {
	static const size_t kProbBits = 12;
	static const size_t kStemBits = 5;
	static const size_t kStems = (1 << kStemBits) + 1;
	static const size_t kMaxP = 1 << kProbBits;
	static const size_t kProbMask = (1 << (kProbBits - kStemBits)) - 1;
	static const size_t kRound = 1 << (kProbBits - kStemBits - 1);
	typedef fastBitModel<int, kProbBits, 9, 30> HPStationaryModel;
	size_t pw;
public:
	std::vector<HPStationaryModel> models;

	template <typename Table>
	void init(size_t num_ctx, const Table* table) {
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
		for (; ctx < num_ctx; ++ctx) {
			std::copy(&models[0], &models[kStems], &models[ctx * kStems]);
		}
	}

	int p(size_t p, size_t ctx) {
		dcheck(p < kMaxP);
		const size_t idx = p >> (kProbBits - kStemBits);
		dcheck(idx < kStems);
		size_t s1 = ctx * kStems + idx;
		size_t mask = p & kProbMask;
		pw = s1 + (mask >= kProbMask / 2);
		return (models[s1].getP() * (1 + kProbMask - mask) + models[s1 + 1].getP() * mask) >> (kProbBits - kStemBits);
	}

	void update(size_t bit) {
		models[pw].update(bit);
	}
};

#endif
