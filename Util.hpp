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

#ifndef _UTIL_HPP_
#define _UTIL_HPP_

#include <mutex>
#include <cassert>
#include <ctime>
#include <emmintrin.h>
#include <fstream>
#include <iostream>
#include <mmintrin.h>
#include <ostream>
#include <stdint.h>
#include <sstream>
#include <string>
#include <vector>

#define OVERRIDE

#ifdef WIN32
#define ALWAYS_INLINE __forceinline
#define NO_INLINE __declspec(noinline)
#else
#define ALWAYS_INLINE __attribute__((always_inline))
#define NO_INLINE __declspec(noinline)
#endif

#define no_alias __restrict
#define rst // no_alias

// TODO: Implement these.
#define LIKELY(x) x
#define UNLIKELY(x) x

#ifdef _DEBUG
static const bool kIsDebugBuild = true;
#else
static const bool kIsDebugBuild = false;
#endif

#ifdef _MSC_VER
#define ASSUME(x) __assume(x)
#else
#define ASSUME(x)
#endif

typedef uint32_t hash_t;

static const uint64_t KB = 1024;
static const uint64_t MB = KB * KB;
static const uint64_t GB = KB * MB;
static const uint32_t kCacheLineSize = 64; // Sandy bridge.
static const uint32_t kPageSize = 4 * KB;
static const uint32_t kBitsPerByte = 8;

ALWAYS_INLINE void Prefetch(const void* ptr) {
#ifdef WIN32
  _mm_prefetch((char*)ptr, _MM_HINT_T0);
#else
  __builtin_prefetch(ptr);
#endif
}

ALWAYS_INLINE static bool IsUpperCase(int c) {
  return c >= 'A' && c <= 'Z';
}

ALWAYS_INLINE static bool IsLowerCase(int c) {
  return c >= 'a' && c <= 'z';
}

ALWAYS_INLINE static bool IsWordChar(int c) {
  return IsLowerCase(c) || IsUpperCase(c) || c >= 128;
}

ALWAYS_INLINE static int UpperToLower(int c) {
  assert(IsUpperCase(c));
  return c - 'A' + 'a';
}

ALWAYS_INLINE static int LowerToUpper(int c) {
  assert(IsLowerCase(c));
  return c - 'a' + 'A';
}

ALWAYS_INLINE static int MakeUpperCase(int c) {
  if (IsLowerCase(c)) {
    c = LowerToUpper(c);
  }
  return c;
}

ALWAYS_INLINE static int MakeLowerCase(int c) {
  if (IsUpperCase(c)) {
    c = UpperToLower(c);
  }
  return c;
}

// Trust in the compiler
ALWAYS_INLINE uint32_t rotate_left(uint32_t h, uint32_t bits) {
  return (h << bits) | (h >> (sizeof(h) * 8 - bits));
}

ALWAYS_INLINE uint32_t rotate_right(uint32_t h, uint32_t bits) {
  return (h << (sizeof(h) * 8 - bits)) | (h >> bits);
}

#define check(c) while (!(c)) { std::cerr << "check failed " << #c << std::endl; *reinterpret_cast<int*>(1234) = 4321;}
#define dcheck(c) assert(c)

template <const uint32_t A, const uint32_t B, const uint32_t C, const uint32_t D>
struct shuffle {
  enum {
    value = (D << 6) | (C << 4) | (B << 2) | A,
  };
};

ALWAYS_INLINE bool isPowerOf2(uint32_t n) {
  return (n & (n - 1)) == 0;
}

ALWAYS_INLINE uint32_t bitSize(uint32_t Value) {
  uint32_t Total = 0;
  for (;Value;Value >>= 1, Total++);
  return Total;
}

template <typename T>
void printIndexedArray(const std::string& str, const T& arr) {
  uint32_t index = 0;
  std::cout << str << std::endl;
  for (const auto& it : arr) {
    if (it) {
      std::cout << index << ":" << it << std::endl;
    }
    index++;
  }
}

template <const uint64_t n>
struct _bitSize { static const uint64_t value = 1 + _bitSize<n / 2>::value; };

template <>
struct _bitSize<0> { static const uint64_t value = 0; };

inline void fatalError(const std::string& message) {
  std::cerr << "Fatal error: " << message << std::endl;
  *reinterpret_cast<uint32_t*>(1234) = 0;
}

inline void unimplementedError(const char* function) {
  std::ostringstream oss;
  oss << "Calling implemented function " << function;
  fatalError(oss.str());
}

inline uint32_t rand32() {
  return rand() ^ (rand() << 16);
}

ALWAYS_INLINE int fastAbs(int n) {
  int mask = n >> 31;
  return (n ^ mask) - mask;
}

bool fileExists(const char* name);

class Closure {
public:
  virtual void run() = 0;
};

template <typename Container>
void deleteValues(Container& container) {
  for (auto* p : container) {
    delete p;
  }
  container.clear();
}

class ScopedLock {
public:
  ScopedLock(std::mutex& mutex) : mutex_(mutex) {
    mutex_.lock();
  }

