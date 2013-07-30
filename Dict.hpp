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

#ifndef _DICT_HPP_
#define _DICT_HPP_

#include <vector>

class Dict {
	class Entry {
	public:
		size_t freq;
	};

	// Hash table.
	class Buckets {
	public:

	};

	// Entries.
	std::vector<Entry> entires;

	std::vector<HashTable> dict;
public:
	template <typename TIn>
	void build(TIn& sin) {
		for (;;) {
			int c = sin.read();
			if (c == EOF) break;
			// Extend current match.

			// ??
		}
	}

	void buildCodes() {
		for (;;) {

		}
	}
};

// Simple compressor that uses the dictionary algorithm.
class DictCompressor {
	Dict dict;
public:
	template <typename TOut, typename TIn>
	size_t Compress(TOut& sout, TIn& sin) {
		dict.build(in);
		sin.restart();
	}
};

#endif
