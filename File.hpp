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
#include <fstream>
#include <mutex>
#include <stdio.h>
#include <sstream>

#include "Compressor.hpp"
#include "Stream.hpp"

#ifndef WIN32
extern int __cdecl _fseeki64(FILE *, int64_t, int);
extern int64_t __cdecl _ftelli64(FILE *);
#endif

class FileInfo {
public:
	FileInfo() : attributes(0) {
	}
private:
	uint32_t attributes;
	std::string name;
};

class FilePath {
public:
	FilePath(const std::string& name = "") : name(name) {
	}

	std::string getName() const {
		return name;
	}

	void setName(const std::string& new_name) {
		name = new_name;
	}

	bool isEmpty() const {
		return name.empty();
	}

private:
	std::string name;
};

inline std::vector<FileInfo> EnumerateFiles(const std::string& dir) {
	std::vector<FileInfo> ret;
	return ret;
}

class File {
protected:
	std::mutex lock;
	uint64_t offset; // Current offset in the file.
	FILE* handle;
public:
	File()
		: handle(nullptr),
		  offset(0) {
	}

	std::mutex& getLock() {
		return lock;
	}

	// Not thread safe.
	forceinline size_t write(const void* bytes, size_t count) {
		size_t ret = fwrite(bytes, 1, count, handle);
		offset += ret;
		return ret;
	}

	// Not thread safe.
	forceinline int put(byte c) {
		++offset;
		return fputc(static_cast<int>(c), handle);
	}

	int close() {
		int ret = 0;
		if (handle != nullptr) {
			ret = fclose(handle);
			handle = nullptr;
		}
		offset = 0; // Mark
		return ret;
	}

	void rewind() {
		offset = 0;
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
		if (handle != nullptr) {
			offset = 0;
			return 0;
		}
		return errno;
	}

	// Not thread safe.
	forceinline size_t read(void* buffer, size_t bytes) {
		size_t ret = fread(buffer, 1, bytes, handle);
		offset += ret;
		return ret;
	}

	// Not thread safe.
	forceinline int get() {
		++offset;
		return fgetc(handle);
	}

	forceinline int64_t tell() const {
		return offset;
	}

	forceinline int seek(int64_t pos, int origin = SEEK_SET) {
		if (origin == SEEK_SET && pos == offset) {
			return 0; // No need to do anything.
		}
		int ret = _fseeki64(handle, pos, origin);
		if (!ret) { // 0 = success
			if (origin != SEEK_SET) {
				// Don't necessarily know where the end is.
				offset = _ftelli64(handle);
			} else {
				offset = pos;
			}
		}
		return ret;
	}

	forceinline FILE* getHandle() {
		return handle;
	}

	// Atomic read.
	// TODO: fread already acquires a lock.
	forceinline size_t aread(uint64_t pos, void* buffer, size_t bytes) {
		lock.lock();
		seek(pos); // Only seeks if necessary.
		size_t ret = read(buffer, bytes);
		lock.unlock();
		return ret;
	}

	// Atomic write.
	// TODO: fwrite already acquires a lock.
	forceinline size_t awrite(uint64_t pos, const void* buffer, size_t bytes) {
		lock.lock();
		seek(pos); // Only seeks if necessary.
		size_t ret = write(buffer, bytes);
		lock.unlock();
		return ret;
	}

	// Atomic get (slow).
	forceinline int aget(uint64_t pos) {
		lock.lock();
		seek(pos); // Only seeks if necessary.
		int ret = get();
		lock.unlock();
		return ret;
	}

	// Atomic write (slow).
	forceinline int aput(uint64_t pos, byte c) {
		lock.lock();
		seek(pos); // Only seeks if necessary.
		int ret = put(c);
		lock.unlock();
		return ret;
	}

	forceinline uint64_t length() {
		lock.lock();
		seek(0, SEEK_END);
		uint64_t length = tell();
		seek(0, SEEK_SET);
		lock.unlock();
		return length;
	}
};

