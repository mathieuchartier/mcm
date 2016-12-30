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

#ifndef _COMPRESS_HPP_
#define _COMPRESS_HPP_

#include <cassert>
#include <cmath>
#include <map>
#include <vector>
#include <iostream>
#include <map>
#include <list>
#include <ctime>
#include <iomanip>
#include <malloc.h>

#include "Stream.hpp"
#include "Util.hpp"

class CompressionJob {
public:
  CompressionJob() {}
  virtual ~CompressionJob() {}
  double getCompressionRatio() const {
    if (!getInBytes()) {
      return 1;
    }
    return static_cast<double>(getOutBytes()) / static_cast<double>(getInBytes());
  }

  virtual bool isDone() const = 0;
  virtual uint64_t getInBytes() const = 0;
  virtual uint64_t getOutBytes() const = 0;
};

class MultiCompressionJob : public CompressionJob {
public:
  MultiCompressionJob() {}
  virtual ~MultiCompressionJob() {}
  void addJob(CompressionJob* job) {
    jobs_.push_back(job);
  }
  virtual bool isDone() const {
    for (CompressionJob* job : jobs_) {
      if (!job->isDone()) {
        return false;
      }
    }
    return true;
  }
  virtual uint64_t getInBytes() const {
    uint64_t sum = 0;
    for (CompressionJob* job : jobs_) {
      sum += job->getInBytes();
    }
    return sum;
  }
  virtual uint64_t getOutBytes() const {
    uint64_t sum = 0;
    for (CompressionJob* job : jobs_) {
      sum += job->getOutBytes();
    }
    return sum;
  }

private:
  std::vector<CompressionJob*> jobs_;
};

// Generic compressor interface.
class Compressor {
public:
  enum Type {
    kTypeStore,
    kTypeWav16,
    kTypeCMTurbo,
    kTypeCMFast,
    kTypeCMMid,
    kTypeCMHigh,
    kTypeCMMax,
    kTypeCMSimple,
    kTypeDMC,
  };

  class Factory {
  public:
    virtual Compressor* create() = 0;
  };
  template <typename CompressorType>
  class FactoryOf : public Compressor::Factory {
    virtual Compressor* create() {
      return new CompressorType();
    }
  };

  // Optimization variable for brute forcing.
  virtual bool setOpt(uint32_t opt) {
    return true;
  }
  virtual bool setOpts(size_t* opts) {
    return true;
  }
  virtual uint32_t getOpt() const {
    return 0;
  }
  virtual void setMemUsage(uint32_t level) {
  }
  virtual bool failed() {
    return false;
  }
  // Compress n bytes.
  virtual void compress(Stream* in, Stream* out, uint64_t max_count = 0xFFFFFFFFFFFFFFFF) = 0;
  // Decompress n bytes, the calls must line up. You can't do C(20)C(30)D(50)
  virtual void decompress(Stream* in, Stream* out, uint64_t max_count = 0xFFFFFFFFFFFFFFFF) = 0;
  virtual ~Compressor() {
  }
};

// In memory compressor.
class MemoryCompressor : public Compressor {
  static const size_t kBufferSize = 32 * MB;
public:
  virtual size_t getMaxExpansion(size_t s) = 0;
  virtual size_t compress(uint8_t* in, uint8_t* out, size_t count) = 0;
  virtual void decompress(uint8_t* in, uint8_t* out, size_t count) = 0;
  // Initial implementations of compress and decompress from memory.
  virtual void compress(Stream* in, Stream* out, uint64_t max_count = 0xFFFFFFFFFFFFFFFF);
  // Decompress n bytes, the calls must line up.
  virtual void decompress(Stream* in, Stream* out, uint64_t max_count = 0xFFFFFFFFFFFFFFFF);
};

template <typename T>
class MemoryStreamWrapper : public MemoryCompressor {
  T compressor;
public:
  // Can't know.
  virtual size_t getMaxExpansion(size_t in_size) {
    return in_size * 3 / 2;
  }

  virtual uint32_t compressBytes(uint8_t* in, uint8_t* out, size_t count) {
    WriteMemoryStream wms(out);
    ReadMemoryStream rms(in, in + count);
    compressor.compress(rms, wms);
    return wms.tell();
  }

  virtual void decompressBytes(uint8_t* in, uint8_t* out, size_t count) {
    WriteMemoryStream wms(out);
    ReadMemoryStream rms(in, in + count);
    compressor.decompress(rms, wms);
  }
};

class Store : public Compressor {
  static constexpr bool kReorder = false;
public:
  Store();
  virtual void compress(Stream* in, Stream* out, uint64_t count);
  virtual void decompress(Stream* in, Stream* out, uint64_t count);
private:
  uint8_t transform_[256];
  uint8_t reverse_[256];
};

class MemCopyCompressor : public MemoryCompressor {
public:
  size_t getMaxExpansion(size_t in_size);
  size_t compress(uint8_t* in, uint8_t* out, size_t count);
  void decompress(uint8_t* in, uint8_t* out, size_t count);
};

class BitStreamCompressor : public MemoryCompressor {
  static const uint32_t kBits = 8;
public:
  size_t getMaxExpansion(size_t in_size);
  size_t compressBytes(uint8_t* in, uint8_t* out, size_t count);
  void decompressBytes(uint8_t* in, uint8_t* out, size_t count);
};

#endif
