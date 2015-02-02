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

#include "LZ.hpp"

#include <fstream>

#ifdef WIN32
#include <intrin.h>
#endif

// #define USE_LZ4
#ifdef USE_LZ4
#include "lz4.h"
#endif

#include <mmintrin.h>

// Standard LZW.
template <bool use_range = true>
class LZW {
protected:
	// Compression data structure, a state machine with hash table + chaining for trasitions.
	class CompDict {
	public:
		class HashEntry {
		public:
			uint32_t key;
			uint32_t code;
			HashEntry* next;
		};
	private:
		// Chaining.
		hash_t hash_mask;
		std::vector<HashEntry*> hash_table;
		uint32_t code_count;
	public:
		void init(uint32_t size) {
			hash_mask = size - 1;
			hash_table.resize(hash_mask + 1);
			code_count = 256;
			for (auto& b : hash_table) b = nullptr;
		}
		
		// Obtain a new entry from ??
		HashEntry* newEntry() {
			return new HashEntry;
		}

		forceinline uint32_t getCodeCount() const {
			return code_count;
		}

		void add(hash_t hash, uint32_t key) {
			uint32_t index = getBucketIndex(hash);
			auto* new_bucket = newEntry();
			new_bucket->key = key;
			new_bucket->code = code_count++;
			new_bucket->next = hash_table[index];
			hash_table[index] = new_bucket;
		}

		forceinline uint32_t getBucketIndex(hash_t hash) const {
			return hash & hash_mask;
		}

		HashEntry* find(hash_t hash, uint32_t key) {
			auto* b = hash_table[getBucketIndex(hash)];
			for (;b != nullptr && b->key != key; b = b->next);
			return b;
		}
	};

	class DeCompDict {
	private:
		std::vector<uint32_t> decomp;
		uint32_t code_pos, base_add;
	public:
		forceinline uint32_t get(uint32_t index) const {
			return decomp[index - 256];
		}

		forceinline void nextBase() {
			++base_add;
		}

		forceinline uint32_t getLimit() const {
			return code_pos + base_add;
		}

		forceinline uint32_t size() const {
			return code_pos;
		}

		void init(uint32_t size) {
			code_pos = 0;
			base_add = 255;
			decomp.resize(size);
		}

		void add(uint32_t code_char) {
			decomp[code_pos++] = code_char;
		}
	};

	Range7 ent;
	uint32_t dict_size;
public:
	static const uint32_t version = 0;
	void setMemUsage(uint32_t n) {}

	void init() {
		dict_size = 1 << 16;
	}

	forceinline hash_t hashFunc(uint32_t a, hash_t b) {
		b += a;
		b += rotate_left(b, 9);
		return b ^ (b >> 6);
	}

	template <typename TOut, typename TIn>
	uint32_t compress(TOut& sout, TIn& sin) {
		init();
		if (use_range) {
			ent.init();
		}
		CompDict dict;
		dict.init(dict_size * 4);
		// Start out at the code for the first char.
		uint32_t code = sin.read();
		uint32_t prev_char = code;
		uint32_t ctx_char = 0, last_char = code;
		ProgressMeter meter;
		for (;;) {
			uint32_t c = sin.read();
			if (c == EOF) break;
			// Attempt to find a transition.
			uint32_t key = (code << 8) | c;
			hash_t hash = hashFunc(c, hashFunc(0x7D1BACD, code));
			auto* entry = dict.find(hash, key);
			if (entry != nullptr) {
				code = entry->code;
				last_char = c;
			} else {
				// Output code, add new code.
				writeCode(sout, code, dict.getCodeCount());
				if (dict.getCodeCount() < dict_size) {
					dict.add(hash, key);
				}
				last_char = code = c;
			}
			meter.addBytePrint(sout.getTotal());
		}
		writeCode(sout, code, dict.getCodeCount());
		if (use_range) {
			ent.flush(sout);
		}
		std::cout << std::endl;
		return (uint32_t)sout.getTotal();
	}

