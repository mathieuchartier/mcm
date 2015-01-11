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

#ifndef _SLIDING_WINDOW_HPP_
#define _SLIDING_WINDOW_HPP_
#pragma once

#include <cassert>
#include "Util.hpp"

template <typename T>
class CyclicBuffer {
	size_t pos;
protected:
	size_t mask, count, alloc_size;
public:
	T *storage, *data;

	forceinline size_t getPos() const {return pos;}
	forceinline size_t getMask() const {return mask;}
	forceinline T* getData() { return data; }

	inline size_t prev(size_t pos, size_t count) const {
		// Relies on integer underflow behaviour -> infinity. Works since pow 2 size.
		return (pos - count) & mask;
	}

	inline size_t next(size_t pos, size_t count) const {
		return (pos + count) & mask;
	}

	// Maximum size.
	forceinline size_t getSize() const {
		return mask + 1;
	}

	// Current nubmer of items inside of the buffer.
	forceinline size_t getCount() const {
		return count;
	}

	CyclicBuffer() : storage(NULL), mask(0) {
		
	}

	virtual ~CyclicBuffer() {
		release();
	}

	virtual void restart() {
		pos = 0;
		count = 0;
	}

	forceinline void push(T val) {
		data[pos++ & mask] = val;
		++count;
	}

	forceinline T& operator [] (size_t offset) {
		return data[offset & mask];
	}

	forceinline T operator [] (size_t offset) const {
		return data[offset & mask];
	}

	forceinline T operator () (size_t offset) const {
		return data[offset];
	}

	virtual void release() {
		pos = alloc_size = 0;
		mask = static_cast<size_t>(-1);
		delete [] storage;
		storage = data = nullptr;
	}

	void fill(T d) {
		std::fill(storage, storage + getSize(), d);
	}

	// Can be used for LZ77.
	void copyStartToEndOfBuffer(size_t count) {
		size_t size = getSize();
		for (size_t i = 0;i < count;++i) {
			data[size + i] = data[i];
		}
	}

	// Can be used for LZ77.
	void copyEndToStartOfBuffer(size_t count) {
		size_t size = getSize();
		for (size_t i = 0;i < count;++i) {
			storage[i] = storage[i + size];
		}
	}

	void resize(size_t newSize, size_t padding = sizeof(size_t)) {
		// Ensure power of 2.
		assert((newSize & newSize - 1) == 0);
		delete [] storage;
		mask = newSize - 1;
		alloc_size = newSize + padding * 2;
		storage = new T[alloc_size]();
		data = storage + padding;
		restart();
	}
};

#endif
