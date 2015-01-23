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

#ifndef _DICT_HPP_
#define _DICT_HPP_

#include <memory>
#include <numeric>
#include <unordered_set>

#include "Filter.hpp"

class SimpleDict : public ByteStreamFilter<16 * KB, 16 * KB> {
public:
	SimpleDict(Stream* stream) : ByteStreamFilter(stream), built_(false) {

	}

	virtual void forwardFilter(uint8_t* out, size_t* out_count, uint8_t* in, size_t* in_count) {
		if (!built_) {
			buildDictionary();
		}
		size_t count = std::min(*out_count, *in_count);
		std::copy(in, in + count, out);
		*in_count = *out_count = count;
	}
	virtual void reverseFilter(uint8_t* out, size_t* out_count, uint8_t* in, size_t* in_count) {
		size_t count = std::min(*out_count, *in_count);
		std::copy(in, in + count, out);
		*in_count = *out_count = count;
	}

private:
	typedef std::pair<size_t, std::string> WordCountPair;

	class EncodeEnry {
	public:
		std::string word;
		uint8_t code_chars;
		uint8_t char_a;
		uint8_t char_b;

		class HashCmp {
		public:
			size_t operator()(const EncodeEnry& a) {
				return std::hash<std::string>()(a.word);
			}
			bool operator()(const EncodeEnry& a, const EncodeEnry& b) {
				return a.word == b.word;
			}
		};
	};

	class EncodeDict {
	public:
		std::unordered_set<EncodeEnry, EncodeEnry::HashCmp, EncodeEnry::HashCmp> words_;
		void find(const std::string& s) {

		}
	};

	class Comparator {
	public:
		Comparator(size_t codes_required) {
			SetCodesRequired(codes_required);
		}
		void SetCodesRequired(size_t codes_required) {
			codes_required_ = codes_required;
		}
		size_t wordCost(const WordCountPair& a) const {
			if (a.second.length() <= codes_required_) {
				return 0;
			}
			const size_t savings = a.first * (a.second.length() - codes_required_);
			const size_t extra_cost = a.second.length() + 1 + codes_required_;
			if (extra_cost >= savings) {
				return 0;
			}
			return savings - extra_cost;
		}
		bool operator()(const WordCountPair& a, const WordCountPair& b) const {
			return wordCost(a) < wordCost(b);
		}

		size_t codes_required_;
	};

	bool isWordChar(int c) const {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
	}

	void buildDictionary() {
		auto stream_pos = stream_->tell();
		uint64_t counts[256] = { 0 };
		std::map<std::string, size_t> words;
		static const size_t kMaxWord = 255;
		uint8_t word_buffer[kMaxWord + 1];
		size_t pos = 0;
		uint64_t char_count = 0;
		BufferedStreamReader<4 * KB> reader(stream_);
		for (auto c = 'a'; c < 'z'; ++c) counts[static_cast<uint8_t>(c)] = std::numeric_limits<uint64_t>::max();
		for (auto c = 'A'; c < 'Z'; ++c) counts[static_cast<uint8_t>(c)] = std::numeric_limits<uint64_t>::max();
		for (;;) {
			int c = reader.get();
			if (c == EOF) {
				break;
			}
			if (c != 0 && isWordChar(c)) {
				if (pos >= kMaxWord) {
					pos = 0;
				}
				word_buffer[pos++] = c;
			} else {
				if (pos > 2) {
					words[std::string(word_buffer, word_buffer + pos)]++;
					char_count += pos;
					word_buffer[pos] = 0;
				}
				pos = 0;
			}
			++counts[static_cast<uint8_t>(c)];
		}

		std::vector<WordCountPair> sorted_words;
		for (auto it = words.begin(); it != words.end(); ++it) {
			// Remove all the words which hve less than 10 occurences.
			if (it->second >= 10) {
				sorted_words.push_back(std::make_pair(it->second, it->first));
			}
		}

		uint64_t total = std::accumulate(counts, counts + 256, 0U);
		// Calculate candidate code bytes.
		std::vector<std::pair<uint64_t, byte>> code_pairs;
		size_t escape_size = 0;
		for (size_t i = 0; i < 256; ++i) {
			if (counts[i] <= total / kDictFraction) {
				code_pairs.push_back(std::make_pair(counts[i], (byte)i));
				escape_size += counts[i];
			}
		}
		std::sort(code_pairs.begin(), code_pairs.end());

		size_t b1 = code_pairs.size() / 2; // One byte code words
		size_t b2 = (code_pairs.size() - b1) * code_pairs.size(); // Two byte code words
		
		Comparator c1(1);
		std::sort(sorted_words.rbegin(), sorted_words.rend(), c1);
		size_t save_total1 = 0;
		for (size_t i = 0; i < b1; ++i) {
			save_total1 += c1.wordCost(sorted_words[i]);
		}
		sorted_words.erase(sorted_words.begin(), sorted_words.begin() + 100);
		Comparator c2(2);
		std::sort(sorted_words.rbegin(), sorted_words.rend(), c2);
		size_t save_total2 = 0;
		for (size_t i = std::min(sorted_words.size(), b1);
			i < std::min(sorted_words.size(), b2); ++i) {
			save_total2 += c2.wordCost(sorted_words[i]);
		}
		std::cout << std::endl << code_pairs.size() << ":" << save_total1 << " " << save_total2 << " " << save_total1 + save_total2 << std::endl;

		stream_->seek(stream_pos);
		built_ = true;
	}
	bool built_;

	class Entry {
	public:
		uint32_t freq;
	};

	// Hash table.
	class Buckets {
	public:

	};

	// Entries.
	static const size_t kDictFraction = 1000;
	std::vector<Entry> entires;
};

#endif
