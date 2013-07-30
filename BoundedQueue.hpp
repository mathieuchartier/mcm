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

#ifndef _BOUNDED_QUEUE_HPP_
#define _BOUNDED_QUEUE_HPP_

#include "CyclicBuffer.hpp"

template <typename T>
class BoundedQueue : public CyclicBuffer<T> {
	size_t read_pos;
public:
	size_t getReadPos() const {
		return read_pos;
	}

	virtual void restart() {
		CyclicBuffer<T>::restart();
		read_pos = 0;
	}

	forceinline T front() const {
		return (*this)[read_pos];
	}

	forceinline T pop_front() {
		return (*this)[read_pos++];
	}
};

#endif
