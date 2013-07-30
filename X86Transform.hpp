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

#ifndef _X86_TRANSFORM_HPP_
#define _X86_TRANSFORM_HPP_

#include "Transform.hpp"

class X86Transform : public Transform {
	bool inverse;
	size_t base;
public:
	void init() {
		inverse = false;
		base = 0;
	}

	virtual size_t attempt_transform() {
		byte c = lookahead.front();
		if ((c & 0xFE) == 0xE8) {
			output.push(c);
			return 1;
		}
	}
};

#endif
