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

#pragma once

#include "Stream.hpp"
#include <vector>

class Store {
public:
	template <typename TOut, typename TIn>
	size_t Compress(TOut& sout, TIn& sin) {
		size_t count = 0;
		for (uint c;(c = sin.read()) != EOF;sout.write(c))
			++count;
		return count;
	}

	template <typename TOut, typename TIn>
	void DeCompress(TOut& sout, TIn& sin) {
		for (uint c;(c = sin.read()) != EOF;sout.write(c));
	}
};

class FreqCount {
	std::vector<size_t> freq;
public:
	void init() {
		freq.resize(256);
		for (auto& f : freq) f = 0;
	}

	template <typename TOut, typename TIn>
	size_t Compress(TOut& sout, TIn& sin) {
		size_t count = 0;
		init();
		for (;;) {
			auto c = sin.read();
			if (c == EOF) break;
			++freq[(size_t)c];
		}
		//printIndexedArray("frequencies", freq);
		size_t index = 0;
		std::cout << "Frequencies" << std::endl;
		size_t min_freq = 9999999999, min_index = 0;
		for (const auto& it : freq) {
			if (it) {
				std::cout << std::hex << "0x" << index << std::dec << "=" << index << "(" << (char)index << ") :" << it << std::endl;
			}
			if (it < min_freq) {
				min_freq = it;
				min_index = index;
			}
			index++;
		}
		std::cout << "Minimum " << min_index << " = " << min_freq << std::endl;
		std::cout << "zeroes: ";
		for (size_t i = 0;i < freq.size();++i) {
			if (!freq[i]) std::cout << i << " ";
		}
		std::cout << std::endl;
		return sout.getTotal();
	}

	template <typename TOut, typename TIn>
	void DeCompress(TOut& sout, TIn& sin) {
		return;
	}
};
