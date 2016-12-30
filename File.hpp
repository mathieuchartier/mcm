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
#define _fseeki64 fseeko
#define _ftelli64 ftello
// extern int __cdecl _fseeki64(FILE *, int64_t, int);
// extern int64_t __cdecl _ftelli64(FILE *);
#endif

class FileList;

class FileInfo {
public:
  typedef uint16_t AttributeType;
  static const uint16_t kAttrDirectory = 0x1;
  static const uint16_t kAttrReadPermission = 0x2;
  static const uint16_t kAttrWritePermission = 0x4;
  static const uint16_t kAttrExecutePermission = 0x8;
  static const uint16_t kAttrSystem = 0x10;
  static const uint16_t kAttrHidden = 0x20;

  FileInfo() {}
  FileInfo(const std::string& name, const std::string* prefix = nullptr);
  FileInfo(const std::string& name, const std::string* prefix, uint32_t attributes);
  FileInfo(const FileInfo& f) { *this = f; }
  FileInfo& operator=(const FileInfo& f) {
    attributes_ = f.attributes_;
    name_ = f.name_;
    prefix_ = f.prefix_;
    open_count_ = f.open_count_;
    return *this;
  }
  const std::string& getName() const {
    return name_;
  }
  const std::string getFullName() const {
    return prefix_ == nullptr ? name_ : *prefix_ + name_;
  }
  AttributeType getAttributes() const {
    return attributes_;
  }
  bool isDir() const {
    return (getAttributes() & kAttrDirectory) != 0;
  }
  static std::string attrToStr(uint16_t attr);
  bool previouslyOpened() const {
    return open_count_ > 0;
  }
  void addOpen() {
    ++open_count_;
  }
  void setPrefix(const std::string* prefix) {
    prefix_ = prefix;
  }
  void SetName(const std::string& name) {
    name_ = name;
  }
  static void CreateDir(const std::string& name);

private:
  void convertAttributes(uint32_t attrs);

  AttributeType attributes_ = 0;
  std::string name_;
  const std::string* prefix_ = nullptr;
  uint32_t open_count_ = 0;
  // TODO: File date.

  friend class FileList;
};

class FileList : public std::vector<FileInfo> {
public:
  bool addDirectory(const std::string& dir, const std::string* prefix = nullptr);
  bool addDirectoryRec(const std::string& dir, const std::string* prefix = nullptr);
  // Read / write to stream.
  void read(Stream* stream);
  void write(Stream* stream);
};


class File : public Stream {
protected:
  std::mutex lock;
  uint64_t offset = 0; // Current offset in the file.
  FILE* handle = nullptr;
public:
  virtual ~File() {
    close();
  }

  File() {}
  File(const std::string& file_name, std::ios_base::open_mode mode = std::ios_base::in | std::ios_base::binary) {
    open(file_name, mode);
  }

  std::mutex& getLock() { return lock; }

  // Not thread safe.
  void write(const uint8_t* bytes, size_t count) {
    while (count != 0) {
      size_t ret = fwrite(bytes, 1, count, handle);
      if (ret == 0 && ferror(handle) != 0) {
        int e = errno;
        if (errno != EINTR) {
          perror("Fatal error during file writing");
          check(false);
        } else {
          clearerr(handle);
        }
      }
      offset += ret;
      count -= ret;
    }
  }

  // Not thread safe.
  ALWAYS_INLINE void put(int c) {
    ++offset;
    fputc(c, handle);
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
    close();
    std::ostringstream oss;
    if (mode & std::ios_base::in) {
      oss << "r";
      if (mode & std::ios_base::out) {
        oss << "+";
      }
    } else if (mode & std::ios_base::out) {
      oss << "w";
    }
    if (mode & std::ios_base::app) {
      oss << "a";
    }

    if (mode & std::ios_base::binary) {
      oss << "b";
    }
    handle = fopen(fileName.c_str(), oss.str().c_str());
    if (handle != nullptr) {
      offset = _ftelli64(handle);
      return 0;
    }
    return errno;
  }

