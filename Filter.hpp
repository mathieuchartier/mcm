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

#ifndef _TEXT_FILTER_HPP_
#define _TEXT_FILTER_HPP_

#include "Transform.hpp"

template <typename Stream>
class IdentityFilter {
	Stream& stream;
public:
	
	IdentityFilter(Stream& stream) : stream(stream) {}

	inline int read() {
		return stream.read();
	}

	inline void write(size_t c) {
		stream.write(c);
	}

	inline bool eof() const {
		return stream.eof();
	}

	inline void restart() {
		stream.restart();
	}

	inline size_t getTotal() const {
		return stream.getTotal();
	}
};

struct IdentityFilterFactory {
	template <typename Stream>
	static IdentityFilter<Stream> Make(Stream& stream) {
		return IdentityFilter<Stream>(stream);
	}
};

template <typename Compressor, typename Filter>
class FilterCompressor : public Compressor {
public:
	template <typename TOut, typename TIn>
	bool DeCompress(TOut& sout, TIn& sin) {
		auto fout = Filter::Make(sout);
		return Compressor::DeCompress(fout, sin);
	}

	template <typename TOut, typename TIn>
	size_t Compress(TOut& sout, TIn& sin) {
		auto fin = Filter::Make(sin);
		return Compressor::Compress(sout, fin);
	}
};

#endif
