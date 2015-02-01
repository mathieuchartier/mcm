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

#include <cstdlib>
#include <cstring>

#include "Memory.hpp"
#include "Util.hpp"

#define USE_MALLOC !defined(WIN32)

#ifdef WIN32
#include <Windows.h>
#else
// TODO: mmap
#endif

MemMap::MemMap() : storage(nullptr), size(0) {

}

MemMap::~MemMap() {
	release();
}

void MemMap::resize(size_t bytes) {
	if (bytes == size) {
		std::fill(reinterpret_cast<uint8_t*>(storage), reinterpret_cast<uint8_t*>(storage) + size, 0);
		return;
	}
	release();
	size = bytes;
#if WIN32
	storage = (void*)VirtualAlloc(nullptr, size, MEM_COMMIT, PAGE_READWRITE);
#elif USE_MALLOC
	storage = std::calloc(1, bytes);
#else
#error UNIMPLEMENTED
#endif
}

void MemMap::release() {
	if (storage != nullptr) {
#if WIN32
		VirtualFree((LPVOID)storage, size, MEM_RELEASE);
#elif USE_MALLOC
		std::free(storage);
#else

#endif
		storage = nullptr;
	}
}

void MemMap::zero() {
#ifdef WIN32
	storage = (void*)VirtualAlloc(storage, size, MEM_RESET, PAGE_READWRITE);
#elif USE_MALLOC
	std::memset(storage, 0, size);
#else
	madvise(storage, size, MADV_DONTNEED);
#endif
}
