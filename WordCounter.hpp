/*	MCM file compressor

  Copyright (C) 2016, Google Inc.
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

#ifndef _WORD_COUNTER_HPP_
#define _WORD_COUNTER_HPP_

#include <string>
#include <vector>

#include "Memory.hpp"
#include "Util.hpp"

// Word capital conversion.
enum WordCC {
  kWordCCNone,
  kWordCCFirstChar,
  kWordCCAll,
  kWordCCInvalid,
  kWordCCCount, // Number of elements
};

static WordCC GetWordCase(const uint8_t* word, size_t word_len) {
  bool first_cap = IsUpperCase(word[0]);
  size_t cap_count = first_cap;
  for (size_t i = 1; i < word_len; ++i) {
    cap_count += IsUpperCase(word[i]);
  }
  if (cap_count == word_len) {
    return kWordCCAll;
  } else if (first_cap && cap_count == 1) {
    return kWordCCFirstChar;
  } else if (cap_count != 0) {
    return kWordCCInvalid;
  }
  return kWordCCNone;
}

class WordCount {
  std::string word_;

  // Occurrences that are normal.
  uint32_t normal_count_ = 0;

  // Occurences that require first char CC.
  uint32_t first_char_count_ = 0;

  // Occurences that require all char CC.
  uint32_t all_char_count_ = 0;

public:
  WordCount(const std::string& w, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0)
    : word_(w),
      normal_count_(a),
      first_char_count_(b),
      all_char_count_(c) {}
  WordCount() = default;
  WordCount(const WordCount&) = default;
  WordCount& operator=(const WordCount&) = default;

  const std::string& Word() const {
    return word_;
  }

  ALWAYS_INLINE size_t Count() const {
    return normal_count_ + CapCount();
  }

  ALWAYS_INLINE size_t CapCount() const {
    return first_char_count_ + all_char_count_;
  }

  void RemoveFrequencies(FrequencyCounter<256>* f) {
    // TODO: Deal with CC.
    f->Remove(MakeUpperCase(static_cast<uint8_t>(word_[0])), first_char_count_);
    for (size_t i = 0; i < word_.length(); ++i) {
      const uint8_t c = word_[i];
      f->Remove(MakeUpperCase(c), all_char_count_);
      f->Remove(c, normal_count_);
      if (i != 0) {
        f->Remove(c, first_char_count_);
      }
    }
  }

  void AddCCFrequencies(FrequencyCounter<256>* f, uint8_t cc_first, uint8_t cc_all) {
    f->Add(cc_first, first_char_count_);
    f->Add(cc_all, all_char_count_);
  }

  void UpdateFrequencies(FrequencyCounter<256>* f, uint8_t cc_first, uint8_t cc_all) {
    RemoveFrequencies(f);
    AddCCFrequencies(f, cc_first, cc_all);
  }

  int64_t SavingsVS(size_t code_word_len) const {
    auto cur_savings = Savings(code_word_len);
    if (code_word_len < 3) {
      // Compare savings for cur vs 1 longer code word.
      return cur_savings - Savings(code_word_len + 1);
    }
    return cur_savings;
  }

  int64_t Savings(size_t code_word_len) const {
    // This approach seems worse for some reason.
    const size_t normal_count = normal_count_;
    const size_t cap_count = CapCount();
    int64_t before = word_.size() * (normal_count + cap_count);
    int64_t after = code_word_len * (normal_count + cap_count) + cap_count + word_.size() + 1;
    return before - after;
  }

  class CompareSavings {
  public:
    CompareSavings(size_t code_word_len) : code_word_len_(code_word_len) {}
    bool operator()(const WordCount& a, const WordCount& b) const {
      return a.SavingsVS(code_word_len_) < b.SavingsVS(code_word_len_);
    }

  private:
    const size_t code_word_len_;
  };

  class CompareLexicographically {
  public:
    bool operator()(const WordCount& a, const WordCount& b) const {
      return a.word_ < b.word_;
    }
  };
};

// Fast memory efficient word counter that uses a compacting GC.
class WordCounter {
public:
  static constexpr size_t kMaxLength = 256;

  ~WordCounter() {
    std::cerr << std::endl << "Word counter used " << Used() << " hash size " << hash_mask_ << std::endl;
  }

  void Init(size_t memory) {
    assert(memory % 8 == 0);
    mem_map_.resize(memory);
    hash_table_ = reinterpret_cast<uint32_t*>(mem_map_.getData());
    hash_mask_ = memory / 2 / sizeof(hash_table_[0]);
    while ((hash_mask_ & (hash_mask_ + 1)) != 0) --hash_mask_;
    ptr_ = begin_ = reinterpret_cast<uint8_t*>(hash_table_ + hash_mask_ + 1);
    end_ = reinterpret_cast<uint8_t*>(mem_map_.getData()) + memory;
    // Hash | pos is stored in the hash table.
    pos_mask_ = Remain();
    while ((pos_mask_ & (pos_mask_ + 1)) != 0) ++pos_mask_;
    ClearHashTable();
  }

  void Clear() {
    mem_map_.resize(0);
  }

  template <class Visitor>
  void Visit(const Visitor& visitor) {
    auto cur = begin_;
    while (cur < ptr_) {
      auto* cur_entry = reinterpret_cast<Entry*>(cur);
      visitor(cur_entry);
      cur += cur_entry->SizeOf();
    }
  }

  // Mark compact.
  void GC(size_t min_count) {
    auto start_size = Used();
    auto start = clock();
    auto cur = begin_;
    auto dest = begin_;
    while (cur < end_) {
      auto* cur_entry = reinterpret_cast<Entry*>(cur);
      auto size = cur_entry->SizeOf();
      if (cur_entry->Count() >= min_count) {
        memmove(dest, cur, size);
        dest += size;
      }
      cur += size;
    }
    ptr_ = dest;
    // Rehash all.
    ClearHashTable();
    Visit([this](Entry* entry) {
      HashEntry(entry);
    });
    // Remove all words that have <= min_count_.
    std::cerr << std::endl << "GC " << prettySize(start_size) << " -> " << Used() << " in " << clockToSeconds(clock() - start) << "S" << std::endl;
  }

  void AddWord(const uint8_t* begin, const uint8_t* end, WordCC cc_type) {
    size_t len = end - begin;
    auto index = Lookup(begin, len);
    Entry* entry;
    if (hash_table_[index] == kInvalidPos) {
      auto required = Entry::ComputeSize(len);
      while (Remain() < required) {
        GC(min_count_);
        ++min_count_;
      }
      entry = new (ptr_) Entry(begin, end);
      hash_table_[index] = GetOffset(entry) | (entry->Hash() & ~pos_mask_);
      ptr_ += required;
    } else {
      entry = reinterpret_cast<Entry*>(begin_ + (hash_table_[index] & pos_mask_));
    }
    entry->Add(cc_type);
  }

  void GetWords(std::vector<WordCount>& out, size_t min_occurences) {
    Visit([&out, min_occurences](Entry* entry) {
      if (entry->Count() >= min_occurences) {
        std::string string_name(entry->Begin(), entry->End());
        auto normal_count = entry->Count(kWordCCNone);
        auto first_char = entry->Count(kWordCCFirstChar);
        auto all_char = entry->Count(kWordCCAll);
        uint32_t cc_count = first_char + all_char;
        // Keep first char capital if is excessively more common. Reduces size due to escapes.
        const bool kEnableCapDict = false;
        if (kEnableCapDict && first_char > 10 * (normal_count + all_char)) {
          assert(IsLowerCase(string_name[0]));
          string_name[0] = MakeUpperCase(string_name[0]);
        }
        WordCount wc(string_name, normal_count, first_char, all_char);
        out.push_back(wc);
      }
    });
  }

private:
  // Memory efficient word counter.
  class Entry {
  public:
    static size_t ComputeSize() {
      return sizeof(WordCounter);
    }

    bool Equals(const uint8_t* word, size_t len) const {
      return length_ == len && memcmp(word, data_, len) == 0;
    }

    size_t SizeOf() const {
      return ComputeSize(length_);
    }

    const char* Begin() const {
      return reinterpret_cast<const char*>(&data_[0]);
    }

    const char* End() const {
      return reinterpret_cast<const char*>(&data_[0]) + length_;
    }

    size_t Count(WordCC type) const {
      return count_[type];
    }

    size_t Count() const {
      return std::accumulate(count_, count_ + kWordCCInvalid, 0);
    }

    static size_t ComputeSize(uint32_t length) {
      return sizeof(Entry) + RoundUp(length, sizeof(uint32_t));
    }

    uint32_t Hash() const {
      return ComputeHash(data_, length_);
    }

    static uint32_t ComputeHash(const uint8_t* str, size_t len) {
      uint32_t ret = len;
      for (size_t i = 0; i < len; ++i) {
        ret = ret * 31 + str[i];
      }
      return ret;
    }

    ALWAYS_INLINE bool Equals(const Entry* other) const {
      return length_ == other->length_ && memcmp(data_, other->data_, length_) == 0;
    }

    Entry(const uint8_t* begin, const uint8_t* end) : length_(end - begin) {
      memcpy(&data_[0], begin, length_);
    }

    void Add(WordCC type) {
      ++count_[static_cast<uint32_t>(type)];
    }

  private:
    // Count for each modifier.
    uint32_t count_[3] = {};
    union {
      struct {
        uint8_t length_;
        uint8_t data_[3];
      };
      uint32_t next_;
    };
    // Linked list context list.
  };

  size_t Remain() const {
    return end_ - ptr_;
  }

  size_t Used() const {
    return ptr_ - begin_;
  }

  size_t GetOffset(Entry* entry) const {
    return reinterpret_cast<uint8_t*>(entry) - begin_;
  }

  void ClearHashTable() {
    std::fill(hash_table_, hash_table_ + hash_mask_ + 1, kInvalidPos);
  }

  // Return hash table index.
  uint32_t Lookup(const uint8_t* word, size_t len) {
    auto h = Entry::ComputeHash(word, len);
    auto index = h % hash_mask_;
    for (;;) {
      auto slot = hash_table_[index];
      if (slot == kInvalidPos) break;
      if ((slot & ~pos_mask_) == (h & ~pos_mask_)) {
        auto* entry = reinterpret_cast<Entry*>(begin_ + (slot & pos_mask_));
        if (entry->Equals(word, len)) {
          break;
        }
      }
      if (++index > hash_mask_) {
        index = 0;
      }
    }
    return index;
  }

  void HashEntry(Entry* entry) {
    auto offset = GetOffset(entry);
    auto h = entry->Hash();
    auto index = h & hash_mask_;
    for (;;) {
      if (hash_table_[index] == kInvalidPos) {
        dcheck(offset <= pos_mask_);
        hash_table_[index] = offset | (h & ~pos_mask_);
        break;
      }
      if (++index > hash_mask_) {
        index = 0;
      }
    }
  }

  // Minimum count for keeping.
  size_t min_count_ = 2;
  uint8_t* begin_;
  uint8_t* ptr_;
  uint8_t* end_;
  MemMap mem_map_;
  uint32_t* hash_table_;
  size_t hash_mask_;
  uint32_t pos_mask_;
  static constexpr uint32_t kInvalidPos = 0xFFFFFFFF;
};

#endif