// Used to mirror data within a file.
class FileMirror {
	bool is_dirty_;
	uint64_t offset_;
public:
	// Update the file if it is dirty.
	void update() {
		if (isDirty()) {
			write();
			setDirty(true);
		}
	}
	// Read the corresponding data from the file.
	virtual void write() {
	}
	// Read the corresponding data from the file.
	virtual void read() {
	}
	void setDirty(bool dirty) {
		is_dirty_ = dirty;
	}
	bool isDirty() const {
		return is_dirty_;
	}
	void setOffset(uint64_t offset) {
		offset_ = offset;
	}
	uint64_t getOffset() const {
		return offset_;
	}
};

class OffsetFileWriteStream : public WriteStream {
	File* file;
	uint64_t offset;

public:
	OffsetFileWriteStream(File* file) : file(file) {
		offset = static_cast<uint64_t>(file->tell());
	}

	File* getFile() {
		return file;
	}

	uint64_t tell() const {
		return offset;
	}

	virtual void put(int c) {
		file->aput(offset, c);
		++offset;
	}

    virtual void write(const byte* buf, uint32_t n) {
		offset += file->awrite(offset, buf, n);
	}

	void seek(uint64_t pos) {
		offset = pos;
	}

	virtual ~OffsetFileWriteStream() {
	}
};

class OffsetFileReadStream : public ReadStream {
public:
	OffsetFileReadStream(File* file = nullptr) : file(file), offset(0) {
	}

	void setFile(File* newFile) {
		file = newFile;
		offset = 0;
	}

	File* getFile() {
		return file;
	}

	uint64_t getOffset() const {
		return offset;
	}

	virtual int get() {
		return file->aget(offset++);
	}

    virtual size_t read(byte* buf, size_t n) {
		size_t count = file->aread(offset, buf, n);
		offset += count;
		return count;
	}

	void seek(uint64_t pos) {
		offset = pos;
	}

	virtual ~OffsetFileReadStream() {
	}

private:
	File* file;
	uint64_t offset;
};

class WriteFileStream : public WriteStream {
	File file;
	uint64_t count;
public:
	WriteFileStream() : count(0) {
	}

	int open(const std::string& fileName, std::ios_base::open_mode mode = std::ios_base::binary) {
		count = 0;
		return file.open(fileName, mode | std::ios_base::out);
	}
	
	File& getFile() {
		return file;
	}

	void close() {
		file.close();
	}

	uint64_t getCount() const {
		return count;
	}

	virtual void put(int c) {
		++count;
		file.put(c);
	}

    virtual void write(const byte* buf, size_t n) {
		count += n;
		file.write(buf, n);
	}

	virtual uint64_t tell() const {
		return count;
	}

	virtual ~WriteFileStream() {
	}
};

class ReadFileStream : public ReadStream {
	File file;
	uint64_t count;
public:
	ReadFileStream() : count(0) {
	}

	int open(const std::string& fileName, std::ios_base::open_mode mode = std::ios_base::binary) {
		count = 0;
		return file.open(fileName, mode | std::ios_base::in);
	}

	File& getFile() {
		return file;
	}

	void close() {
		file.close();
	}

	uint64_t getCount() const {
		return count;
	}

	void seek(uint64_t pos) {
		file.seek(pos, SEEK_SET);
		count = pos;
	}

    virtual int get() {
		int c = file.get();
		if (c != EOF) {
			++count;
		}
		return c;
	}

    virtual size_t read(byte* buf, size_t n) {
		size_t ret = file.read(buf, n);
		count += ret;
		return ret;
	}

	virtual uint64_t tell() const {
		return count;
	}

    virtual ~ReadFileStream() {
	}
};

template <const uint32_t size = 16 * KB>
class BufferedFileStream {
public:
	static const uint64_t mask = size - 1;
	byte buffer[size];
	uint64_t total, eof_pos;
	File* file;

private:
	void flush() {
		if ((total & mask) != 0) {
			file->write(buffer, static_cast<uint32_t>(total & mask));
		}
	}