	template <typename TOut, typename TIn>
	bool decompress(TOut& sout, TIn& sin) {
		DeCompDict dict;
		dict.init(dict_size);
		dict.nextBase();
		init();
		if (use_range) {
			ent.initDecoder(sin);
		}
		ProgressMeter meter(false);
		uint32_t prev_code = readCode(sin, 256);
		if (!sin.eof()) {
			sout.write(prev_code);
			for (;;) {
				uint32_t limit = dict.getLimit();
				uint32_t code = readCode(sin, std::min(limit + 1, dict_size));
				if (sin.eof()) {
					break;
				}
				uint32_t output_code = (code == limit) ? prev_code : code;
				// Output the code so that we can get the first char if needed.
				byte buffer[32 * KB];
				uint32_t bpos = 0;
				while (output_code >= 256) {
					uint32_t entry = dict.get(output_code);
					buffer[bpos++] = (byte)entry;
					output_code = entry >> 8;
				}
				uint32_t first_char = output_code;
				buffer[bpos++] = (byte)first_char;
				while (bpos) {
					sout.write(buffer[--bpos]);
					meter.addBytePrint(sin.getTotal());
				}

				if (code == limit) {
					sout.write(first_char);
					meter.addBytePrint(sin.getTotal());
					dict.add((prev_code << 8) | first_char);
				} else {
					if (limit < dict_size) {
						dict.add((prev_code << 8) | first_char);
					}
				}
				prev_code = code;
			}
		}
		std::cout << std::endl;
		return true;
	}

	template <typename TOut>
	void writeCode(TOut& sout, uint32_t code, uint32_t max) {
		if (use_range) {
			ent.encodeDirect(sout, code, max);
		} else {
			sout.write(code >> 8);
			sout.write((byte)code);
		}
	}

	template <typename TIn>
	uint32_t readCode(TIn& sin, uint32_t max_code) {
		if (use_range) {
			return ent.decodeDirect(sin, max_code);
		} else {
			uint32_t code = (byte)sin.read();
			code <<= 8;
			code |= byte(sin.read());
			assert(code < max_code);
			return code;
		}
	}
};

class RLZW : public LZW<true> {
	static const uint32_t version = 0;
	void setMemUsage(uint32_t n) {}
		
	void init() {
		dict_size = 1 << 16;
	}

	forceinline hash_t hashFunc(uint32_t a, hash_t b) {
		b += a;
		b += rotate_left(b, 9);
		return b ^ (b >> 6);
	}

	template <typename TOut, typename TIn>
	uint32_t compress(TOut& sout, TIn& sin) {
		init();
		ent.init();
		CompDict dict[256], *cur_dict = nullptr;
		for (auto& d : dict) d.init(dict_size * 4);
		// Start out at the code for the first char.
		uint32_t code = sin.read();
		uint32_t prev_char = code;
		uint32_t ctx_char = 0, last_char = code;
		cur_dict = &dict[0];
		ProgressMeter meter;
		for (;;) {
			uint32_t c = sin.read();
			if (c == EOF) break;
			// Attempt to find a transition.
			uint32_t key = (code << 8) | c;
			hash_t hash = hashFunc(c, hashFunc(0x7D1BACD, code));
			auto entry = cur_dict->find(hash, key);
			if (entry != nullptr) {
				code = entry->code;
				last_char = c;
			} else {
				// Output code, add new code.
				ent.encodeDirect(sout, code, cur_dict->getCodeCount());
				if (cur_dict->getCodeCount() < dict_size) {
					cur_dict->add(hash, key);
				}
				//cur_dict = &dict[last_char];
				last_char = code = c;
			}
			meter.addBytePrint(sout.getTotal());
		}
		ent.encodeDirect(sout, code, cur_dict->getCodeCount());
		ent.flush(sout);
		std::cout << std::endl;
		return (uint32_t)sout.getTotal();
	}

