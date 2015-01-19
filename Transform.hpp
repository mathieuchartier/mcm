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

#ifndef _TRANSFORM_HPP_
#define _TRANSFORM_HPP_

#include <deque>
#include <thread>

#include "BoundedQueue.hpp"
#include "CyclicBuffer.hpp"
#include "Util.hpp"

// Transform interface.
class Transform {
protected:
	// Seen buffer.
	CyclicBuffer<byte> seen;
	// Look-ahead queue.
	BoundedQueue<byte> lookahead, output;
	// Output queue.
	byte *buffer;
	uint32_t buffer_mask;
	uint32_t transform_count, untransform_count;
	uint32_t transform_pos, untransform_pos;

	// Returns how many bytes were transformed.
	virtual uint32_t attempt_transform() = 0;
public:
	void release() {
		delete [] buffer;
		buffer = nullptr;
	}

	// Size needs to be a power of 2.
	void init(uint32_t size = 512 * KB) {
		release();
		buffer_mask = size - 1;
		buffer = new byte[buffer_mask + 1];
		transform_count = untransform_count = 0;
		transform_pos = untransform_pos = 0;
	}

	void push(byte c) {
		++untransform_count;
		buffer[untransform_pos++ & buffer_mask] = c;
	}

	// Start of the untransformed region.
	byte& at(uint32_t index) {
		return buffer[(untransform_pos - untransform_count + index) & buffer_mask];
	}

	forceinline byte at(uint32_t index) const {
		return buffer[(untransform_pos - untransform_count + index) & buffer_mask];
	}

	void transform() {
		auto count = attempt_transform();
		transform_count += count;
		untransform_count -= count;
	}

	bool empty() const {
		return 0;
	}

	int read() {
		if (!transform_count) {
			return EOF;
		}
		--transform_count;
		return buffer[transform_pos++ & buffer_mask];
	}

	Transform() : buffer(nullptr), buffer_mask(0) {

	}
};


#endif
