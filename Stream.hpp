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
#include <cstring>

// TODO: Fix for files over 4 GB.
template <const size_t size>
class BufferedStream {
public:
	static const size_t mask = size - 1;
	typedef BufferedStream SelfType;
	char buffer[size];
	size_t total, eof_pos;
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
	inline std::streamsize getTotal() const {return total;}

	inline bool eof() const {
		if (total > eof_pos)
			return true;
		return total > eof_pos;
	}

	inline bool good() const {
		return !eof();
	}

	int open(const std::string& fileName, std::ios_base::open_mode mode = std::ios_base::in | std::ios_base::binary) {
		close();
		char openModeBuffer[32];
		openModeBuffer[0] = '\0';

		if (mode & std::ios_base::out) {
			strcat(openModeBuffer, "w");
			if (mode & std::ios_base::in) {
				strcat(openModeBuffer, "+");
			}
		} else if (mode & std::ios_base::in) {
			strcat(openModeBuffer, "r");
		}

		if (mode & std::ios_base::binary) {
			strcat(openModeBuffer, "b");
		}
			
		fileHandle = fopen(fileName.c_str(), openModeBuffer);
		return fileHandle != nullptr ? 0 : errno;
	}

	inline void write(size_t ch) {
		assert(ch < 0x100);
		size_t pos = total++;
		buffer[pos & mask] = static_cast<char>(ch);
		if ((total & mask) == 0) {
			flushWhole();
		}
	}
	
	void prefetch() {
		size_t readCount = fread(buffer, 1, size, fileHandle);
		eof_pos = total + readCount;
	}

	inline int read() {
		if (!(total & mask)) {
			prefetch();
		}
		auto pos = total++;
		if (total <= eof_pos)
			return (int)(unsigned char)buffer[pos & mask];
		else
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
