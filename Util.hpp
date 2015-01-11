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

#ifndef _UTIL_HPP_
#define _UTIL_HPP_

#include <cassert>
#include <ctime>
#include <emmintrin.h>
#include <iostream>
#include <mmintrin.h>
#include <mutex>
#include <ostream>
#include <stdint.h>
#include <sstream>
#include <string>
#include <vector>

#ifdef WIN32
#define forceinline __forceinline
#else
#define forceinline inline __attribute__((always_inline))
#endif
#define ALWAYS_INLINE forceinline

#define no_alias __restrict

#ifndef BYTE_DEFINED
#define BYTE_DEFINED
typedef unsigned char byte;
#endif

#ifndef UINT_DEFINED
#define UINT_DEFINED
typedef unsigned int uint;
#endif

#ifndef WORD_DEFINED
#define WORD_DEFINED
typedef unsigned short word;
#endif

// TODO: Implement these.
#define LIKELY(x) x
#define UNLIKELY(x) x

#ifdef _DEBUG
static const bool kIsDebugBuild = true;
#else
static const bool kIsDebugBuild = false;
#endif

#ifdef _MSC_VER
#define ASSUME(x) __assume(x)
#else
#define ASSUME(x)
#endif

typedef size_t hash_t;

static const uint64_t KB = 1024;
static const uint64_t MB = KB * KB;
static const uint64_t GB = KB * MB;
static const size_t kCacheLineSize = 64; // Sandy bridge.
static const size_t kPageSize = 4 * KB;
static const size_t kBitsPerByte = 8;

enum DataProfile {
	kText,
	kExe,
	kBinary,
	kWave,
	kEOF,
	kProfileCount,
};

std::ostream& operator << (std::ostream& sout, const DataProfile& pattern);

forceinline void prefetch(const void* ptr) {
#ifdef WIN32
	_mm_prefetch((char*)ptr, _MM_HINT_T0);
#else
	__builtin_prefetch(ptr);
#endif
}

forceinline static bool is_upper(char c) {
	return c >= 'A' && c <= 'Z';
}

forceinline static bool is_lower(char c) {
	return c >= 'a' && c <= 'z';
}

forceinline static char lower_case(char c) {
	if (is_upper(c)) c = c - 'A' + 'a';
	return c;
}

// Trust in the compiler
forceinline size_t rotate_left(size_t h, size_t bits) {
	return (h << bits) | (h >> (sizeof(h) * 8 - bits));
}

forceinline size_t rotate_right(size_t h, size_t bits) {
	return (h << (sizeof(h) * 8 - bits)) | (h >> bits);
}

#define check(c) while (!(c)) { std::cerr << "check failed " << #c << std::endl; *reinterpret_cast<int*>(1234) = 4321;}
#define dcheck(c) assert(c)

template <const size_t A, const size_t B, const size_t C, const size_t D>
struct shuffle {
	enum {
		value = (D << 6) | (C << 4) | (B << 2) | A,
	};
};

forceinline bool isPowerOf2(size_t n) {
	return (n & (n - 1)) == 0;
}

forceinline uint bitSize(uint Value) {
	uint Total = 0;
	for (;Value;Value >>= 1, Total++);
	return Total;
}

template <typename T>
void printIndexedArray(const std::string& str, const T& arr) {
	size_t index = 0;
	std::cout << str << std::endl;
	for (const auto& it : arr) {
		if (it) {
			std::cout << index << ":" << it << std::endl;
		}
		index++;
	}
}

template <const uint64_t n>
struct _bitSize {static const uint64_t value = 1 + _bitSize<n / 2>::value;};

template <>
struct _bitSize<0> {static const uint64_t value = 0;};

inline void fatalError(const std::string& message) {
	std::cerr << "Fatal error: " << message << std::endl;
	*reinterpret_cast<size_t*>(1234) = 0;
}

inline void unimplementedError(const char* function) {
	std::ostringstream oss;
	oss << "Calling implemented function " << function;
	fatalError(oss.str());
}

inline size_t rand32() {
	return rand() ^ (rand() << 16);
}

forceinline int fastAbs(int n) {
	int mask = n >> 31;
	return (n ^ mask) - mask;
}

bool fileExists(const char* name);

class Closure {
public:
	virtual void run() = 0;
};

template <typename Container>
void deleteValues(Container& container) {
	for (auto* p : container) {
		delete p;
	}
	container.clear();
}

class ScopedLock {
public:
	ScopedLock(std::mutex& mutex) : mutex_(mutex) {
		mutex_.lock();
	}

	~ScopedLock() {
		mutex_.unlock();
	}

private:
	std::mutex& mutex_;
};

forceinline void copy16bytes(byte* no_alias out, const byte* no_alias in, const byte* limit) {
	_mm_storeu_ps(reinterpret_cast<float*>(out), _mm_loadu_ps(reinterpret_cast<const float*>(in)));
}

