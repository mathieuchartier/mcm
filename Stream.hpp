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

#ifndef _FILE_STREAM_HPP_
#define _FILE_STREAM_HPP_

#include <cassert>
#include <cstdio>
#include <sstream>

#include "Compress.hpp"

// TODO: Fix for files over 4 GB.
template <const size_t size>
class BufferedStream {
public:
	static const uint64_t mask = size - 1;
	typedef BufferedStream SelfType;
	char buffer[size];
	uint64_t total, eof_pos;
private:
	FILE* fileHandle; 

	void flush() {
		if ((total & mask) != 0) {
			fwrite(buffer, 1, static_cast<size_t>(total & mask), fileHandle);
		}
	}

	void flushWhole() {
		assert((total & mask) == 0);
		fwrite(buffer, 1, static_cast<size_t>(size), fileHandle);
	}
public:
	inline uint64_t getTotal() const {return total;}

	inline bool eof() const {
		// return total >= eof_pos;
		if (total >= eof_pos)
			return true;
		else
			return false;
	}

	inline bool good() const {
		return !eof();
	}

	int open(const std::string& fileName, std::ios_base::open_mode mode = std::ios_base::in | std::ios_base::binary) {
		close();
		std::ostringstream oss;
		
		if (mode & std::ios_base::out) {
			oss << "w";
			if (mode & std::ios_base::in) {
				oss << "+";
			}
		} else if (mode & std::ios_base::in) {
			oss << "r";
		}

		if (mode & std::ios_base::binary) {
			oss << "b";
		}
			
		fileHandle = fopen(fileName.c_str(), oss.str().c_str());
		return fileHandle != nullptr ? 0 : errno;
	}

	inline void write(byte ch) {
		uint64_t pos = total++;
		buffer[pos & mask] = ch;
		if ((total & mask) == 0) {
			flushWhole();
		}
	}
	
	void prefetch() {
		uint64_t readCount = fread(buffer, 1, size, fileHandle);
		if (readCount != size) {
			eof_pos = total + readCount;
		}
	}

	inline int read() {
		if (!(total & mask)) {
			prefetch();
		}
		if (LIKELY(total < eof_pos)) {
			return (int)(byte)buffer[total++ & mask];
		} else
			return EOF;
	}

	// Go back to the start of the file.
	void restart() {
		total = 0;
		eof_pos = (size_t)-1;
		rewind(fileHandle);
	}

	void close() {
		if (fileHandle) {
			flush();
			fclose(fileHandle);
			fileHandle = 0;
			total = 0;
		}
	}
		
	void reset() {
		fileHandle = nullptr;
		total = 0;
		eof_pos = (size_t)-1;
	}

	BufferedStream() {
		reset();
	}

	virtual ~BufferedStream() {
		close();
	} 
};

#endif
