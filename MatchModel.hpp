#ifndef _MATCH_MODEL_HPP_
#define _MATCH_MODEL_HPP_

#include "Memory.hpp"

template <typename Model>
class MatchModel {
public:
	static const size_t min_match = 6; // TODO: Tweak this??????
	static const size_t small_match = 8;
	static const size_t max_match = 80;
	static const size_t char_shift = 2;
	static const size_t mm_shift = 2;
	static const size_t mm_round = (1 << mm_shift) - 1;
	static const size_t max_value = 1 << 12;
private:
	static const size_t bits_per_char = 8;
	static const size_t num_length_models = ((max_match - min_match + 2 + 2 * mm_round) >> mm_shift) * bits_per_char;
	Model models[(256 >> char_shift) * num_length_models], *model_base; // Bits expected 0 x 8, 1 x 8.

	// Current minimum match
	size_t cur_min_match;

	// Current match.
	size_t pos, len;

	// Hash table
	size_t hash_mask;
	MemMap hash_storage;
	size_t* hash_table;
	Model* cur_mdl;
	size_t expected_code, prev_char;
	static const size_t code_bit_shift = sizeof(size_t) * 8 - 1;

	// Hashes.
	hash_t h0, h1, h2, h3;
public:
	void resize(size_t size) {
		hash_mask = size - 1;
		// Check power of 2.
		assert((hash_mask & (hash_mask + 1)) == 0);
		hash_storage.resize((hash_mask + 1) * sizeof(size_t));
		hash_table = (size_t*)hash_storage.getData();
	}

	forceinline int getP(const short* no_alias st) {
		if (!len) return 0;;
		int p = st[cur_mdl->getP()];
		int bit = expected_code >> code_bit_shift;
		return bit ? -p : p;
	}

	void init() {
		h0 = 0x99721245;
		h1 = 0xDFED1353;
		h2 = 0x22354235;
		h3 = 0x67777349;
		cur_min_match = min_match;
		expected_code = 0;
		pos = len = 0;
		for (auto& m : models) m.init();
		for (size_t c = 0;c < (256 >> char_shift);++c) {
			setPrevChar(c << char_shift);
			for (size_t i = 0;i < num_length_models;++i) {
				size_t index = i / bits_per_char;
				size_t len = min_match + (index << mm_shift);
				model_base[i].setP((max_value / 2) / len); 
			}
		}
		setPrevChar(254);
		updateCurMdl();
	}

	forceinline size_t getLength() const {
		return len;
	}

	forceinline void resetMatch() {
		len = 0;
	}

	forceinline void setPrevChar(size_t c) {
		prev_char = c;
		size_t base = prev_char >> char_shift;
		model_base = &models[base * num_length_models];
	}
			
	void search(SlidingWindow2<byte>& buffer, size_t spos) {
		// Reverse match.
		size_t blast = buffer.getPos() - 1;
		size_t len = sizeof(size_t);
		if (LIKELY(*(size_t*)&buffer[spos - len] == *(size_t*)&buffer[blast - len])) {
			for (; buffer[spos - len] == buffer[blast - len] && len < max_match; ++len);
			if (len >= cur_min_match && len > getLength()) {
				// Update our match.
				this->pos = spos;
				this->len = len;
			}
		}
	}

	forceinline void setHash(hash_t new_h1, size_t new_min_match = min_match) {
		h0 = new_h1;
		// cur_min_match = new_min_match;
		assert(cur_min_match >= min_match);
	}
	
	static forceinline hash_t hashFunc(size_t c, hash_t h) {
		h += c;
		h += rotate_left(h, 10);
		return h ^ (h >> 6);
	}

	void update(SlidingWindow2<byte>& buffer) {
		const auto blast = buffer.getPos() - 1;
		auto bmask = buffer.getMask();
		setPrevChar(buffer(blast & bmask));

		// Update hashes.
		h3 = hashFunc(prev_char, h2); // order n + 2
		h2 = hashFunc(prev_char, h1); // order n + 1
		h1 = hashFunc(prev_char, h0); // order n

		// Update the existing match.
		if (!len) {
			auto& b1 = hash_table[h1 & hash_mask];
			if (!((b1 ^ h1) & ~bmask)) {
				search(buffer, b1);
			}
			if (len < small_match) {
				auto& b2 = hash_table[h3 & hash_mask];
				if (!((b2 ^ h3) & ~bmask)) {
					search(buffer, b2);
				}
				b2 = (blast & bmask) | (h3 & ~bmask);
			} else
				b1 = (blast & bmask) | (h1 & ~bmask);
		} else {
			len += len < max_match;
			++pos;
		}
		updateCurMdl();
	}

	void updateCurMdl() {
		if (len) {
			cur_mdl = model_base + bits_per_char * ((len - min_match) >> 2);
		}
	}

	forceinline size_t getExpectedChar(SlidingWindow2<byte>& buffer) const {
		return buffer[pos + 1];
	}

	void updateExpectedCode(size_t code, size_t bit_len = 8) {
		expected_code = code << (code_bit_shift - bit_len + 1);
	}

	forceinline void updateBit(size_t bit) {
		if (len) {
			size_t diff = (expected_code >> code_bit_shift) ^ bit;
			(cur_mdl++)->update(diff);
			expected_code <<= 1;
			len &= -(1 ^ diff);
		}
	}
};

#endif
