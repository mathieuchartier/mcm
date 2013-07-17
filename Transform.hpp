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

#include "Compress.hpp"
#include "Util.hpp"

// Not finished.
class Transform {
public:
	enum Type {
		// Useless, adds one and subtracts one.
		kTTAdd1,
		// Single byte delta.
		kTTDelta8,
		// Multi byte delta.
		kTTDelta16,
		// EXE
		kTTExe,
		// Transform none.
		kTTNone,
		// Alphabet reorder??
	};
protected:
	Type transform;

	// Number of bytes in the buffer.
	size_t buffer_count;

	// Number of transformed bytes in the buffer.
	size_t transformed_count;

	// Smal fifo queue
	SlidingWindow2<byte> buffer;
public:
	Transform() {

	}

	forceinline size_t readBytes(size_t pos, size_t bytes = 4, bool big_endian = true) {
		size_t w = 0;
		if (pos + bytes >= size()) {
			// Past the end of buffer :(
			if(big_endian) {
				for (size_t i = 0; i < bytes; ++i) {
					w = (w << 8) | at(pos + i);
				}
			} else {
				for (size_t i = bytes; i; --i) {
					w = (w << 8) | at(pos + i - 1);
				}
			}
		}
		return w;
	}

	void init(size_t size = 64 * KB) {
		buffer.resize(size);
		buffer.fill(0);
		buffer_count = 0;
		transformed_count = 0;
	}

	forceinline size_t size() const {
		return buffer_count;
	}

	forceinline bool empty() const {
		return !buffer_count;
	}

	forceinline bool full() const {
		return buffer_count >= buffer.getSize();
	}

	void setTransform() {
		transform = kTTNone;
	}

	// Streaming mode??
	void setTransform(Type new_transform) {
		transform = new_transform;
	}

	void push(size_t c) {
		++buffer_count;
		buffer.push(c);
	}

	int popTransformed() {
		if (transformed_count) {
			return pop();
		}
		return EOF;
	}

	forceinline byte at(size_t i) const {
		assert(i < buffer_count);
		return buffer[buffer.getPos() - buffer_count + i];
	}

	forceinline byte& at(size_t i) {
		assert(i < buffer_count);
		return buffer[buffer.getPos() - buffer_count + i];
	}

	int pop() {
		if (!transformed_count) {
			apply_transform(false);
		}
		if (buffer_count == 0) {
			return EOF;
		}
		size_t c = buffer[buffer.getPos() - buffer_count];
		--buffer_count;
		return c;
	}
	
	void apply_transform(bool reverse) {
		assert(!transformed_count);
		switch (transform) {
		case kTTAdd1:
			at(0) += (reverse ? -1 : 1);
			transformed_count++;
			break;
		case kTTExe:
			// E8E9

			break;
		}
	}
};

#endif
