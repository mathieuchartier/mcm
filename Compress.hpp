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

#ifndef _COMPRESS_HPP_
#define _COMPRESS_HPP_

#include <cmath>
#include <map>
#include <vector>
#include <iostream>
#include <map>
#include <list>
#include <ctime>
#include <iomanip>
#include <malloc.h>
#include "Util.hpp"

forceinline bool is_power_2(size_t n) {
	return (n & (n - 1)) == 0;
}

forceinline uint bitSize(uint Value) {
	uint Total = 0;
	for (;Value;Value >>= 1, Total++);
	return Total;
}

template <typename T>
void printIndexedArray(const std::string& str, const T& arr) {
	size_t index = 0;
	std::cout << str << std::endl;
	for (const auto& it : arr) {
		if (it) {
			std::cout << index << ":" << it << std::endl;
		}
		index++;
	}
}

template <const uint64 n>
struct _bitSize {static const uint64 value = 1 + _bitSize<n / 2>::value;};

template <>
struct _bitSize<0> {static const uint64 value = 0;};

class ProgressMeter {
	size_t count, prev_size, prev_count;
	clock_t start;
	bool encode;
public:
	ProgressMeter(bool encode = true)
		: count(0)
		, prev_size(0)
		, prev_count(0)
		, encode(encode) {
		start = clock();
	}

	virtual ~ProgressMeter() {

	}

	inline size_t addByte() {
		return ++count;
	}

	void printRatio(size_t comp_size, const std::string& extra) {
		_mm_empty();
		const float cur_ratio = float(double(comp_size - prev_size) / (count - prev_count));
		const float ratio = float(double(comp_size) / count);
		auto time_delta = clock() - start;
		if (!time_delta) ++time_delta;

		const size_t rate = size_t(double(count / KB) / (double(time_delta) / double(CLOCKS_PER_SEC)));
		std::cout
			<< count / KB << "KB " << (encode ? "->" : "<-") << " "
			<< comp_size / KB << "KB " << rate << "KB/s ratio: " << std::setprecision(5) << std::fixed << ratio << extra.c_str() << " cratio: " << cur_ratio << "\t\r";
		prev_size = comp_size;
		prev_count = count;
	}

	inline void addBytePrint(size_t total, const char* extra = "") {
		if (!(addByte() & 0x3FFFF)) {
			printRatio(total, extra);
		}
	}
					
	size_t getCount() const {
		return count;
	}
};

#endif