  // Not thread safe.
  size_t read(uint8_t* buffer, size_t bytes) {
    size_t ret = fread(buffer, 1, bytes, handle);
    offset += ret;
    return ret;
  }

  // Not thread safe.
  int get() {
    ++offset;
    return fgetc(handle);
  }

  uint64_t tell() const {
    return offset;
  }

  void seek(uint64_t pos) {
    auto res = seek(static_cast<uint64_t>(pos), SEEK_SET);
    dcheck(res == 0);
  }
  int seek(int64_t pos, int origin) {
    if (origin == SEEK_SET && pos == offset) {
      return 0; // No need to do anything.
    }
    int ret = _fseeki64(handle, pos, origin);
    if (ret == 0) {
      if (origin != SEEK_SET) {
        // Don't necessarily know where the end is.
        offset = _ftelli64(handle);
      } else {
        offset = pos;
        assert(_ftelli64(handle) == offset);
      }
    }
    return ret;
  }

  ALWAYS_INLINE FILE* getHandle() {
    return handle;
  }

  // Atomic read.
  // TODO: fread already acquires a lock.
  ALWAYS_INLINE size_t readat(uint64_t pos, uint8_t* buffer, size_t bytes) {
    std::unique_lock<std::mutex> mu(lock);
    seek(pos);
    return read(buffer, bytes);
  }

  // Atomic write.
  // TODO: fwrite already acquires a lock.
  ALWAYS_INLINE void writeat(uint64_t pos, const uint8_t* buffer, size_t bytes) {
    std::unique_lock<std::mutex> mu(lock);
    seek(pos);
    write(buffer, bytes);
  }

  // Atomic get (slow).
  ALWAYS_INLINE int aget(uint64_t pos) {
    std::unique_lock<std::mutex> mu(lock);
    seek(pos);
    return get();
  }

  // Atomic write (slow).
  ALWAYS_INLINE void aput(uint64_t pos, uint8_t c) {
    std::unique_lock<std::mutex> mu(lock);
    seek(pos);
    put(c);
  }