  ~ScopedLock() {
    mutex_.unlock();
  }

private:
  std::mutex& mutex_;
};

ALWAYS_INLINE void copy16bytes(uint8_t* no_alias out, const uint8_t* no_alias in) {
  _mm_storeu_ps(reinterpret_cast<float*>(out), _mm_loadu_ps(reinterpret_cast<const float*>(in)));
}

ALWAYS_INLINE static void memcpy16(void* dest, const void* src, size_t len) {
  uint8_t* no_alias dest_ptr = reinterpret_cast<uint8_t* no_alias>(dest);
  const uint8_t* no_alias src_ptr = reinterpret_cast<const uint8_t* no_alias>(src);
  const uint8_t* no_alias limit = dest_ptr + len;
  *dest_ptr++ = *src_ptr++;
  if (len >= sizeof(__m128)) {
    const uint8_t* no_alias limit2 = limit - sizeof(__m128);
    do {
      copy16bytes(dest_ptr, src_ptr);
      src_ptr += sizeof(__m128);
      dest_ptr += sizeof(__m128);
    } while (dest_ptr < limit2);
  }
  while (dest_ptr < limit) {
    *dest_ptr++ = *src_ptr++;
  }
}

template<typename CopyUnit>
ALWAYS_INLINE void fastcopy(uint8_t* no_alias out, const uint8_t* no_alias in, const uint8_t* limit) {
  do {
    *reinterpret_cast<CopyUnit* no_alias>(out) = *reinterpret_cast<const CopyUnit* no_alias>(in);
    out += sizeof(CopyUnit);
    in += sizeof(CopyUnit);
  } while (in < limit);
}

ALWAYS_INLINE void memcpy16unsafe(uint8_t* no_alias out, const uint8_t* no_alias in, const uint8_t* limit) {
  do {
    copy16bytes(out, in);
    out += 16;
    in += 16;
  } while (out < limit);
}

template<uint32_t kMaxSize>
class FixedSizeByteBuffer {
public:
  uint32_t getMaxSize() const {
    return kMaxSize;
  }

protected:
  uint8_t buffer_[kMaxSize];
};

// Move to front.
template <typename T>
class MTF {
  std::vector<T> data_;
public:
  void init(size_t n) {
    data_.resize(n);
    for (size_t i = 0; i < n; ++i) {
      data_[i] = static_cast<T>(n - 1 - i);
    }
  }
  size_t find(T value) {
    for (size_t i = 0; i < data_.size(); ++i) {
      if (data_[i] == value) {
        return i;
      }
    }
    return data_.size();
  }
  ALWAYS_INLINE T back() const {
    return data_.back();
  }
  size_t size() const {
    return data_.size();
  }
  void moveToFront(size_t index) {
    auto old = data_[index];
    while (index) {
      data_[index] = data_[index - 1];
      --index;
    }
    data_[0] = old;
  }
};

template <class T, size_t kSize>
class StaticArray {
public:
  StaticArray() {
  }
  ALWAYS_INLINE const T& operator[](size_t i) const {
    return data_[i];
  }
  ALWAYS_INLINE T& operator[](size_t i) {
    return data_[i];
  }
  ALWAYS_INLINE size_t size() const {
    return kSize;
  }

private:
  T data_[kSize];
};

template <class T, uint32_t kCapacity>
class StaticBuffer {
public:
  StaticBuffer() : pos_(0), size_(0) {
  }
  ALWAYS_INLINE const T& operator[](size_t i) const {
    return data_[i];
  }
  ALWAYS_INLINE T& operator[](size_t i) {
    return data_[i];
  }
  ALWAYS_INLINE size_t pos() const {
    return pos_;
  }
  ALWAYS_INLINE size_t size() const {
    return size_;
  }
  ALWAYS_INLINE size_t capacity() const {
return kCapacity;
  }
  ALWAYS_INLINE size_t reamainCapacity() const {
    return capacity() - size();
  }
  ALWAYS_INLINE T get() {
    (pos_ < size_);
    return data_[pos_++];
  }
  ALWAYS_INLINE void read(T* ptr, size_t len) {
    dcheck(pos_ + len <= size_);
    std::copy(&data_[pos_], &data_[pos_ + len], &ptr[0]);
    pos_ += len;
  }
  ALWAYS_INLINE void put(T c) {
    dcheck(pos_ < size_);
    data_[pos_++] = c;
  }
  ALWAYS_INLINE void write(const T* ptr, size_t len) {
    dcheck(pos_ + len <= size_);
    std::copy(&ptr[0], &ptr[len], &data_[pos_]);
    pos_ += len;
  }
  ALWAYS_INLINE size_t remain() const {
    return size_ - pos_;
  }
  void erase(size_t chars) {
    dcheck(chars <= pos());
    std::move(&data_[chars], &data_[size()], &data_[0]);
    pos_ -= std::min(pos_, chars);
    size_ -= std::min(size_, chars);
  }
  void addPos(size_t n) {
    pos_ += n;
    dcheck(pos_ <= size());
  }
  void addSize(size_t n) {
    size_ += n;
    dcheck(size_ <= capacity());
  }
  T* begin() {
    return &operator[](0);
  }
  T* end() {
    return &operator[](size_);
  }
  T* limit() {
    return &operator[](capacity());
  }

private:
  size_t pos_;
  size_t size_;
  T data_[kCapacity];
};

