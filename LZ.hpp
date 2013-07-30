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

#ifndef _LZ_HPP_
#define _LZ_HPP_

#include "BoundedQueue.hpp"
#include "CyclicBuffer.hpp"
#include "Model.hpp"
#include "Range.hpp"

// Standard LZW.
template <bool use_range = true>
class LZW {
protected:
	// Compression data structure, a state machine with hash table + chaining for trasitions.
	class CompDict {
	public:
		class HashEntry {
		public:
			size_t key;
			size_t code;
			HashEntry* next;
		};
	private:
		// Chaining.
		hash_t hash_mask;
		std::vector<HashEntry*> hash_table;
		size_t code_count;
	public:
		void init(size_t size) {
			hash_mask = size - 1;
			hash_table.resize(hash_mask + 1);
			code_count = 256;
			for (auto& b : hash_table) b = nullptr;
		}
		
		// Obtain a new entry from ??
		HashEntry* newEntry() {
			return new HashEntry;
		}

		forceinline size_t getCodeCount() const {
			return code_count;
		}

		void add(hash_t hash, size_t key) {
			size_t index = getBucketIndex(hash);
			auto* new_bucket = newEntry();
			new_bucket->key = key;
			new_bucket->code = code_count++;
			new_bucket->next = hash_table[index];
			hash_table[index] = new_bucket;
		}

		forceinline size_t getBucketIndex(hash_t hash) const {
			return hash & hash_mask;
		}

		HashEntry* find(hash_t hash, size_t key) {
			auto* b = hash_table[getBucketIndex(hash)];
			for (;b != nullptr && b->key != key; b = b->next);
			return b;
		}
	};

	class DeCompDict {
	private:
		std::vector<size_t> decomp;
		size_t code_pos, base_add;
	public:
		forceinline size_t get(size_t index) const {
			return decomp[index - 256];
		}

		forceinline void nextBase() {
			++base_add;
		}

		forceinline size_t getLimit() const {
			return code_pos + base_add;
		}

		forceinline size_t size() const {
			return code_pos;
		}

		void init(size_t size) {
			code_pos = 0;
			base_add = 255;
			decomp.resize(size);
		}

		void add(size_t code_char) {
			decomp[code_pos++] = code_char;
		}
	};

	Range7 ent;
	size_t dict_size;
public:
	static const size_t version = 0;
	void setMemUsage(size_t n) {}

	void init() {
		dict_size = 1 << 16;
	}

	forceinline hash_t hashFunc(size_t a, hash_t b) {
		b += a;
		b += rotate_left(b, 9);
		return b ^ (b >> 6);
	}

