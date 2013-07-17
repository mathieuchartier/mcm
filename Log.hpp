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

#ifndef _LOG_HPP_
#define _LOG_HPP_

#include <cmath>

// squash = ln(p/(1-p))
// stretch = squash^-1 = 1/(1+e^-x)

inline double squash(double p)
{
	if (p < 0.0001) return -999.0;
	if (p > 0.9999) return 999.0;
	return (double) std::log((double) p / (1.0 - (double) p));
}

inline double stretch(double p)
{
	return 1.0 / double(1.0 + exp((double) -p));
}

inline int roundint(double p)
{
	return int(p + 0.5);
}

// Squash - stretch table
template <typename T, int denom, int minInt, int maxInt, int FP>
struct ss_table {
	static const int total = maxInt - minInt;
	T stretchTable[denom], squashTable[total], *squashPtr;
public:
	// probability = p / Denom
	void build() {
		size_t limit = maxInt + 1;

		for (int x = 0; x < total; x++) {
			squashTable[x] = limit;
		}

		for (int i = 0;i < denom;i++) {
			const double p = (double) i / (double) denom;
			double sp = squash(p);
			int cp = roundint(sp * double(1 << FP));
			if (cp < minInt) cp = minInt;
			if (cp >= maxInt) cp = maxInt - 1;
			stretchTable[i] = cp;
			squashTable[cp - minInt] = i ? i : 1;
		}

		for (int x = 0; x < total; x++) {
			if (squashTable[x] == limit) {
				double f = double(x + minInt) / double(1 << FP);
				double s = stretch(f);
				int p = roundint(s * double(denom));
				if (p < 0) p = 0;
				if (p >= denom) p = denom - 1;
				squashTable[x] = p;
			}
		}

		squashPtr = &squashTable[0 - minInt];
	}

	const T* getStretchPtr() const {
		return stretchTable;
	}

	// 0 <= p < denom
	forceinline int st(size_t p) const {
		return stretchTable[p];
	}

	// minInt <= p < maxInt
	inline size_t sq(int p) const {
		if (p <= minInt) {
			return 1;
		}
		if (p >= maxInt) {
			return denom - 1;
		}
		return squashPtr[p];
	}
};

#endif
