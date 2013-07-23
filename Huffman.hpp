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

#ifndef _HUFFMAN_HPP_
#define _HUFFMAN_HPP_

#include <algorithm>
#include <cassert>
#include <set>

#include "Compress.hpp"
#include "Range.hpp"

class Huffman {
public:
	class Code {
	public:
		static const size_t nonLeaf = 0;
		size_t value;
		size_t length;

		Code() : value(0), length(nonLeaf) {
		
		}
	};

	template <const size_t length_limit = 16>
	class DeCode {
		// IsLeaf bitmap.
		static const size_t limit = 1 << (length_limit + 1);
		static const size_t bpw = sizeof(size_t) * 8;
		size_t bitmap[limit / bpw];
		byte decode[limit];
	public:
		forceinline void setBit(size_t index) {
			bitmap[index / bpw] |= 1 << (index & (bpw - 1));
		}

		forceinline size_t getBit(size_t index) {
			return (bitmap[index / bpw] >> (index & (bpw - 1))) & 1;
		}

		forceinline bool isLeaf(size_t ctx) {
			return getBit(ctx) != 0;
		}

		forceinline byte getCode(size_t ctx) {
			return decode[ctx];
		}

		void build(Code* codes, size_t alphabet_size) {
			for (auto& b : bitmap) b = 0;
			for (auto& b : decode) b = 0;
			for (size_t i = 0; i < alphabet_size; ++i) {
				size_t new_code = codes[i].value | (1 << codes[i].length);
				setBit(new_code);
				decode[new_code] = i;
			}
		}
	};

	template <typename T>
	class Tree {
	public:
		size_t value;
		T weight;
		Tree *a, *b;

		forceinline size_t getAlphabet() const {
			return code;
		}

		forceinline bool isLeaf() const {
			return a == nullptr && b == nullptr;
		}

		forceinline T getWeight() const {
			return weight;
		}

		void getCodes(Code* codes, size_t bits = 0, size_t length = 0) {
			assert(codes != nullptr);
			if (isLeaf()) {
				codes[value].value = bits;
				codes[value].length = length;
			} else {
				a->getCodes(codes, (bits << 1) | 0,  length + 1);
				b->getCodes(codes, (bits << 1) | 1,  length + 1);
			}
		}

		void getLengths(T* lengths, size_t cur_len = 0) {
			if (isLeaf()) {
				lengths[value] = cur_len;
			} else {
				a->getLengths(lengths, cur_len + 1);
				b->getLengths(lengths, cur_len + 1);
			}
		}

		void calcDepth(size_t cur_depth = 0) {
			if (!isLeaf()) weight = 0;
			if (a != nullptr) {
				a->calcDepth(cur_depth + 1);
				weight += a->getWeight();
			}
			if (b != nullptr) {
				b->calcDepth(cur_depth + 1);
				weight += b->getWeight();
			}
		}

		uint64 getCost(size_t bits = 0) const {
			if (isLeaf())
				return bits * weight;
			else
				return a->getCost(bits + 1) + b->getCost(bits + 1);
		}

		Tree(size_t value, T w) : value(value), weight(w), a(nullptr), b(nullptr) {

		}

		Tree(Tree* a, Tree* b)
			: value(0), weight(a->getWeight() + b->getWeight()), a(a), b(b) {
			
		}

		~Tree() {
			delete a;
			delete b;
		}

		void printRatio(const char* name) const {
			std::cout << "Huffman tree " << name << ": " << getWeight() << " -> " << getCost() / 8 << std::endl;
		}
	};

	typedef Tree<size_t> HuffTree;

	class TreeComparator {
	public:
		inline bool operator()(HuffTree* a, HuffTree* b) const {
			return a->getWeight() < b->getWeight();
		}
	};

	typedef std::multiset<HuffTree*, TreeComparator> TreeSet;
public:
	HuffTree* buildTreeOptimal(size_t* frequencies, size_t count = 256) {
		TreeSet trees;
		for (size_t i = 0; i < count; ++i) {
			if (frequencies[i]) {
				trees.insert(new HuffTree(i, frequencies[i]));
			}
		}
		return buildTree(trees);
	}

