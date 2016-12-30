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

#ifndef _SLIDING_WINDOW_HPP_
#define _SLIDING_WINDOW_HPP_
#pragma once

#include <algorithm>
#include <cassert>
#include <memory>

#include "Util.hpp"

template <typename T>
class CyclicBuffer {
protected:
  size_t pos_ = 0, mask_ = 0, alloc_size_ = 0;
  std::unique_ptr<T[]> storage_;
  T *data_;
public:

  ALWAYS_INLINE size_t Pos() const { return pos_; }
  ALWAYS_INLINE size_t Mask() const { return mask_; }
  ALWAYS_INLINE T* Data() { return data_; }
  size_t Prev(size_t pos, size_t count) const {
    // Relies on integer underflow behavior. Works since pow 2 size.
    return (pos - count) & mask_;
  }
  size_t Next(size_t pos, size_t count) const {
    return (pos + count) & mask_;
  }
  ALWAYS_INLINE size_t Size() const {
    return mask_ + 1;
  }
  virtual ~CyclicBuffer() {
    release();
  }
  virtual void Restart() {
    pos_ = 0;
  }
  ALWAYS_INLINE void Push(T val) {
    data_[pos_++ & mask_] = val;
  }
  void Push(const T* elements, size_t count) {
    const size_t masked_pos = pos_ & mask_;
    pos_ += count;
    size_t max_c = Size() - masked_pos;
    size_t copy_fist = std::min(max_c, count);
    std::copy_n(elements, copy_fist, &data_[masked_pos]);
    count -= copy_fist;
    if (count == 0) return;
    std::copy_n(elements + copy_fist, count, &data_[0]);  // Copy remaining.
  }
  ALWAYS_INLINE T& operator [] (size_t offset) {
    return data_[offset & mask_];
  }
  ALWAYS_INLINE T operator [] (size_t offset) const {
    return data_[offset & mask_];
  }
  ALWAYS_INLINE T operator () (size_t offset) const {
    return data_[offset];
  }
  virtual void release() {
    pos_ = alloc_size_ = 0;
    mask_ = static_cast<size_t>(-1);
    storage_.reset();
    data_ = nullptr;
  }
  void Fill(T d) {
    std::fill(&storage_[0], &storage_[0] + Size(), d);
  }
  void CopyStartToEndOfBuffer(size_t count) {
    size_t size = Size();
    for (size_t i = 0;i < count;++i) {
      data_[size + i] = data_[i];
    }
  }
  void CopyEndToStartOfBuffer(size_t count) {
    size_t size = Size();
    for (size_t i = 0;i < count;++i) {
      storage_[i] = storage_[i + size];
    }
  }
  void Resize(size_t new_size, size_t padding = sizeof(uint32_t)) {
    // Ensure power of 2.
    assert((new_size & (new_size - 1)) == 0);
    storage_.release();
    mask_ = new_size - 1;
    alloc_size_ = new_size + padding * 2;
    storage_.reset(new T[alloc_size_]());
    data_ = storage_.get() + padding;
    Restart();
  }
};

template <typename T>
class CyclicDeque : public CyclicBuffer<T> {
  size_t size_ = 0;
  size_t front_pos_ = 0;
public:
  ALWAYS_INLINE size_t Capacity() const {
    return this->mask_ + 1;
  }
  void PopFront(size_t count = 1) {
    dcheck(size_ >= count);
    front_pos_ += count;
    size_ -= count;
  }
  void PushBackCount(T* elements, size_t count) {
    assert(size_ + count <= Capacity());
    CyclicBuffer<T>::Push(elements, count);
    size_ += count;
  }
  void PushBack(T c) {
    assert(size_ < Capacity());
    ++size_;
    this->Push(c);
  }
  ALWAYS_INLINE T Front() const {
    return this->data_[front_pos_ & this->Mask()];
  }
  ALWAYS_INLINE size_t Size() const {
    return size_;
  }
  ALWAYS_INLINE size_t Remain() const {
    return Capacity() - Size();
  }
  ALWAYS_INLINE T operator [] (size_t offset) const {
    return this->data_[(front_pos_ + offset) & this->Mask()];
  }
  ALWAYS_INLINE bool Full() const {
    return this->Size() == Capacity();
  }
  ALWAYS_INLINE bool Empty() const {
    return this->Size() == 0;
  }
};

template <typename Buffer>
class Window {
public:
  Window(Buffer& buffer, size_t offset) : buffer_(buffer), offset_(offset) {}
  size_t size() const {
    return buffer_.Size() - offset_;
  }
  template <Endian kEndian>
  size_t Read(size_t pos, size_t len) const {
    pos += offset_;
    if (pos + len > buffer_.Size()) return 0;
    size_t acc = 0;
    for (size_t i = 0; i < len; ++i) {
      if (kEndian == kEndianLittle) {
        acc |= buffer_[pos + i] << (i * 8);
      } else {
        acc = (acc << 8) | buffer_[pos + i];
      }
    }
    return acc;
  }

private:
  Buffer& buffer_;
  const size_t offset_;
};

#endif