	template <typename TOut, typename TIn>
	size_t Compress(TOut& sout, TIn& sin) {
		init();
		if (use_range) {
			ent.init();
		}
		CompDict dict;
		dict.init(dict_size * 4);
		// Start out at the code for the first char.
		size_t code = sin.read();
		size_t prev_char = code;
		size_t ctx_char = 0, last_char = code;
		ProgressMeter meter;
		for (;;) {
			size_t c = sin.read();
			if (c == EOF) break;
			// Attempt to find a transition.
			size_t key = (code << 8) | c;
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
		return (size_t)sout.getTotal();
	}

	template <typename TOut, typename TIn>
	bool DeCompress(TOut& sout, TIn& sin) {
		DeCompDict dict;
		dict.init(dict_size);
		dict.nextBase();
		init();
		if (use_range) {
			ent.initDecoder(sin);
		}
		ProgressMeter meter(false);
		size_t prev_code = readCode(sin, 256);
		if (!sin.eof()) {
			sout.write(prev_code);
			for (;;) {
				size_t limit = dict.getLimit();
				size_t code = readCode(sin, std::min(limit + 1, dict_size));
				if (sin.eof()) {
					break;
				}
				size_t output_code = (code == limit) ? prev_code : code;
				// Output the code so that we can get the first char if needed.
				byte buffer[32 * KB];
				size_t bpos = 0;
				while (output_code >= 256) {
					size_t entry = dict.get(output_code);
					buffer[bpos++] = (byte)entry;
					output_code = entry >> 8;
				}
				size_t first_char = output_code;
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
	void writeCode(TOut& sout, size_t code, size_t max) {
		if (use_range) {
			ent.encodeDirect(sout, code, max);
		} else {
			sout.write(code >> 8);
			sout.write((byte)code);
		}
	}

	template <typename TIn>
	size_t readCode(TIn& sin, size_t max_code) {
		if (use_range) {
			return ent.decodeDirect(sin, max_code);
		} else {
			size_t code = (byte)sin.read();
			code <<= 8;
			code |= byte(sin.read());
			assert(code < max_code);
			return code;
		}
	}
};

class RLZW : public LZW<true> {
	static const size_t version = 0;
	void setMemUsage(size_t n) {}
		
	void init() {
		dict_size = 1 << 16;
	}

	forceinline hash_t hashFunc(size_t a, hash_t b) {
		b += a;
		b += rotate_left(b, 9);
		return b ^ (b >> 6);
	}

	template <typename TOut, typename TIn>
	size_t Compress(TOut& sout, TIn& sin) {
		init();
		ent.init();
		CompDict dict[256], *cur_dict = nullptr;
		for (auto& d : dict) d.init(dict_size * 4);
		// Start out at the code for the first char.
		size_t code = sin.read();
		size_t prev_char = code;
		size_t ctx_char = 0, last_char = code;
		cur_dict = &dict[0];
		ProgressMeter meter;
		for (;;) {
			size_t c = sin.read();
			if (c == EOF) break;
			// Attempt to find a transition.
			size_t key = (code << 8) | c;
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
		return (size_t)sout.getTotal();
	}

	template <typename TOut, typename TIn>
	bool DeCompress(TOut& sout, TIn& sin) {
		DeCompDict dict[256], *cur_dict = nullptr, *prev_dict = nullptr;
		for (auto& d : dict) {
			d.init(dict_size);
		}
		init();
		ent.initDecoder(sin);
		ProgressMeter meter(false);
		size_t prev_code = 0;
		cur_dict = &dict[0];
		for (;;) {
			size_t limit = cur_dict->getLimit();
			size_t code = ent.decodeDirect(sin, std::min(limit + 1, dict_size));
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
			size_t output_code = (code == limit) ? prev_code : code;
			// Output the code so that we can get the first char if needed.
			byte buffer[32 * KB];
			size_t bpos = 0;
			while (output_code >= 256) {
				size_t entry = cur_dict->get(output_code);
				buffer[bpos++] = (byte)entry;
				output_code = entry >> 8;
			}
			size_t first_char = output_code;
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

class LZ {
	BoundedQueue<byte> lookahead;
	CyclicBuffer<byte> buffer;

	void tryMatch(size_t& pos, size_t& len) {
		
	}

	void update(byte c) {
		buffer.push(c);
	}
public:
	static const size_t version = 0;
	void setMemUsage(size_t n) {}

	typedef safeBitModel<unsigned int, 12> BitModel;
	typedef bitContextModel<BitModel, 1 << 8> CtxModel;

	Range7 ent;
	CtxModel mdl;

	void init() {
		mdl.init();
	}

	template <typename TOut, typename TIn>
	size_t Compress(TOut& sout, TIn& sin) {
		init();
		ent.init();
		for (;;) {
			int c = sin.read();
			if (c == EOF) break;
			mdl.encode(ent, sout, c);
		}
		ent.flush(sout);
		return (size_t)sout.getTotal();
	}

	template <typename TOut, typename TIn>
	bool DeCompress(TOut& sout, TIn& sin) {
		init();
		ent.initDecoder(sin);
		for (;;) {
			int c = mdl.decode(ent, sin);
			if (sin.eof()) break;
			sout.write(c);
		}
		return true;
	}
};

#endif
