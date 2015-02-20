/*	MCM file compressor

	Copyright (C) 2015, Google Inc.
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

#include "CM.hpp"

template <CMType kCMType>
void CM<kCMType>::compress(Stream* in_stream, Stream* out_stream) {
	BufferedStreamReader<4 * KB> sin(in_stream);
	BufferedStreamWriter<4 * KB> sout(out_stream);
	assert(in_stream != nullptr);
	assert(out_stream != nullptr);
	Detector detector;
	detector.setOptVar(opt_var);
	detector.init();

	// Compression profiles.
	std::vector<uint64_t> profile_counts((uint32_t)kProfileCount, 0);
	std::vector<uint64_t> profile_len((uint32_t)kProfileCount, 0);

	// Start by writing out archive header.
	init();
	ent.init();

	// TODO: Disable if entropy is too high?
	if (use_huffman) {
		clock_t start = clock();
		size_t freqs[256] = { 1 };
		std::cout << "Building huffman tree" << std::endl;
		if (false) {
			for (;;) {
				int c = sin.get();
				if (c == EOF) break;
				++freqs[c];
			}
		}
		Huffman::HuffTree* tree = Huffman::buildTreePackageMerge(freqs, 256, static_cast<size_t>(huffman_len_limit));
		tree->printRatio("LL");
		Huffman::writeTree(ent, sout, tree, 256, huffman_len_limit);
		huff.build(tree);	
		std::cout << "Building huffman tree took: " << clock() - start << " MS" << std::endl;
	}
	size_t freqs[kProfileCount][256] = { 0 };
	detector.fill(sin);
	for (;;) {
		if (!detector.size()) break;

		// Block header
		SubBlockHeader block;
		// Initial detection.
		block.profile = detector.detect();
		++profile_counts[(uint32_t)block.profile];
		writeSubBlock(sout, block);
		setDataProfile(block.profile);

		// Code a block.
		uint64_t block_size = 0;
		for (;;++block_size) {
			detector.fill(sin);
			DataProfile new_profile = detector.detect();
			bool is_end_of_block = new_profile != block.profile;
			int c;
			if (!is_end_of_block) {
				c = detector.read();
				is_end_of_block = c == EOF;
			}
			if (is_end_of_block) {
				c = eof_char;
			}
			freqs[block.profile][(byte)c]++;
			processByte<false>(sout, c);
			update(c);
			if (UNLIKELY(c == eof_char)) {
				ent.encode(sout, is_end_of_block, end_of_block_mdl.getP(), kShift);
				end_of_block_mdl.update(is_end_of_block);
				if (is_end_of_block) break;
			}
		}
		profile_len[(uint64_t)block.profile] += block_size;
	}
#ifndef _WIN64
	_mm_empty();
#endif
	std::cout << std::endl;

	// Encode the end of block subblock.
	SubBlockHeader eof_block;
	eof_block.profile = kEOF;
	writeSubBlock(sout, eof_block);
	ent.flush(sout);
		
	// TODO: Put in statistics??
	uint64_t total = 0;
	for (size_t i = 0; i < kProfileCount; ++i) {
		auto cnt = profile_counts[i];
		if (cnt) {
			std::cout << (DataProfile)i << " : " << cnt << "(" << profile_len[i] / KB << "KB)" << std::endl;
		}
		if (false) {
			Huffman h2;
			auto* tree = h2.buildTreePackageMerge(freqs[i]);
			tree->printRatio("Tree");
			total += tree->getCost() / 8;
		}
	}
	if (false) {
		std::cout << "Total huffman: " << total << std::endl;
	}

	if (kStatistics) {
		if (!kFastStats) {
			std::ofstream fout("probs.txt");
			for (size_t i = 0; i < inputs; ++i) {
				fout << "{";
				for (uint32_t j = 0; j < 256; ++j) fout << preds[kText][i][j].getP() << ",";
				fout << "}," << std::endl;
			}
			// Print average weights so that we find out which contexts are good and which are not.
			for (size_t cur_p = 0; cur_p < static_cast<uint32_t>(kProfileCount); ++cur_p) {
				auto cur_profile = (DataProfile)cur_p;
				CMMixer* mixers = getProfileMixers(cur_profile);
				std::cout << "Mixer weights for profile " << cur_profile << std::endl;
				for (size_t i = 0; i < 256; ++i) {
					double weights[inputs+2] = { 0 };
					double lzp_weights[inputs+2] = { 0 };
					size_t count = 0, lzp_count = 0;
					for (size_t j = 0; j < 256; ++j) {
						// Only mixers which have been used at least a few times.
						auto& m = mixers[i * 256 + j];
						if (m.getLearn() < 30) {
							if (j != 0) {
								for (size_t k = 0; k < m.size(); ++k) {
									weights[k] += double(m.getWeight(k)) / double(1 << m.shift());
								}
								++count;
							} else {
								for (size_t k = 0; k < m.size(); ++k) {
									lzp_weights[k] += double(m.getWeight(k)) / double(1 << m.shift());
								}
								++lzp_count;
							}
						}
					}
					if (count != 0) {
						std::cout << "Weights " << i << ":";
						for (auto& w : weights) std::cout << w / double(count) << " ";
						std::cout << std::endl;
					}
					if (lzp_count) {
						std::cout << "LZP " << i << ":";
						for (auto& w : lzp_weights) std::cout << w << " ";
						std::cout << std::endl;
					}
				}
			}
			// State count.
			size_t z = 0, nz = 0;
			for (size_t i = 0;i <= hash_mask;++i) {
				++(hash_table[i] != 0 ? nz : z);
			}
			std::cout << "zero=" << z << " nonzero=" << nz << std::endl;
		}
		std::cout << "CMed bytes=" << (mixer_skip[0] + mixer_skip[1]) / 8 <<  " mix skip=" << mixer_skip[0] << " mix nonskip=" << mixer_skip[1] << std::endl;
		std::cout << "match=" << match_count_ << " matchfail=" << non_match_count_ << " nonmatch=" << other_count_ << std::endl;
		std::cout << "lzp_bit_size=" << lzp_bit_match_bytes_ << " lzp_bit_miss_bytes=" << lzp_bit_miss_bytes_
			<< " lzp_miss_bytes=" << lzp_miss_bytes_ << " lzp_normal_bytes=" << normal_bytes_ << std::endl;
	}
}

template <CMType kCMType>
void CM<kCMType>::decompress(Stream* in_stream, Stream* out_stream) {
	BufferedStreamReader<4 * KB> sin(in_stream);
	BufferedStreamWriter<4 * KB> sout(out_stream);
	ProgressMeter meter(false);
	init();
	ent.initDecoder(sin);
	if (use_huffman) {
		auto* tree = Huffman::readTree(ent, sin, 256, huffman_len_limit);
		huff.build(tree);
		delete tree;
	}
	for (;;) {
		SubBlockHeader block;
		readSubBlock(sin, block);
		if (block.profile == kEOF) break;
		setDataProfile(block.profile);
			
		for (;;) {
			uint32_t c = processByte<true>(sin);
			update(c);

			if (c == eof_char) {
				int eob = ent.decode(sin, end_of_block_mdl.getP(), kShift);
				end_of_block_mdl.update(eob);
				if (eob) {
					break; // Hit end of block, go to next block.
				}
			}

			sout.put(c);
		}
	}
}	

template class CM<kCMTypeTurbo>;
template class CM<kCMTypeFast>;
template class CM<kCMTypeMid>;
template class CM<kCMTypeHigh>;
template class CM<kCMTypeMax>;
