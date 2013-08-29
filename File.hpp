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
#include <stdio.h>
#include <sstream>

#include "Compressor.hpp"
#include "Stream.hpp"

#ifndef WIN32
extern int __cdecl _fseeki64(FILE *, int64_t, int);
extern int64_t __cdecl _ftelli64(FILE *);
#endif

class File {
	FILE* handle;
public:
	File() : handle(nullptr) {

	}

	size_t write(byte* bytes, size_t count) {
		return fwrite(bytes, 1,count, handle);
	}

	int close() {
		int ret = 0;
		if (handle != nullptr) {
			ret = fclose(handle);
			handle = nullptr;
		}
		return ret;
	}

	void rewind() {
		::rewind(handle);
	}

	bool isOpen() const {
		return handle != nullptr;
	}

	// Return 0 if successful, errno otherwise.
	int open(const std::string& fileName, std::ios_base::open_mode mode = std::ios_base::in | std::ios_base::binary) {
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
			
		handle = fopen(fileName.c_str(), oss.str().c_str());
		return handle != nullptr ? 0 : errno;
	}
	
	size_t read(byte* buffer, size_t bytes) {
		return fread(buffer, 1, bytes, handle);
	}

	int64_t tell() const {
		return _ftelli64(handle);
	}

	int seek(int64_t offset, int origin = SEEK_SET) {
		return _fseeki64(handle, offset, origin);
	}


	FILE* getHandle() {
		return handle;
	}
};

class FileStream : WriteStream {

};

template <const size_t size>
class BufferedFileStream {
public:
	static const uint64_t mask = size - 1;
	byte buffer[size];
	uint64_t total, eof_pos;
	File file;
private:

	void flush() {
		if ((total & mask) != 0) {
			file.write(buffer, static_cast<size_t>(total & mask));
		}
	}

	void flushWhole() {
		assert((total & mask) == 0);
		file.write(buffer, static_cast<size_t>(size));
	}
public:
	inline uint64_t getTotal() const {
		return total;
	}

	inline bool eof() const {
		return total >= eof_pos;
	}

	inline bool good() const {
		return !eof();
	}

	int open(const std::string& fileName, std::ios_base::open_mode mode = std::ios_base::in | std::ios_base::binary) {
		close();
		return file.open(fileName, mode);
	}

	inline void write(byte ch) {
		uint64_t pos = total++;
		buffer[pos & mask] = ch;
		if ((total & mask) == 0) {
			flushWhole();
		}
	}
	
	void prefetch() {
		uint64_t readCount = file.read(buffer, size);
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
		} else {
			return EOF;
		}
	}

	// Go back to the start of the file.
	void restart() {
		total = 0;
		eof_pos = (size_t)-1;
		file.rewind();
	}

	void close() {
		if (file.isOpen()) {
			flush();
			file.close();
			total = 0;
		}
	}
		
	void reset() {
		file.close();
		total = 0;
		eof_pos = (size_t)-1;
	}

	BufferedFileStream() {
		reset();
	}

	virtual ~BufferedFileStream() {
		close();
	} 
};

#endif
