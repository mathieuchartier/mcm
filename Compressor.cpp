/*	MCM file compressor

  Copyright (C) 2014, Google Inc.
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

#include "Compressor.hpp"

#include <memory>

size_t MemCopyCompressor::getMaxExpansion(size_t in_size) {
  return in_size;
}

size_t MemCopyCompressor::compress(uint8_t* in, uint8_t* out, size_t count) {
  memcpy16(out, in, count);
  return count;
}

void MemCopyCompressor::decompress(uint8_t* in, uint8_t* out, size_t count) {
  memcpy16(out, in, count);
}

size_t BitStreamCompressor::getMaxExpansion(size_t in_size) {
  return in_size * kBits / 8 + 100;
}

size_t BitStreamCompressor::compressBytes(uint8_t* in, uint8_t* out, size_t count) {
  MemoryBitStream<true> stream_out(out);
  for (; count; --count) {
    stream_out.writeBits(*in++, kBits);
  }
  stream_out.flush();
  return stream_out.getData() - out;
}

void BitStreamCompressor::decompressBytes(uint8_t* in, uint8_t* out, size_t count) {
  MemoryBitStream<true> stream_in(in);
  for (size_t i = 0; i < count; ++i) {
    out[i] = stream_in.readBits(kBits);
  }
}

void Store::compress(Stream* in, Stream* out, uint64_t count) {
  static const uint64_t kBufferSize = 8 * KB;
  uint8_t buffer[kBufferSize];
  while (count > 0) {
    const size_t read = in->read(buffer, std::min(count, kBufferSize));
    if (read == 0) {
      break;
    }
    out->write(buffer, read);
    count -= read;
  }
}

void Store::decompress(Stream* in, Stream* out, uint64_t count) {
  static const uint64_t kBufferSize = 8 * KB;
  uint8_t buffer[kBufferSize];
  while (count > 0) {
    const size_t read = in->read(buffer, std::min(count, kBufferSize));
    if (read == 0) {
      break;
    }
    out->write(buffer, read);
    count -= read;
  }
}

void MemoryCompressor::compress(Stream* in, Stream* out, uint64_t max_count) {
  std::unique_ptr<uint8_t[]> in_buffer(new uint8_t[kBufferSize]);
  std::unique_ptr<uint8_t[]> out_buffer(new uint8_t[getMaxExpansion(kBufferSize)]);
  for (;;) {
    const size_t n = in->read(in_buffer.get(), std::min(kBufferSize, max_count));
    if (n == 0) {
      break;
    }
    const size_t out_bytes = compress(in_buffer.get(), out_buffer.get(), n);
    out->leb128Encode(out_bytes);
    out->write(out_buffer.get(), out_bytes);
    max_count -= n;
  }
  out->leb128Encode(0);
}

void MemoryCompressor::decompress(Stream* in, Stream* out, uint64_t max_count) {
  const size_t in_buffer_size = getMaxExpansion(kBufferSize);
  std::unique_ptr<uint8_t[]> in_buffer(new uint8_t[in_buffer_size]);
  std::unique_ptr<uint8_t[]> out_buffer(new uint8_t[kBufferSize]);
  for (;;) {
    size_t size = in->leb128Decode();
    check(size <= in_buffer_size);
    const size_t n = in->read(in_buffer.get(), size);
    check(n == size);
    if (n == 0) {
      break;
    }
    decompress(in_buffer.get(), out_buffer.get(), n);
    out->write(out_buffer.get(), kBufferSize);
    max_count -= kBufferSize;
  }
}