  ALWAYS_INLINE uint64_t length() {
    std::unique_lock<std::mutex> mu(lock);
    seek(0, SEEK_END);
    uint64_t length = tell();
    seek(0, SEEK_SET);
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

// FileSegmentStream is an advanced stream which reads / writes from arbitrary sections from multiple files.
class FileSegmentStream : public Stream {
public:
  struct SegmentRange {
    // 32 bits for reducing memory usage.
    uint64_t offset_;
    uint64_t length_;
  };
  class FileSegments {
  public:
    size_t stream_idx_;
    uint64_t base_offset_;
    uint64_t total_size_;  // Used to optimized seek.
    std::vector<SegmentRange> ranges_;

    void calculateTotalSize() {
      total_size_ = 0;
      for (const auto& seg : ranges_) total_size_ += seg.length_;
    }

    void write(Stream* stream) {
      stream->leb128Encode(stream_idx_);
      stream->leb128Encode(base_offset_);
      stream->leb128Encode(ranges_.size());
      check(ranges_.size());
      uint64_t prev = 0;
      for (const auto& r : ranges_) {
        stream->leb128Encode(static_cast<uint64_t>(r.length_));
      }
      for (const auto& r : ranges_) {
        uint64_t delta = r.offset_ - prev;
        stream->leb128Encode(delta);
        prev = r.offset_ + r.length_;
      }
    }

    void read(Stream* stream) {
      stream_idx_ = stream->leb128Decode();
      base_offset_ = stream->leb128Decode();
      const auto num_ranges = stream->leb128Decode();
      check(num_ranges < 10000000);
      uint64_t prev = 0;
      ranges_.resize(num_ranges);
      for (auto& r : ranges_) {
        r.length_ = stream->leb128Decode();
      }
      for (auto& r : ranges_) {
        uint64_t delta = stream->leb128Decode();
        r.offset_ = prev + delta;
        prev = r.offset_ + r.length_;
      }
    }
  };

  FileSegmentStream(std::vector<FileSegments>* segments, uint64_t count)
    : segments_(segments), count_(count) {
    seekStart();
  }
  virtual ~FileSegmentStream() {}
  void seekStart() {
    file_idx_ = -1;
    range_idx_ = 0;
    num_ranges_ = 0;
    cur_pos_ = 0;
    cur_end_ = 0;
    cur_stream_ = nullptr;
  }
  virtual void put(int c) {
    const uint8_t b = c;
    write(&b, 1);
  }
  virtual void write(const uint8_t* buf, size_t n) {
    process<true>(const_cast<uint8_t*>(buf), n);
  }
  virtual int get() {
    uint8_t c;
    if (read(&c, 1) == 0) return EOF;
    return c;
  }
  virtual size_t read(uint8_t* buf, size_t n) {
    return process<false>(buf, n);
  }
  virtual uint64_t tell() const {
    return count_;
  }

protected:
  Stream* cur_stream_;

private:
  std::vector<FileSegments>* const segments_;
  int file_idx_;
  size_t range_idx_;
  size_t num_ranges_;
  uint64_t cur_pos_;
  uint64_t cur_end_;
  uint64_t count_;

  template <bool w>
  size_t process(uint8_t* buf, size_t n) {
    const uint8_t* start = buf;
    const uint8_t* limit = buf + n;
    while (buf < limit) {
      if (cur_pos_ >= cur_end_) {
        nextRange();
        if (cur_pos_ >= cur_end_) break;
      }
      const size_t max_c = std::min(
        static_cast<size_t>(limit - buf), static_cast<size_t>(cur_end_ - cur_pos_));
      size_t count;
      if (w) {
        cur_stream_->writeat(cur_pos_, buf, max_c);
        count = max_c;
      } else {
        count = cur_stream_->readat(cur_pos_, buf, max_c);
      }
      cur_pos_ += count;
      buf += count;
    }
    count_ += buf - start;
    return buf - start;
  }
  virtual Stream* openNewStream(size_t index) = 0;
  void nextRange() {
    while (cur_pos_ >= cur_end_) {
      if (range_idx_ < num_ranges_) {
        auto* segs = &segments_->operator[](file_idx_);
        auto* range = &segs->ranges_[range_idx_];
        cur_pos_ = segs->base_offset_ + range->offset_;
        cur_end_ = cur_pos_ + range->length_;
        ++range_idx_;
      } else {
        // Out of ranges in current file, go to next file.
        ++file_idx_;
        if (file_idx_ >= segments_->size()) return;
        auto* segs = &segments_->operator[](file_idx_);
        num_ranges_ = segs->ranges_.size();
        cur_stream_ = openNewStream(segs->stream_idx_);
        range_idx_ = 0;
      }
    }
  }
};

class FileManager {
public:
  class CachedFile {
  public:
    std::string name;
    std::ios_base::open_mode mode;
    File file;
    uint32_t count = 0;

    File* getFile() { return &file; }
    const std::string& getName() const { return name; }
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

ALWAYS_INLINE WriteStream& operator << (WriteStream& stream, uint8_t c) {
  stream.put(c);
  return stream;
}

ALWAYS_INLINE WriteStream& operator << (WriteStream& stream, int8_t c) {
  return stream << static_cast<uint8_t>(c);
}

ALWAYS_INLINE WriteStream& operator << (WriteStream& stream, uint16_t c) {
  return stream << static_cast<uint8_t>(c >> 8) << static_cast<uint8_t>(c);
}

ALWAYS_INLINE WriteStream& operator << (WriteStream& stream, int16_t c) {
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

// OS specific, directory.

#endif
