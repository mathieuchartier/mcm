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

#include "Util.hpp"
#include <Windows.h>
#include "Memory.hpp"

MemMap::MemMap() : storage(nullptr), size(0) {

}

MemMap::~MemMap() {
	release();
}

void MemMap::resize(size_t bytes) {
	release();
	storage = (void*)VirtualAlloc(nullptr, bytes, MEM_COMMIT, PAGE_READWRITE);
}

void MemMap::release() {
	if (storage != nullptr) {
		VirtualFree((LPVOID)storage, size, MEM_RELEASE);
	}
}

void MemMap::zero() {
	memset(storage, 0, size);
	storage = (void*)VirtualAlloc(storage, size, MEM_RESET, PAGE_READWRITE);
}