std::string prettySize(uint64_t size);
std::string formatNumber(uint64_t n);
double clockToSeconds(clock_t c);
std::string errstr(int err);
std::vector<uint8_t> randomArray(size_t size);
uint64_t computeRate(uint64_t size, uint64_t delta_time);
std::vector<uint8_t> loadFile(const std::string& name, uint32_t max_size = 0xFFFFFFF);
std::string trimExt(const std::string& str);
std::string trimDir(const std::string& str);
std::string getExt(const std::string& str);
std::pair<std::string, std::string> GetFileName(const std::string& str);

static inline int Clamp(int a, int min, int max) {
  if (a < min) a = min;
  if (a > max) a = max;
  return a;
}

static inline const size_t RoundDown(size_t n, size_t r) {
  return n - n % r;
}

static inline const size_t RoundUp(size_t n, size_t r) {
  return RoundDown(n + r - 1, r);
}

template <typename T>
static inline T* AlignUp(T* ptr, size_t r) {
  return reinterpret_cast<T*>(RoundUp(reinterpret_cast<size_t>(ptr), r));
}

template <typename T>
static void ReplaceSubstring(T* data, size_t old_pos, size_t len, size_t new_pos, size_t cur_len) {
  if (old_pos == new_pos) {
    return;
  }
  std::vector<T> temp(len);
  // Delete cur and reinsert.
  memcpy(&temp[0], &data[old_pos], len * sizeof(T));
  cur_len -= len;
  memmove(&data[old_pos], &data[old_pos + len], (cur_len - old_pos) * sizeof(T));
  // Reinsert.
  new_pos = new_pos % (cur_len + 1);
  memmove(&data[new_pos + len], &data[new_pos], (cur_len - new_pos) * sizeof(T));
  memcpy(&data[new_pos], &temp[0], len * sizeof(T));
}

template <typename T>
static void Inverse(T* out, const T* in, size_t count) {
  check(in != out);
  for (size_t i = 0; i < count; ++i) {
    out[in[i]] = i;
  }
}

template <typename Data, typename Perm>
static void Permute(Data* out, const Data* in, const Perm* perm, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    out[i] = in[perm[i]];
  }
}

template <typename Data, typename Perm>
static void InversePermute(Data* out, const Data* in, const Perm* perm, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    out[perm[i]] = in[i];
  }
}

void RunUtilTests();
bool IsAbsolutePath(const std::string& path);

template <typename T>
std::vector<T> ReadCSI(const std::string& file) {
  std::ifstream fin(file.c_str());
  std::vector<T> ret;
  for (;;) {
    T temp;
    if (!(fin >> temp)) break;
    char separator;
    fin >> separator;
    if (separator != ',') break;
    ret.push_back(temp);
  }
  return ret;
}

static inline constexpr uint32_t MakeWord(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  return (a << 24) | (b << 16) | (c << 8) | (d << 0);
}

enum Endian {
  kEndianLittle,
  kEndianBig,
};

struct OffsetBlock {
  size_t offset;
  size_t len;
};

template <uint32_t kAlphabetSize = 0x100>
class FrequencyCounter {
  uint64_t frequencies_[kAlphabetSize] = {};
public:
  ALWAYS_INLINE void Add(uint32_t index, uint64_t count = 1) {
    frequencies_[index] += count;
  }

  template <typename T>
  ALWAYS_INLINE void AddRegion(const T* in, size_t count) {
    for (size_t i = 0; i < count; ++i) {
      Add(in[i], 1);
    }
  }

  ALWAYS_INLINE void Remove(uint32_t index, uint64_t count = 1) {
    dcheck(frequencies_[index] >= count);
    frequencies_[index] -= count;
  }

  uint64_t Sum() const {
    uint64_t ret = 0;
    for (uint64_t c : frequencies_) {
      ret += c;
    }
    return ret;
  }

  void Normalize(uint32_t target) {
    check(target != 0U);
    uint64_t total = 0;
    for (auto f : frequencies_) {
      total += f;
    }
    const auto factor = static_cast<double>(target) / static_cast<double>(total);
    for (auto& f : frequencies_) {
      auto new_val = static_cast<uint32_t>(double(f) * factor);
      total += new_val - f;
      f = new_val;
    }
    // Fudge the probabilities until we match.
    int64_t delta = static_cast<int64_t>(target) - total;
    while (delta) {
      for (auto& f : frequencies_) {
        if (f) {
          if (delta > 0) {
            ++f;
            delta--;
          } else {
            // Don't ever go back down to 0 since we can't necessarily represent that.
            if (f > 1) {
              --f;
              delta++;
            }
          }
        }
      }
    }
  }

  const uint64_t* GetFrequencies() const {
    return frequencies_;
  }
};

#endif
