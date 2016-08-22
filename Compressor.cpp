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

Store::Store() {
  uint8_t text_reorder[] = {7,14,12,3,1,4,6,9,11,15,16,17,18,13,19,5,45,20,21,22,23,8,2,26,10,32,36,35,30,42,29,34,24,37,25,31,33,43,39,38,0,41,28,40,44,46,58,59,27,60,61,91,63,95,47,94,64,92,124,62,93,96,123,125,72,69,68,65,66,67,83,82,73,71,70,80,76,81,77,87,78,74,79,84,75,48,49,50,51,52,53,54,55,56,57,86,88,97,98,99,100,85,101,90,103,104,89,105,107,102,108,109,110,111,106,113,112,114,115,116,119,118,120,121,117,122,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,151,144,145,146,147,148,149,150,152,153,155,156,157,154,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,224,225,226,239,227,228,229,230,231,232,233,234,235,236,237,238,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255,};
  for (int i = 0; i < 256; ++i) {
    transform_[text_reorder[i]] = i;
    reverse_[i] = text_reorder[i];
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
    for (size_t i = 0; kReorder && i < read; ++i) {
      buffer[i] = transform_[buffer[i]];
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
    for (size_t i = 0; kReorder && i < read; ++i) {
      buffer[i] = reverse_[buffer[i]];
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