	// TODO: Optimize, fix memory leaks.
	// Based off of example from Introduction to Data Compression.
	HuffTree* buildTreePackageMerge(size_t* frequencies, size_t count = 256, size_t max_depth = 16) {
#if 0
		// Example from book.
		count = 6;
		max_depth = 3;
		frequencies[0] = 5;
		frequencies[1] = 10;
		frequencies[2] = 15;
		frequencies[3] = 20;
		frequencies[4] = 20;
		frequencies[5] = 30;
#endif

		class Package {
		public:
			std::multiset<size_t> alphabets;
			int64 weight;

			Package() : weight(0) {

			}

			bool operator()(const Package* a, const Package* b) const {
				if (a->weight < b->weight) return true;
				if (a->weight > b->weight) return false;
				return a->alphabets.size() < b->alphabets.size();
			}
		};

		size_t package_limit = 2 * count - 2;

		// Set up initial packages.
		typedef std::multiset<Package*, Package> PSet;
		PSet original_set;
		for (size_t i = 0; i < count; ++i) {
			auto* p = new Package;
			p->alphabets.insert(i);
			p->weight = 1 + frequencies[i]; // Algorithm can't handle 0 frequencies.
			original_set.insert(p);
		}

		PSet merge_set = original_set;

		// Perform the package merge algorithm.
		for (size_t i = 1; i < max_depth; ++i) {
			PSet new_set;
			size_t count = merge_set.size() / 2;
			// Package count pacakges.
			auto it = merge_set.begin();
			for (size_t j = 0; j < count; ++j) {
				Package *a = *(it++);
				Package *b = *(it++);
				auto *new_package = new Package;
				new_package->alphabets.insert(a->alphabets.begin(), a->alphabets.end());
				new_package->alphabets.insert(b->alphabets.begin(), b->alphabets.end());
				new_package->weight = a->weight + b->weight;
				new_set.insert(new_package);
			}

			// Merge back into original set.
			merge_set = original_set;
			merge_set.insert(new_set.begin(), new_set.end());

			while (merge_set.size() > package_limit) {
				auto* pend = *merge_set.rbegin();
				merge_set.erase(pend);
				//delete pend;
			}

			// Print packages.
			if (false) {
				for (auto* p : merge_set) {
					std::cout << " " << p->weight << "{";
					for (auto a : p->alphabets)
						std::cout << a << ",";
					std::cout << "}, ";
				}
 				std::cout << std::endl;
			}
		}

		// Calculate lengths.
		std::vector<size_t> lengths(count, 0);
		for (auto* p : merge_set) {
			for (auto a : p->alphabets) { 
				++lengths[a];
			}
		}

		// Might not work for max_depth = 32.
		size_t total = 0;
		for (auto l : lengths) {
			assert(l > 0 && l <= max_depth);
			total += 1 << (max_depth - l);
		}

		// Sanity check.
		if (total != 1 << max_depth) {
			std::cerr << "Fatal error constructing huffman table " << total << " vs " << (1 << max_depth) << std::endl;
			return nullptr;
		}

		// Build huffmann tree from the code lengths.
		return buildFromCodeLengths(&lengths[0], count, max_depth, &frequencies[0]);
	}

	HuffTree* buildFromCodeLengths(size_t* lengths, size_t count, size_t max_depth, size_t* freqs = nullptr) {
		HuffTree* tree = new HuffTree(size_t(0), 0);
		typedef std::vector<HuffTree*> TreeVec;
		TreeVec cur_level;
		cur_level.push_back(tree);
		for (size_t i = 0; i <= max_depth; ++i) {
			if (cur_level.empty()) break;
			for (size_t j = 0; j < count; ++j) {
				if (lengths[j] == i) {
					auto* tree = cur_level.back();
					cur_level.pop_back();
					tree->value = j;
					tree->weight = freqs != nullptr ? freqs[j] : 0;
				}
			}

			TreeVec new_set;
			for (size_t i = 0; i < cur_level.size(); ++i) {
				auto* tree = cur_level[i];
				tree->a = new HuffTree(size_t(0), 0);
				tree->b = new HuffTree(size_t(0), 0);
				new_set.push_back(tree->a);
				new_set.push_back(tree->b);
			}
			cur_level = new_set;
		}
		tree->calcDepth(0);
		return tree;
	}

