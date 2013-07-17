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

#include <emmintrin.h>
#include <ostream>
#include <string>

#ifdef WIN32
#define forceinline __forceinline
#else
#define forceinline inline __attribute__((always_inline))
#endif

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

#define LIKELY(x) x
#define UNLIKELY(x) x

typedef __int64 int64;
typedef unsigned __int64 uint64;
typedef unsigned short ushort;
typedef size_t hash_t;

static const size_t KB = 1024;
static const size_t MB = KB * KB;
static const size_t GB = KB * MB;
static const size_t kCacheLineSize = 64; // Sandy bridge.
static const size_t kPageSize = 4 * KB;

enum DataProfile {
	kText,
	kExe,
	kBinary,
	kWave,
	kEOF,
	kProfileCount,
};

inline std::ostream& operator << (std::ostream& sout, const DataProfile& pattern) {
	switch (pattern) {
	case kText: return sout << "text";
	case kBinary: return sout << "binary";
	case kExe: return sout << "exe";
	case kWave: return sout << "wave";
	}
	return sout << "unknown!";
}

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

template <const size_t A, const size_t B, const size_t C, const size_t D>
struct shuffle {
	enum {
		value = (D << 6) | (C << 4) | (B << 2) | A,
	};
};

#endif