	template <typename TOut, typename TIn>
	bool decompress(TOut& sout, TIn& sin) {
		DeCompDict dict[256], *cur_dict = nullptr, *prev_dict = nullptr;
		for (auto& d : dict) {
			d.init(dict_size);
		}
		init();
		ent.initDecoder(sin);
		ProgressMeter meter(false);
		uint32_t prev_code = 0;
		cur_dict = &dict[0];
		for (;;) {
			uint32_t limit = cur_dict->getLimit();
			uint32_t code = ent.decodeDirect(sin, std::min(limit + 1, dict_size));
			if (sin.eof()) {
				break;
			}
			if (limit == 255) {
				cur_dict->nextBase();
				sout.write(code);
				prev_code = code;
				//cur_dict = &dict[code];
				continue;
			}
			uint32_t output_code = (code == limit) ? prev_code : code;
			// Output the code so that we can get the first char if needed.
			byte buffer[32 * KB];
			uint32_t bpos = 0;
			while (output_code >= 256) {
				uint32_t entry = cur_dict->get(output_code);
				buffer[bpos++] = (byte)entry;
				output_code = entry >> 8;
			}
			uint32_t first_char = output_code;
			buffer[bpos++] = (byte)first_char;
			while (bpos) {
				sout.write(buffer[--bpos]);
				meter.addBytePrint(sin.getTotal());
			}

			if (code == limit) {
				sout.write(first_char);
				meter.addBytePrint(sin.getTotal());
				cur_dict->add((prev_code << 8) | first_char);
				//cur_dict = &dict[first_char];
			} else {
				if (limit < dict_size) {
					cur_dict->add((prev_code << 8) | first_char);
				}
				//cur_dict = &dict[buffer[0]];
				prev_dict = cur_dict;
			}
			prev_code = code;
		}
		std::cout << std::endl;
		return true;
	}
};

uint32_t LZ4::getMaxExpansion(uint32_t in_size) {
#ifdef USE_LZ4
	return LZ4_COMPRESSBOUND(in_size);
#else
	return 0;
#endif
}

uint32_t LZ4::compressBytes(byte* in, byte* out, uint32_t count) {
#ifdef USE_LZ4
	return LZ4_compress(reinterpret_cast<char*>(in), reinterpret_cast<char*>(out), count);
#else
	return 0;
#endif
}

void LZ4::decompressBytes(byte* in, byte* out, uint32_t count) {
#ifdef USE_LZ4
	LZ4_decompress_fast(reinterpret_cast<char*>(in), reinterpret_cast<char*>(out), count);
#endif
}

size_t MemoryLZ::getMatchLen(byte* m1, byte* m2, byte* limit1) {
	byte* start = m1;
	while (m1 < limit1) {
		uint32_t diff = *reinterpret_cast<uint32_t*>(m1) ^ *reinterpret_cast<uint32_t*>(m2);
		if (UNLIKELY(diff)) {
			unsigned long idx = 0;
			// TODO: Fix _BitScanForward(&idx, diff);
			m1 += idx >> 3;
			break;
		}
		m1 += sizeof(uint32_t);
		m2 += sizeof(uint32_t);
	}
	return m1 - start;
}

size_t LZFast::getMaxExpansion(size_t in_size) {
	return in_size + in_size / 16 + 100;
}

GreedyMatchFinder::GreedyMatchFinder() {
}

void GreedyMatchFinder::init(byte* in, const byte* limit) {
	in_ = in_ptr_ = in;
	limit_ = limit;
	hash_mask_ = 0x7FFFF;
	hash_storage_.resize(hash_mask_ + 1);
	for (auto& h : hash_storage_) {
		h.init();
	}
	hash_table_ = &hash_storage_[0];
}