	// Combine two smallest trees until we hit max depth.
	HuffTree* buildTree(TreeSet& trees) {
		while (trees.size() > 1) {
			auto it = trees.begin();
			HuffTree *a = *it;
			trees.erase(it);
			it = trees.begin();
			HuffTree *b = *it; 
			trees.erase(it);
			// Reinsert the new tree.
			trees.insert(new HuffTree(a, b));	
		}
		return *trees.begin();
	}

	// Write a huffmann tree to a stream.
	template <typename TEnt, typename TStream>
	void writeTree(TEnt& ent, TStream& stream, HuffTree* tree, size_t alphabet_size, size_t max_length) {
		std::vector<size_t> lengths(alphabet_size, 0);
		tree->getLengths(&lengths[0]);
		// Assumes we can't have any 0 length codes.
		for (size_t i = 0; i < alphabet_size; ++i) {
			assert(lengths[i] > 0 && lengths[i] <= max_length);
			ent.encodeDirect(stream, lengths[i] - 1, max_length);
		}
	}

	// Read a huffmann tree from a stream.
	template <typename TEnt, typename TStream>
	HuffTree* readTree(TEnt& ent, TStream& stream, size_t alphabet_size, size_t max_length) {
		std::vector<size_t> lengths(alphabet_size, 0);
		for (size_t i = 0; i < alphabet_size; ++i) {
			lengths[i] = ent.decodeDirect(stream, max_length) + 1;
		}
		return buildFromCodeLengths(&lengths[0], alphabet_size, max_length, nullptr);
	}
};

class HuffmanComp {
	Range7 ent;

	static const size_t alphabet_size = 256;
	static const size_t max_length = 16;
public:
	static const size_t version = 0;
	void setMemUsage(size_t n) {}

	void init() {
		
	}

	template <typename TOut, typename TIn>
	size_t Compress(TOut& sout, TIn& sin) {
		size_t count = 0;
		std::vector<size_t> freq(alphabet_size, 0);
		init();

		// Get frequencies
		size_t length = 0;
		for (;;++length) {
			auto c = sin.read();
			if (c == EOF) break;
			++freq[(size_t)c];
		}

		// Print frequencies
		printIndexedArray("frequencies", freq);
		sin.restart();

		Huffman huff;
		// Build length limited tree with package merge algorithm.
		auto* tree = huff.buildTreePackageMerge(&freq[0], alphabet_size, max_length);
		tree->printRatio("LL(16)");

		if (false ){
			auto* treeO = huff.buildTreeOptimal(&freq[0], alphabet_size);
			treeO->printRatio("optimal");
		}
		
		ProgressMeter meter;
		ent.init();
		huff.writeTree(ent, sout, tree, alphabet_size, max_length);
		std::cout << "Encoded huffmann tree in ~" << sout.getTotal() << " bytes" << std::endl;

		ent.EncodeBits(sout, length, 31);

		// Generate codes.
		Huffman::Code codes[alphabet_size];
		tree->getCodes(codes);

		// Encode with huffman codes.
		std::cout << std::endl;
		for (;;) {
			int c = sin.read();
			if (c == EOF) break;
			auto* code = &codes[c];
			ent.EncodeBits(sout, code->value, code->length);
			meter.addBytePrint(sout.getTotal());
		}
		std::cout << std::endl;
		ent.flush(sout);
		return sout.getTotal();
	}

	template <typename TOut, typename TIn>
	bool DeCompress(TOut& sout, TIn& sin) {
		Huffman huff;

		ProgressMeter meter(true);

		ent.initDecoder(sin);
		auto* tree = huff.readTree(ent, sin, alphabet_size, max_length);
		size_t length = ent.DecodeDirectBits(sin, 31);

		// Generate codes.
		Huffman::Code codes[alphabet_size];
		tree->getCodes(codes);

		// Build inverse.
		Huffman::DeCode<max_length> decoder;
		decoder.build(&codes[0], alphabet_size);

		std::cout << std::endl;
		for (size_t i = 0; i < length; ++i) {
			size_t cur_code = 1;
			do {
				cur_code = (cur_code << 1) | ent.DecodeDirectBit(sin);
			} while (!decoder.isLeaf(cur_code));
			sout.write(decoder.getCode(cur_code));
			meter.addBytePrint(sin.getTotal());
		}
		std::cout << std::endl;
		return true;
	}
};

#endif
