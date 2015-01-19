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

#include "Filter.hpp"

class Dict : public ByteStreamFilter<16 * KB, 16 * KB> {
public:
	Dict(Stream* stream) : ByteStreamFilter(stream), built_(false) {

	}

	virtual void forwardFilter(uint8_t* out, size_t* out_count, uint8_t* in, size_t* in_count) {
		if (!built_) {
			buildDictionary();
		}
	}
	virtual void reverseFilter(uint8_t* out, size_t* out_count, uint8_t* in, size_t* in_count) {
	}

	void build() {

	}

	void buildCodes() {
		for (;;) {

		}
	}

private:
	void buildDictionary() {
		auto stream_pos = stream_->tell();
		uint64_t counts[256] = { 0 };
		std::map<std::string, size_t> words;
		static const size_t kMaxWord = 4096;
		uint8_t word_buffer[kMaxWord + 1];
		size_t pos = 0;
		uint64_t char_count = 0;
		BufferedStreamReader<4 * KB> reader(stream_);
		for (;;) {
			int c = reader.get();
			if (c == EOF) {
				break;
			}
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
				word_buffer[pos++] = c;
				if (pos == kMaxWord) {
					pos = 0;;
				}
			} else {
				if (pos > 2) {
					words[std::string(word_buffer, word_buffer + pos)]++;
					char_count += pos;
					word_buffer[pos] = 0;
				}
				pos = 0;
			}
			++counts[(uint8_t)c];
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

		std::vector<std::pair<size_t, std::string>> sorted_words;
		for (auto it = words.begin(); it != words.end(); ++it) {
			// Remove all the words which hve less than 10 occurences.
			if (it->second >= 10) {
				sorted_words.push_back(std::make_pair(it->second * (it->first.length() - 1), it->first));
			}
		}
		std::sort(sorted_words.rbegin(), sorted_words.rend());
		size_t save_total1 = 0;
		for (size_t i = 0; i < 100; ++i) {
			size_t num_words = sorted_words[i].first / (sorted_words[i].second.length() - 1);
			save_total1 += sorted_words[i].first - num_words * 0;
		}
		size_t save_total2 = 0;
		for (size_t i = 100; i < 100 * 100; ++i) {
			size_t num_words = sorted_words[i].first / (sorted_words[i].second.length() - 1);
			save_total2 += sorted_words[i].first - num_words * 1;
		}
		std::cout << std::endl << save_total1 << " " << save_total2 << " " << save_total1 + save_total2 << std::endl;

		// Remove anything codes over 60.
		// Reset.

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
	static const size_t kDictFraction = 2000;
	std::vector<Entry> entires;
};

#endif