Match GreedyMatchFinder::findNextMatch() {
	static const bool kPromoteMatches = true;
	const uint32_t kMaxHash = kMinMatch + 1;
	non_match_ptr_ = in_ptr_;
	const byte* match_ptr = nullptr;
	auto lookahead = *reinterpret_cast<const uint32_t*>(in_ptr_);
	size_t best_len = 0, best_dist = 0;
	while (true) {
		// uint32_t hash = (lookahead * 190807U) >> opt;
		uint32_t hash = (lookahead * 286474U) >> 13;
		const auto in_pos = static_cast<size_t>(in_ptr_ - in_);
		uint32_t pos = static_cast<uint32_t>(in_pos);	
		uint32_t index = hash & hash_mask_;
		uint32_t hash_word = Entry::buildWord(hash, kMinMatch);
		std::swap(hash_table_[index].pos_, pos);
		uint32_t old_pos = pos;
		std::swap(hash_table_[index].hash_, hash_word);
		uint32_t i = sizeof(lookahead);
		for (;;) {
			match_ptr = in_ + pos;
			auto dist = static_cast<size_t>(in_ptr_ - match_ptr);
			if (dist - kMinMatch <= kMaxDist) {
				if (*reinterpret_cast<const uint32_t*>(match_ptr) == lookahead) {
					// Improve the match.
					size_t len = sizeof(lookahead);
					const auto max_match = std::min(dist, static_cast<size_t>(limit_ - in_ptr_));
					while (len < max_match) {
						typedef unsigned long ulong;
						auto diff = *reinterpret_cast<const ulong*>(in_ptr_ + len) ^ *reinterpret_cast<const ulong*>(match_ptr + len);
						if (UNLIKELY(diff)) {
							ulong idx = 0;
							// TODO: Fix _BitScanForward(&idx, diff);
							len += idx >> 3;
							break;
						}
						len += sizeof(diff);
					}
					len = std::min(len, max_match);
					if (len > best_len) {
						best_len = len;
						best_dist = dist;
					}
				}
			}
			if (i >= kMaxHash) {
				break;
			}
			const uint32_t new_hash = hashFunc(in_ptr_[i++], hash);
			const uint32_t new_index = new_hash & hash_mask_;
			pos = hash_table_[new_index].pos_;
			if (!kPromoteMatches) {
				hash_table_[new_index].pos_ = static_cast<uint32_t>(in_pos);
			}
		}

		// Promote the match.
		if (kPromoteMatches) for (;;) {
			size_t dist = in_pos - old_pos;
			if (dist >= kMaxDist) {
				// Match too far, no need to promote.
				break;
			}
			uint32_t len = Entry::getLen(hash_word);
			uint32_t hash = Entry::getHash(hash_word, index);
			if (len >= kMaxHash) {
				break;
			}
			// Extend the match.
			hash = hashFunc(in_[old_pos + len++], hash);
			hash_word = Entry::buildWord(hash, len);
			index = hash & hash_mask_;
			std::swap(hash_table_[index].pos_, old_pos);
			std::swap(hash_table_[index].hash_, hash_word);
		}
		
		if (best_len >= kMinMatch) {
			non_match_len_ = in_ptr_ - non_match_ptr_;
			match_ptr = in_ptr_ - best_dist;
			assert(match_ptr + best_len <= in_ptr_);
			assert(in_ptr_ + best_len <= limit_);
			// Verify match.
			for (uint32_t i = 0; i < best_len; ++i) {
				assert(in_ptr_[i] == match_ptr[i]);
			}
			Match match;
			match.setDist(best_dist);
			match.setLength(best_len);
			in_ptr_ += best_len;
			return match;
		}
		lookahead = (lookahead >> 8) | (static_cast<uint32_t>(*(in_ptr_ + sizeof(lookahead))) << 24);
		if (++in_ptr_ >= limit_) {
			// assert(in_ptr_ == limit_);
			non_match_len_ = in_ptr_ - non_match_ptr_;
			break;
		}
	}
	return Match();
}

FastMatchFinder::FastMatchFinder() {
}

void MemoryMatchFinder::init(byte* in, const byte* limit) {
	in_ = in_ptr_ = in;
	limit_ = limit;
}

void FastMatchFinder::init(byte* in, const byte* limit) {
	MemoryMatchFinder::init(in, limit);
	hash_mask_ = 0xFFFF;
	hash_storage_.resize(hash_mask_ + 1, 0U);
	hash_table_ = &hash_storage_[0];
}

