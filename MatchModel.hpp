#ifndef _MATCH_MODEL_HPP_
#define _MATCH_MODEL_HPP_

#include "Memory.hpp"

template <typename Model>
class MatchModel {
public:
	static const size_t kMinMatch = 4; // TODO: Tweak this??????
	static const size_t small_match = 8;
	static const size_t kCharShift = 2;
	static const size_t kCharMax = 256U >> kCharShift;
	static const size_t mm_shift = 2;  // WTF DIS 4.
	static const size_t max_value = 1 << 12;
	static const size_t kMaxCtxLen = 32;
	static const bool kMultiMatch = false;
	static const bool kExtendMatch = false;
private:
	static const size_t bits_per_char = 16;
	std::vector<Model> models;
	Model* model_base; // Bits expected 0 x 8, 1 x 8.

	// Current minimum match
	size_t cur_min_match;
	size_t cur_max_match;
	size_t dist;

	// Current match.
	size_t pos, len;

	size_t max_bits_per_char_;
	size_t num_length_models_;

	// Hash table
	uint32_t hash_mask;
	MemMap hash_storage;
	uint32_t* hash_table;
	Model* cur_mdl;
	uint32_t expected_code, prev_char;
	static const uint32_t code_bit_shift = sizeof(uint32_t) * 8 - 1;
public:
	typedef CyclicBuffer<uint8_t> Buffer;
	uint32_t opt_var;

	MatchModel() : opt_var(0) {

	}

	void setOpt(uint32_t var) {
		opt_var = var;
	}

	void resize(uint32_t size) {
		hash_mask = size - 1;
		// Check power of 2.
		assert((hash_mask & (hash_mask + 1)) == 0);
		hash_storage.resize((hash_mask + 1) * sizeof(uint32_t));
		hash_table = (uint32_t*)hash_storage.getData();
	}

	forceinline int getP(const short* st) {
		if (!len) return 0;
		int p = st[cur_mdl->getP()];
		int bit = getExpectedBit();
		return bit ? -p : p;
	}

	forceinline size_t getPos() const {
		return pos;
	}

	forceinline uint32_t getExpectedBit() const {
		return expected_code >> code_bit_shift;
	}

	forceinline size_t getMinMatch() const {
		return kMinMatch;
	}

	void init(size_t min_match, size_t max_match, size_t max_bits_per_char = 8) {
		max_bits_per_char_ = max_bits_per_char;
		cur_max_match = max_match;
		num_length_models_ = (cur_max_match + 1) * max_bits_per_char_;
		models.resize(kCharMax * num_length_models_);
		cur_min_match = min_match;
		expected_code = 0;
		pos = len = dist = 0;
		for (auto& m : models) m.init();
		for (size_t c = 0; c < kCharMax; ++c) {
			setPrevChar(c << kCharShift);
			for (size_t i = 0; i < num_length_models_; ++i) {
				const size_t index = i / bits_per_char;
				const size_t len = kMinMatch + (index << mm_shift);
				model_base[i].setP((max_value / 2) / len); 
			}
		}
		setPrevChar(0);
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
		const size_t base = prev_char >> kCharShift;
		// size_t base = prev_char >> (kCharShift + 1);
		// base = base * 2 + (dist <= 0x32 * 104);
		model_base = &models[base * num_length_models_];
	}
			
	void search(Buffer& buffer, uint32_t spos) {
		// Reverse match.
		uint32_t blast = buffer.getPos() - 1;
		uint32_t len = sizeof(uint32_t);
		if (*reinterpret_cast<uint32_t*>(&buffer[spos - len]) ==
			*reinterpret_cast<uint32_t*>(&buffer[blast - len])) {
			--spos;
			--blast;
			if (kExtendMatch) {
				for (; len < cur_min_match; ++len) {
					if (buffer[spos - len] != buffer[blast - len]) {
						return;
					}
				}
				for (;buffer[spos - len] == buffer[blast - len] && len < cur_max_match; ++len);
			}
			// Update our match.
			const size_t bmask = buffer.getMask();
			dist = (blast & bmask) - (spos & bmask);
			this->pos = spos + 1;
			this->len = len;
		}
	}

	void update(Buffer& buffer, uint32_t h) {
		const auto blast = buffer.getPos() - 1;
		const auto bmask = buffer.getMask();
		const auto last_pos = blast & bmask;
		setPrevChar(buffer(last_pos));
		// Update the existing match.
		auto& b1 = hash_table[h & hash_mask];
		if (!len) {
			search(buffer, b1);
			// b1 = last_pos;
		} else {
			len += len < cur_max_match;
			++pos;
		}
		b1 = last_pos;
		updateCurMdl();
	}

	void updateCurMdl() {
		if (len) {
			cur_mdl = model_base + bits_per_char * std::min(len - kMinMatch, kMaxCtxLen);
		}
	}

	forceinline uint32_t getExpectedChar(Buffer& buffer) const {
		return buffer[pos + 1];
	}

	void updateExpectedCode(uint32_t code, uint32_t bit_len = 8) {
		expected_code = code << (code_bit_shift - bit_len + 1);
	}

	forceinline void updateBit(uint32_t bit) {
		if (len) {
			uint32_t diff = (expected_code >> code_bit_shift) ^ bit;
			cur_mdl->update(diff);
			len &= -(1 ^ diff);
			expected_code <<= 1;
			cur_mdl++;
		}
	}
};

#endif
