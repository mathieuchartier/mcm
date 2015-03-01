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

class DictBuilder {
public:
};

class SimpleDict : public ByteStreamFilter<16 * KB, 16 * KB> {
	static const size_t kMaxWordLen = 255;
public:
	SimpleDict(Stream* stream, const bool verbose = true || kIsDebugBuild, size_t min_occurences = 10)
		: ByteStreamFilter(stream), built_(false), verbose_(verbose), min_occurences_(min_occurences) {
	}

	virtual void forwardFilter(uint8_t* out, size_t* out_count, uint8_t* in, size_t* in_count) {
		if (false) {
			size_t count = std::min(*out_count, *in_count);
			std::copy(in, in + count, out);
			*in_count = *out_count = count;
			return;
		}
		if (!built_) {
			buildDict();
			built_ = true;
		}
		// TODO: Write the dictionary if required.
		uint8_t* in_ptr = in;
		uint8_t* out_ptr = out;
		uint8_t* in_limit = in + *in_count;
		uint8_t* out_limit = out + *out_count;
		if (dict_pos_ < dict_buffer_.size()) {
			size_t max_cpy = std::min(static_cast<size_t>(out_limit - out_ptr), dict_buffer_.size() - dict_pos_);
			std::copy(&dict_buffer_[0] + dict_pos_, &dict_buffer_[0] + dict_pos_ + max_cpy, out_ptr);
			out_ptr += max_cpy;
			dict_pos_ += max_cpy;
		}
		size_t word_pos = 0;
		char word_buffer[kMaxWordLen + 1];
		while (in_ptr < in_limit) {
			if (out_ptr + 2 >= out_limit) break;
			uint8_t c;
			if (in_ptr + word_pos < in_limit && word_pos < kMaxWordLen && isWordChar(c = in_ptr[word_pos])) {
				word_buffer[word_pos++] = c;
			} else {
				if (word_pos) {
					for (size_t len = word_pos; len >= 3; --len) {
						std::string s(word_buffer, word_buffer + len);
						word_pos = 0;
						auto it1 = one_byte_map_.find(s);
						if (it1 != one_byte_map_.end()) {
							*(out_ptr++) = it1->second.c1_;
							in_ptr += s.length();
							goto cont;
						}
						auto it2 = two_byte_map_.find(s);
						if (it2 != two_byte_map_.end()) {
							*(out_ptr++) = it2->second.c1_;
							*(out_ptr++) = it2->second.c2_;
							in_ptr += s.length();
							goto cont;
						}
					}
				}
				c = *(in_ptr++);
				if (is_char_codes_[c]) {
					*(out_ptr++) = escape_char_;
				}
				*(out_ptr++) = c;
				cont:;
			}
		}
		dcheck(in_ptr <= in_limit);
		dcheck(out_ptr <= out_limit);
		*in_count = in_ptr - in;
		*out_count = out_ptr - out;
	}
	virtual void reverseFilter(uint8_t* out, size_t* out_count, uint8_t* in, size_t* in_count) {
		size_t count = std::min(*out_count, *in_count);
		std::copy(in, in + count, out);
		*in_count = *out_count = count;
	}
	void setOpt(size_t n) {}
	void dumpInfo() {
	}

private:
	typedef std::pair<size_t, std::string> WordCountPair;

	struct DictEntry {
		DictEntry(uint8_t c1 = 0, uint8_t c2 = 0) : c1_(c1), c2_(c2) {}
		uint8_t c1_, c2_;
	};