size_t LZFast::compressBytes(byte* in, byte* out, size_t count) {
	uint32_t matches[kMaxMatch + 1] = { 0 };
	const byte* limit = in + count;
	byte* out_ptr = out;
	match_finder_.init(in, limit - sizeof(uint32_t) * 4);
	match_finder_.opt = opt;
	if (kCountMatches) {
		non_matches_.resize(kMaxNonMatch + 1);
		std::fill(non_matches_.begin(), non_matches_.end(), 0U);
	}
	uint32_t dists[0x100] = { 0 };
	while (!match_finder_.done()) {
		Match match = match_finder_.findNextMatch();
		if (match.getLength()) {
			size_t dist = match.getDist() - kMinMatch;
			if (kCountMatches) ++dists[dist >> 8];
			auto len = match.getLength();
			assert(len >= kMinMatch);
			size_t non_match_len = match_finder_.getNonMatchLen();
			out_ptr = flushNonMatch(out_ptr, match_finder_.getNonMatchPtr(), non_match_len);
			// Encode one part of the match at a time, until its all encoded.
			while (len >= kMinMatch) {
				const auto cur_len = std::min(kMaxMatch, static_cast<size_t>(len));
				*out_ptr++ = static_cast<uint8_t>((cur_len - kMinMatch) ^ kMatchFlag);
				*reinterpret_cast<uint16_t*>(out_ptr) = static_cast<uint16_t>(dist);
				out_ptr += 2;
				len -= cur_len;
				if (kCountMatches) ++matches[cur_len];
			}
			match_finder_.backtrack(len);
		} else {
			// assert(match_finder_.done());
			break;
		}
	}
	out_ptr = flushNonMatch(out_ptr, match_finder_.getNonMatchPtr(), limit - match_finder_.getNonMatchPtr());\
	if (kCountMatches) {
		for (uint32_t i = 0; i < 0x100; ++i) {
			std::cout << i << " : " << dists[i] << std::endl;
		}
		uint32_t match_count = 0;
		for (uint32_t i = 0; i <= kMaxMatch; ++i) {
			if (matches[i] != 0) {
				std::cout << "Match " << i << " = " << matches[i] << std::endl;
				match_count += matches[i];
			}
		}
		for (uint32_t bits = 0; bits < 8; ++bits) {
			const uint32_t cur_max_nm = kMinNonMatch + (1U << bits) - 1;
			uint32_t non_match_count = 0, non_match_bytes = 0;
			uint64_t total_bits = 0;
			for (uint32_t i = 0; i < non_matches_.size(); ++i) {
				if (non_matches_[i] != 0) {
					total_bits += (static_cast<uint64_t>(1 + bits) * non_matches_[i] * ((i + cur_max_nm - 1) / cur_max_nm));
					non_match_count += non_matches_[i];
					non_match_bytes += i * non_matches_[i];
				}	
			}
			std::cout << "Match count " << match_count << " non count " << non_match_count << " non bytes " << non_match_bytes << " nb " << total_bits / 8 << std::endl;
		}
		for (uint32_t i = 0; i < non_matches_.size(); ++i) {
			if (non_matches_[i] != 0) {
				std::cout << "Nonmatch " << i << " = " << non_matches_[i] << std::endl;
			}
		}
	}
	return out_ptr - out;
}

static forceinline void safecopy(byte* no_alias out, const byte* no_alias in, const byte* out_limit) {
	do {
		*out++ = *in++;
	} while (out < out_limit);
}

forceinline byte* LZFast::flushNonMatch(byte* out_ptr, const byte* in_ptr, size_t non_match_len) {
	while (non_match_len > 0) {
		size_t len = std::min(kMaxNonMatch, non_match_len);
		*out_ptr++ = len - kMinNonMatch;
		fastcopy<uint64_t>(out_ptr, in_ptr, in_ptr + len);
		in_ptr += len;
		out_ptr += len;
		non_match_len -= len;
		if (kCountMatches) {
			non_matches_[len] += len != 0;
		}
	}
	return out_ptr;
}

/* Encoding format.
 * 1 byte (bit 1 = match, 0 = no match).
 * 32 bits = 4 bits per -> length
 * 2x bytes distance for matches.
 */

template<uint32_t pos>
ALWAYS_INLINE byte* LZFast::processMatch(byte matches, uint32_t lengths, byte* out, byte** in) {
	uint32_t len = (lengths >> (pos * 4)) & 0xF;
	if (matches & (1 << pos)) {
		len += kMinMatch;
		const uint32_t dist = *reinterpret_cast<uint16_t*>(*in) + kMinMatch;
		*in += 2;
		memcpy16unsafe(out, out - dist, out + len);
	} else {
		len += kMinNonMatch;
		memcpy16unsafe(out, *in, out + len);
		*in += len;
	}
	return out + len;
}

void LZFast::decompressBytes(byte* in, byte* out, size_t count) {
	if (!count) {
		return;
	}
	byte *start = out, *limit = out + count;
	static const uint32_t kCopyBufferSize = 4;
	while (true) {
#if 1
		auto win = *reinterpret_cast<uint32_t*>(in);
		uint32_t len = win & 0x7FU;
		uint32_t flag = win & kMatchFlag;
		byte* copy_src;
		if (LIKELY(flag)) {
			len += kMinMatch;
			ASSUME(len <= kMaxMatch);
			auto dist = static_cast<uint16_t>(win >> 8) + kMinMatch;
			in += 3;
			copy_src = out - dist;
			if (kMaxMatch <= 16) {
				copy16bytes(out, copy_src, nullptr); 
			} else {
				memcpy16unsafe(out, copy_src, out + len);
			}
			out += len;
		} else {
			len += kMinNonMatch;
			ASSUME(len <= kMaxNonMatch);
			in += 1;
			copy_src = in;
			in += len;
			out += len;
			if (out >= limit) {
				safecopy(out - len, copy_src, out);
				break;
			}
			if (kMaxNonMatch <= 16) {
				copy16bytes(out - len, copy_src, nullptr); 
			} else {
				memcpy16unsafe(out - len, copy_src, out);
			}
		}
#else
		byte matches = *in++;
		uint32_t lengths = *reinterpret_cast<uint32_t*>(in);
		in += sizeof(lengths);
		out = processMatch<0>(matches, lengths, out, &in);
		out = processMatch<1>(matches, lengths, out, &in);
		out = processMatch<2>(matches, lengths, out, &in);
		out = processMatch<3>(matches, lengths, out, &in);
		out = processMatch<4>(matches, lengths, out, &in);
		out = processMatch<5>(matches, lengths, out, &in);
		out = processMatch<6>(matches, lengths, out, &in);
		out = processMatch<7>(matches, lengths, out, &in);
#endif
	}
}

