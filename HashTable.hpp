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

#pragma once

// Fixed size hash table.
template <typename Key, typename Value>
class ChainingHashTable {
private:
  class HashEntry {
  public:
    Key key_;
    Value value_;
    uint32_t next_;
  };
public:
  void Resize(size_t buckets, size_t max_elements) {
    hash_mask_ = buckets - 1;
    table_.resize(hash_mask_ + 1);
    entries_.resize(max_elements);
    Reset();
  }

  void Reset() {
    entry_pos_ = 0;
    std::fill(table_.begin(), table_.end(), kInvalidIndex);
  }

  HashEntry& NewEntry() {
    assert(entry_pos_ < entries_.size());
    return entries_[entry_pos_++];
  }

  void Add(hash_t hash, const Key& key, const Value& value) {
    uint32_t index = GetBucketIndex(hash);
    auto& entry = NewEntry();
    entry.key_ = key;
    entry.value_ = value;
    entry.next_ = table_[index];
    table_[index] = entry_pos_ - 1;
  }

  ALWAYS_INLINE uint32_t GetBucketIndex(hash_t hash) const {
    return hash & hash_mask_;
  }

  ALWAYS_INLINE size_t Size() const {
    return entry_pos_;
  }

  Value* Get(hash_t hash, const Key& key) {
    uint32_t index = table_[GetBucketIndex(hash)];
    while (index != kInvalidIndex) {
      auto& entry = entries_[index];
      if (entry.key_ == key) {
        return &entry.value_;
      }
      index = entry.next_;
    }
    return nullptr;
  }

private:
  // Chaining.
  hash_t hash_mask_;
  std::vector<uint32_t> table_;
  std::vector<HashEntry> entries_;
  size_t entry_pos_;
  static const uint32_t kInvalidIndex = 0xFFFFFFFF;
};