	void flushWhole() {
		assert((total & mask) == 0);
		file->write(buffer, static_cast<uint32_t>(size));
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
		return file->open(fileName, mode);
	}

	inline void write(byte ch) {
		uint64_t pos = total++;
		buffer[pos & mask] = ch;
		if ((total & mask) == 0) {
			flushWhole();
		}
	}
	
	void prefetch() {
		uint64_t readCount = file->read(buffer, size);
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
		eof_pos = (uint32_t)-1;
		file->rewind();
	}

	void close() {
		if (file->isOpen()) {
			flush();
			file->close();
			total = 0;
		}
	}
		
	void reset() {
		file->close();
		total = 0;
		eof_pos = (uint32_t)-1;
	}

	BufferedFileStream() {
		reset();
	}

	virtual ~BufferedFileStream() {
		close();
	} 
};

class FileManager {
public:
	class CachedFile {
	public:
		std::string name;
		std::ios_base::open_mode mode;
		File file;
		uint32_t count;
		
		File* getFile() {
			return &file;
		}

		const std::string& getName() const {
			return name;
		}

		CachedFile() : count(0) {
		}
	};

	// Clean up.
	CachedFile* open(const std::string& name, std::ios_base::open_mode mode = std::ios_base::binary) {
 		CachedFile* ret = nullptr;
		lock.lock();
		auto it = files.find(name);
		if (it == files.end()) {
			ret = files[name] = new CachedFile;
			ret->name = name;
			ret->file.open(name.c_str(), mode);
			ret->mode = mode;
		} else {
			// Make sure that our modes match.
			// assert(it->mode == mode);
		}
		++ret->count;
		lock.unlock();
		return ret;
	}

	void close_file(CachedFile*& file) {
		bool delete_file = false;
		lock.lock();
		if (!--file->count) {
			auto it = files.find(file->getName());
			assert(it != files.end());
			files.erase(it);
			delete_file = true;
		}
		file = nullptr;
		lock.unlock();
		if (delete_file) {
			delete file;
		}
	}

	virtual ~FileManager() {
		while (!files.empty()) {
			close_file(files.begin()->second);
		}
	}

private:
	std::map<std::string, CachedFile*> files;
	std::mutex lock;
};

forceinline WriteStream& operator << (WriteStream& stream, uint8_t c) {
	stream.put(c);
	return stream;
}

forceinline WriteStream& operator << (WriteStream& stream, int8_t c) {
	return stream << static_cast<uint8_t>(c);
}

forceinline WriteStream& operator << (WriteStream& stream, uint16_t c) {
	return stream << static_cast<uint8_t>(c >> 8) << static_cast<uint8_t>(c);
}

forceinline WriteStream& operator << (WriteStream& stream, int16_t c) {
	return stream << static_cast<uint16_t>(c);
}

inline WriteStream& operator << (WriteStream& stream, uint32_t c) {
	return stream << static_cast<uint16_t>(c >> 16) << static_cast<uint16_t>(c);
}

inline WriteStream& operator << (WriteStream& stream, int32_t c) {
	return stream << static_cast<uint32_t>(c);
}

inline WriteStream& operator << (WriteStream& stream, uint64_t c) {
	return stream << static_cast<uint16_t>(c >> 32) << static_cast<uint16_t>(c);
}

inline WriteStream& operator << (WriteStream& stream, int64_t c) {
	return stream << static_cast<uint64_t>(c);
}

inline WriteStream& operator << (WriteStream& stream, float c) {
	return stream << *reinterpret_cast<uint32_t*>(&c);
}

inline WriteStream& operator << (WriteStream& stream, double c) {
	return stream << *reinterpret_cast<uint64_t*>(&c);
}

inline uint64_t getFileLength(const std::string& name) {
	FILE* file = nullptr;
#ifdef WIN32
	fopen_s(&file, name.c_str(), "rb");
#else
	file = fopen(name.c_str(), "rb");
#endif
	fseek(file, 0, SEEK_END);
	auto ret = _ftelli64(file);
	fclose(file);
	return static_cast<uint64_t>(ret);
}

#endif