size_t VRolz::getMaxExpansion(size_t in_size) {
	return in_size * 4 / 3;
}

uint32_t VRolz::getMatchLen(byte* m1, byte* m2, uint32_t max_len) {
	uint32_t len = 0;
	while (len <= max_len && m1[len] == m2[len]) {
		++len;
	}
	return len;
}

size_t VRolz::compressBytes(byte* in, byte* out, size_t count) {
	// Match encoding format.
	// 2 bits index (0 = char, 1 = rolz1, 2 = rolz2, 3 = rolz3??
	// <char> | 3 bits index, 5 bits len.

	// Skip the first 4 bytes for good measure.
	byte* out_ptr = out;
	byte* in_ptr = in;
	byte* limit = in + count;
	*out_ptr++= 0;
	for (uint32_t i = 0; i < 4; ++i) {
		if (in_ptr < limit) {
			*out_ptr++ = *in_ptr++;
		}
	}
	uint32_t bpos = 0;
	byte cmatch = 0;
	byte buffer[4];
	while (in_ptr < limit) {
		uint32_t prev = *reinterpret_cast<uint32_t*>(in_ptr - sizeof(uint32_t));
		// Try the tables.
		uint32_t best_match = 0;
		uint32_t best_offset = *in_ptr;
		uint32_t best_len = 1;
		uint32_t max_match = std::min(static_cast<uint32_t>(limit - in_ptr), kMaxMatch);
		auto& ord1 = order1[prev >> 24];
		for (uint32_t i = 0; i < ord1.size(); ++i) {
			auto* match_ptr = in + ord1[i];
			uint32_t len = getMatchLen(in_ptr, match_ptr, std::min(max_match, static_cast<uint32_t>(in - match_ptr)));
			if (len > best_len) {
				best_match = 1;
				best_len = len;
				best_offset = i;
			}
		}
		auto& ord2 = order2[prev >> 16];
		for (uint32_t i = 0; i < ord2.size(); ++i) {
			auto* match_ptr = in + ord2[i];
			uint32_t len = getMatchLen(in_ptr, match_ptr, std::min(max_match, static_cast<uint32_t>(in - match_ptr)));
			if (len > best_len) {
				best_match = 2;
				best_len = len;
				best_offset = i;
			}
		}
		if (best_len >= kMinMatch) {
			// Encode match
			in_ptr += best_len;
			cmatch = (cmatch << 2) | best_match;
			buffer[bpos++] = ((best_len - kMinMatch) << 3) | best_offset;
		} else {
			cmatch <<= 2;
			addHash(in, in_ptr - in, prev);
			buffer[bpos++] = *in_ptr++;
		}
		if (bpos >= 4) {
			*out_ptr++ = cmatch;
			for (uint32_t i = 0; i < bpos; ++i) {
				*out_ptr++ = buffer[i];
			}
			cmatch = 0;
			bpos = 0;
		}
	}
	return out_ptr - out;
}

void VRolz::addHash(byte* in, uint32_t pos, uint32_t prev) {
	uint32_t old1 = order1[prev >> 24].add(pos);
	// Extend old 1 into order 2 match.
	if (old1 > sizeof(uint32_t)) {
		prev = *reinterpret_cast<uint32_t*>(in + old1 - sizeof(uint32_t));
		order2[prev >> 16].add(old1);
	}
}

void VRolz::decompressBytes(byte* in, byte* out, size_t count) {
	return;
}