forceinline static void memcpy16(void* dest, const void* src, size_t len) {
	byte* no_alias dest_ptr = reinterpret_cast<byte* no_alias>(dest);
	const byte* no_alias src_ptr = reinterpret_cast<const byte* no_alias>(src);
	const byte* no_alias limit = dest_ptr + len;
	*dest_ptr++ = *src_ptr++;
	if (len >= sizeof(__m128)) {
		const byte* no_alias limit2 = limit - sizeof(__m128);
		do {
			copy16bytes(dest_ptr, src_ptr, limit);
			src_ptr += sizeof(__m128);
			dest_ptr += sizeof(__m128);
		} while (dest_ptr < limit2);
	}
	while (dest_ptr < limit) {
		*dest_ptr++ = *src_ptr++;
	}
}

template<typename CopyUnit>
forceinline void fastcopy(byte* no_alias out, const byte* no_alias in, const byte* limit) {
	do {
		*reinterpret_cast<CopyUnit* no_alias>(out) = *reinterpret_cast<const CopyUnit* no_alias>(in);
		out += sizeof(CopyUnit);
		in += sizeof(CopyUnit);
	} while (in < limit);
}

forceinline void memcpy16unsafe(byte* no_alias out, const byte* no_alias in, const byte* limit) {
	do {
		copy16bytes(out, in, limit);
		out += 16;
		in += 16;
	} while (out < limit);
}

template<size_t kMaxSize>
class FixedSizeByteBuffer {
public:
	size_t getMaxSize() const {
		return kMaxSize;
	}

protected:
	byte buffer_[kMaxSize];
};

template<size_t kMaxSize>
class FixedSizeStreamBuffer : public FixedSizeByteBuffer<kMaxSize> {
public:
	FixedSizeStreamBuffer() : size_(0), pos_(0) {
	}

	size_t getMaxWrite() const {
		return kMaxSize - size_;
	}

	size_t getMaxRead() const {
		return size_ - pos_;
	}

	size_t write(byte* buf, size_t count) {
		assert(count < getMaxWrite());
		memcpy(&buffer_[size_], buf, count);
		size_ += count;
	}

	void read(byte* buf, size_t count) {
		assert(count < getMaxRead());
		memcpy(buf, &buffer_[pos_], count);
		pos_ += count;
	}

	// Delete the already read chars up to pos.
	void rebase() {
		memmove(&buffer_[0], &buffer_[pos_], getMaxRead());
		size_ -= pos_;
		pos_ = 0;
	}

private:
	// Current size.
	size_t size_;
	// Current position.
	size_t pos_;
};

template <class T, size_t kSize>
class StaticArray {
public:
	StaticArray() {
	}
	ALWAYS_INLINE const T& operator[](size_t i) const {
		return data_[i];
	}
	ALWAYS_INLINE T& operator[](size_t i) {
		return data_[i];
	}
	ALWAYS_INLINE size_t size() const {
		return kSize;
	}

private:
	T data_[kSize];
};

template <class T, size_t kCapacity>
class StaticBuffer {
public:
	StaticBuffer() : pos_(0), size_(0) {
	}
	ALWAYS_INLINE const T& operator[](size_t i) const {
		return data_[i];
	}
	ALWAYS_INLINE T& operator[](size_t i) {
		return data_[i];
	}
	ALWAYS_INLINE size_t pos() const {
		return pos_;
	}
	ALWAYS_INLINE size_t size() const {
		return size_;
	}
	ALWAYS_INLINE size_t capacity() const {
		return kCapacity;
	}
	ALWAYS_INLINE size_t reamainCapacity() const {
		return capacity() - size();
	}
	ALWAYS_INLINE T get() {
		(pos_ < size_);
		return data_[pos_++];
	}
	ALWAYS_INLINE void read(T* ptr, size_t len) {
		dcheck(pos_ + len <= size_);
		std::copy(&data_[pos_], &data_[pos_ + len], &ptr[0]);
		pos_ += len;
	}
	ALWAYS_INLINE void put(T c) {
		dcheck(pos_ < size_);
		data_[pos_++] = c;
	}
	ALWAYS_INLINE void write(const T* ptr, size_t len) {
		dcheck(pos_ + len <= size_);
		std::copy(&ptr[0], &ptr[len], &data_[pos_]);
		pos_ += len;
	}
	ALWAYS_INLINE size_t remain() const {
		return size_ - pos_;
	}
	void erase(size_t chars) {
		dcheck(chars <= pos());
		std::move(&data_[chars], &data_[size()], &data_[0]);
		pos_ -= std::min(pos_, chars);
		size_ -= std::min(size_, chars);
	}
	void addPos(size_t n) {
		pos_ += n;
		dcheck(pos_ <= size());
	}
	void addSize(size_t n) {
		size_ += n;
		dcheck(size_ <= capacity());
	}
	T* begin() {
		return &operator[](0);
	}
	T* end() {
		return &operator[](size_);
	}
	T* limit() {
		return &operator[](capacity());
	}

private:
	size_t pos_;
	size_t size_;
	T data_[kCapacity];
};

std::string prettySize(uint64_t size);
double clockToSeconds(clock_t c);
std::string errstr(int err);
std::vector<byte> randomArray(size_t size);
uint64_t computeRate(uint64_t size, uint64_t delta_time);
std::vector<byte> loadFile(const std::string& name, size_t max_size = 0xFFFFFFF);

#endif
