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