	void buildDict() {
		dict_pos_ = 0;
		if (verbose_) std::cout << std::endl;
		auto stream_pos = stream_->tell();
		uint64_t counts[256] = { 0 };
		std::map<std::string, size_t> words;
		uint8_t word_buffer[kMaxWordLen + 1];
		size_t pos = 0;
		BufferedStreamReader<4 * KB> reader(stream_);
		for (;;) {
			const int c = reader.get();
			if (c == EOF) break;
			if (isWordChar(c)) {
				word_buffer[pos++] = c;
				if (pos >= kMaxWordLen) {
					pos = 0;
				}
			} else {
				if (pos > 3) {
					++words[std::string(word_buffer, word_buffer + pos)];
				}
				pos = 0;
			}
			++counts[static_cast<uint8_t>(c)];
		}

		std::vector<WordCountPair> sorted_words;
		for (auto it = words.begin(); it != words.end(); ++it) {
			// Remove all the words which hve less than 10 occurences.
			if (it->second >= min_occurences_) {
				sorted_words.push_back(std::make_pair(it->second, it->first));
			}
		}
		words.clear();

		uint64_t total = std::accumulate(counts, counts + 256, 0U);
		// Calculate candidate code bytes.
		char_codes_.clear();
		uint64_t escape_size = 0;
		std::vector<std::pair<uint64_t, byte>> code_pairs;
		for (size_t i = 0; i < 256; ++i) {
			code_pairs.push_back(std::make_pair(counts[i], static_cast<byte>(i)));
		}
		std::sort(code_pairs.begin(), code_pairs.end());
		size_t idx = 0;
		std::cout << std::endl;
		for (auto& p : code_pairs) {
			std::cout << idx++ << " b=" << static_cast<size_t>(p.second) << ":" << p.first << std::endl;
		}	
#if 0
		for (size_t i = 0; i < 170; ++i) {
			char_codes_.push_back(code_pairs[i].second);
		}
#else
		for (size_t i = 128; i < 256; ++i) {
			char_codes_.push_back(i);
		}
#endif
		for (auto& c : char_codes_) {
			escape_size += counts[c];
		}
		// TODO: Properly calculate the escape char?
		escape_char_ = char_codes_.back();
		char_codes_.pop_back();

		Comparator c1(1), c2(2);

		size_t optimal_b1 = char_codes_.size() / 2;
		uint64_t optimal_save = 0;
		if (verbose_) std::cout << "Word size= " << sorted_words.size() << std::endl;
		// b1 is number of 1 byte codes.
		if (!kIsDebugBuild)
		for (size_t b1 = 0; b1 <= char_codes_.size(); ++b1) {
			std::vector<WordCountPair> new_words = sorted_words;
			// b2 is the number of two byte codes.
			const size_t b2 = (char_codes_.size() - b1) * char_codes_.size(); // Two byte code words
		
			std::sort(new_words.rbegin(), new_words.rend(), c1);
			uint64_t save_total1 = 0;
			for (size_t i = 0; i < b1; ++i) {
				save_total1 += c1.wordCost(new_words[i]);
			}
			// Erase the saved 1 byte words.
			new_words.erase(new_words.begin(), new_words.begin() + b1);

			// Re-sort with new criteria.
			std::sort(new_words.rbegin(), new_words.rend(), c2);
			// Calculate 2 byte code savings.
			uint64_t save_total2 = 0;
			for (size_t i = 0; i < std::min(new_words.size(), b2); ++i) {
				save_total2 += c2.wordCost(new_words[i]);
			}
			const auto total_save = save_total1 + save_total2 - escape_size;
			if (total_save > optimal_save) {
				optimal_b1 = b1;
				optimal_save = total_save;
			}
			if (verbose_) {
				std::cout << "len1=" << b1 << ":" << save_total1 << "+" << save_total2 << "-" << escape_size << "=" << total_save << std::endl;
			}
		}
		std::vector<WordCountPair> new_words = sorted_words;
		std::sort(new_words.rbegin(), new_words.rend(), c1);
		uint64_t save_total1 = 0;
		for (size_t i = 0; i < optimal_b1; ++i) {
			one_byte_words_.push_back(new_words[i].second);
		}
		std::sort(one_byte_words_.begin(), one_byte_words_.end());
		// Erase the saved 1 byte words.
		new_words.erase(new_words.begin(), new_words.begin() + optimal_b1);
		// Re-sort with new criteria.
		std::sort(new_words.rbegin(), new_words.rend(), c2);
		// Calculate 2 byte code savings.
		uint64_t save_total2 = 0;
		const size_t b2 = (char_codes_.size() - optimal_b1) * char_codes_.size(); // Two byte code words
		for (size_t i = 0; i < std::min(new_words.size(), b2); ++i) {
			two_byte_words_.push_back(new_words[i].second);
		}
		std::sort(two_byte_words_.begin(), two_byte_words_.end());
		size_t extra = 0;
		for (size_t i = b2;i < new_words.size(); ++i) {
			extra += new_words[i].second.length() * new_words[i].first;
		}

		// Number of 1b words.
		if (verbose_) std::cout << "words=" << sorted_words.size() << " 1b=" << one_byte_words_.size() << " 2b="
			<< two_byte_words_.size() <<  " save=" << optimal_save << " extra=" << extra << std::endl;

		// Go back to start.
		stream_->seek(stream_pos);

		// Create the dict buffer.
		// dict_buffer_
		dict_buffer_.clear();
		dict_buffer_.push_back('D');
		// Dictionary length.
		size_t len_pos_ = dict_buffer_.size();
		dict_buffer_.push_back(0);
		dict_buffer_.push_back(0);
		dict_buffer_.push_back(0);
		dict_buffer_.push_back(0);
		dict_buffer_.push_back(escape_char_);
		// Write char codes.
		dict_buffer_.push_back(char_codes_.size());
		for (auto c : char_codes_) dict_buffer_.push_back(c);
		// Number of 1 byte words.
		dict_buffer_.push_back(one_byte_words_.size());
		std::fill(is_one_byte_char_, is_one_byte_char_ + 256, false);
		std::fill(is_char_codes_, is_char_codes_ + 256, false);
		for (size_t i = 0; i < one_byte_words_.size(); ++i) {
			is_one_byte_char_[char_codes_[i]] = true;
			one_byte_map_[one_byte_words_[i]] = DictEntry(char_codes_[i]);
			writeWord(one_byte_words_[i]);
		}
		// Number of 2 byte codes.
		dict_buffer_.push_back(two_byte_words_.size());
		size_t i = 0;
		for (size_t c1 = one_byte_words_.size(); i < two_byte_words_.size() && c1 < char_codes_.size(); ++c1) {
			for (size_t c2 = 0; i < two_byte_words_.size() && c2 < char_codes_.size(); ++c2) {
				two_byte_map_[two_byte_words_[i]] = DictEntry(char_codes_[c1], char_codes_[c2]);
				writeWord(two_byte_words_[i]);
				i++;
			}
		}
		if (verbose_) std::cout << "Final buffer size " << dict_buffer_.size() << std::endl;
	}

private:
	class Comparator {
	public:
		Comparator(size_t codes_required) {
			SetCodesRequired(codes_required);
		}
		void SetCodesRequired(size_t codes_required) {
			codes_required_ = codes_required;
		}
		size_t wordCost(const WordCountPair& a) const {
			// return a.first;
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

	void encode() {
		
	}
	void writeWord(const std::string& s) {
		dcheck(s.length() <= 255);
		dict_buffer_.push_back(s.length());
		for (auto c : s) dict_buffer_.push_back(static_cast<uint8_t>(c));
	}
	std::vector<std::string> one_byte_words_;
	std::map<std::string, DictEntry> one_byte_map_;
	std::vector<std::string> two_byte_words_;
	std::map<std::string, DictEntry> two_byte_map_;
	// Dict buffer for encoding / decoding.
	std::vector<uint8_t> dict_buffer_;
	size_t dict_pos_;
	// Escape code, used to encode non dict char codes.
	uint8_t escape_char_;
	// Char codes
	std::vector<uint8_t> char_codes_;
	// 
	bool is_one_byte_char_[256];
	bool is_char_codes_[256];

	// Options.
	const bool verbose_;
	size_t min_occurences_;

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
	static const size_t kDictFraction = 2048;
	std::vector<Entry> entires;
};

#endif
