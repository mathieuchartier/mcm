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
#include "Compress.hpp"

template <typename T>
class SlidingWindow {
	size_t pos, size, total;
public:
	std::vector<T> data;

	inline size_t getPos() const {return pos;}
	inline size_t getTotal() const {return total;}
	inline size_t getSize() const {return size;}

	inline size_t prev(size_t pos, size_t count) const {
		assert(count <= size);
		return (pos >= count ? pos : pos + size) - count;
	}

	inline size_t next(size_t pos, size_t count) const {
		pos += count;
		return pos >= size ? pos - size : pos;
	}

	SlidingWindow()	{
		restart();
	}

	void restart() {
		pos = total = 0;
	}

	inline void push(const T& copy) {
		total++;
		data[pos] = copy;
		if (++pos >= size) pos -= size;
	}

	inline T operator [] (size_t offset) const {
		return data[offset % size];
	}

	inline T operator () (size_t offset) const {
		return data[offset];
	}

	inline void clear() {
		pos = size = total = 0;
		data.clear();
	}

	void resize(size_t newSize) {
		clear();
		size = newSize;
		data.resize(size);
		std::fill(data.begin(), data.end(), 0);
	}
};

template <typename T>
class SlidingWindow2 {
	size_t pos, mask;
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

	forceinline size_t getSize() const {
		return mask + 1;
	}

	SlidingWindow2() : storage(NULL), mask(0) {
		
	}

	virtual ~SlidingWindow2() {
		clear();
	}

	void restart() {
		pos = 0;
	}

	forceinline void push(T val) {
		data[pos++ & mask] = val;
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

	inline void clear() {
		pos = 0;
		mask = static_cast<size_t>(-1);
		delete [] storage;
		storage = data = nullptr;
	}

	void fill(T d) {
		std::fill(data, data + getSize(), d);
	}

	void copyStartToEndOfBuffer(size_t count) {
		size_t size = getSize();
		for (size_t i = 0;i < count;++i) {
			data[size + i] = data[i];
		}
	}

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
		const auto alloc_size = newSize + padding * 2;
		storage = new T[alloc_size];
		std::fill(storage, storage + alloc_size, 0);
		data = storage + padding;
		fill(0);
		restart();
	}
};

#endif
