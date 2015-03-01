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
	BufferedStreamWriter<4 * KB> sout(out_stream);
	assert(in_stream != nullptr);
	assert(out_stream != nullptr);
	Detector detector(in_stream);
	detector.setOptVar(opt_var);
	detector.init();

	// Compression profiles.
	std::vector<uint64_t> profile_counts(static_cast<uint32_t>(Detector::kProfileCount), 0);
	std::vector<uint64_t> profile_len(static_cast<uint32_t>(Detector::kProfileCount), 0);

// #define SPLIT_TYPES
#if SPLIT_TYPES
	std::ofstream fbinary("binary.out", std::ios_base::out | std::ios_base::binary);
	std::ofstream ftext("text.out", std::ios_base::out | std::ios_base::binary);
#endif

	// Start by writing out archive header.
	init();
	ent.init();

	// TODO: Disable if entropy is too high?
	if (use_huffman) {
		clock_t start = clock();
		size_t freqs[256] = { 1 };
		std::cout << "Building huffman tree" << std::endl;
		/*
		for (;;) {
			int c = sin.get();
			if (c == EOF) break;
			++freqs[c];
		}
		*/
		Huffman::HuffTree* tree = Huffman::buildTreePackageMerge(freqs, 256, static_cast<size_t>(huffman_len_limit));
		tree->printRatio("LL");
		Huffman::writeTree(ent, sout, tree, 256, huffman_len_limit);
		huff.build(tree);	
		std::cout << "Building huffman tree took: " << clock() - start << " MS" << std::endl;
	}
	size_t freqs[Detector::kProfileCount][256] = { 0 };
	for (;;) {
		Detector::Profile new_profile;
		auto c = detector.get(new_profile);
		if (new_profile == Detector::kProfileEOF) break;
		auto cm_profile = profileForDetectorProfile(new_profile);
		if (cm_profile != profile) {
			setDataProfile(cm_profile);
		}
#if SPLIT_TYPES
		if (cm_profile == kProfileBinary)  fbinary.put(c);
		else ftext.put(c);
#endif
		// Initial detection.
		dcheck(c != EOF);
		processByte<false>(sout, c);
		update(c);
		// profile_len[(uint64_t)block.profile] += block_size;
	}
	ent.flush(sout);
#ifndef _WIN64
	_mm_empty();
#endif
	std::cout << std::endl;
		
	// TODO: Put in statistics??
	uint64_t total = 0;
	for (size_t i = 0; i < kProfileCount; ++i) {
		auto cnt = profile_counts[i];
		if (cnt) {
			std::cout << static_cast<CMProfile>(i) << " : " << cnt << "(" << profile_len[i] / KB << "KB)" << std::endl;
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
				for (uint32_t j = 0; j < 256; ++j) fout << preds[kProfileText][i][j].getP() << ",";
				fout << "}," << std::endl;
			}
			// Print average weights so that we find out which contexts are good and which are not.
			for (size_t cur_p = 0; cur_p < static_cast<uint32_t>(kProfileCount); ++cur_p) {
				auto cur_profile = (CMProfile)cur_p;
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
		detector.dumpInfo();
		std::cout << "CMed bytes=" << formatNumber((mixer_skip[0] + mixer_skip[1]) / 8)
			<< " mix skip=" << formatNumber(mixer_skip[0])
			<< " mix nonskip=" << formatNumber(mixer_skip[1]) << std::endl;
		std::cout << "match=" << formatNumber(match_count_)
			<< " matchfail=" << formatNumber(non_match_count_)
			<< " nonmatch=" << formatNumber(other_count_) << std::endl;
		for (size_t i = 0; i < kMaxMatch; ++i) {
			const size_t t = match_hits_[i] + match_miss_[i];
			if (t != 0) {
				std::cout << i << ":" << match_hits_[i] << "/" << match_miss_[i] << " = " << static_cast<double>(match_hits_[i]) / t << std::endl;
			}
		}
		if (lzp_enabled_) {
			std::cout << "lzp_bit_size=" << formatNumber(lzp_bit_match_bytes_)
				<< " lzp_bit_miss_bytes=" << formatNumber(lzp_bit_miss_bytes_)
				<< " lzp_miss_bytes=" << formatNumber(lzp_miss_bytes_)
				<< " lzp_normal_bytes=" << formatNumber(normal_bytes_) << std::endl;
		}
	}
}

template <CMType kCMType>
void CM<kCMType>::decompress(Stream* in_stream, Stream* out_stream) {
	BufferedStreamReader<4 * KB> sin(in_stream);
	Detector detector(out_stream);
	detector.setOptVar(opt_var);
	detector.init();

	init();
	ent.initDecoder(sin);
	if (use_huffman) {
		auto* tree = Huffman::readTree(ent, sin, 256, huffman_len_limit);
		huff.build(tree);
		delete tree;
	}
	for (;;) {
		auto new_profile = detector.detect();
		if (new_profile == Detector::kProfileEOF) {
			break;
		}
		auto cm_profile = profileForDetectorProfile(new_profile);
		if (cm_profile != profile) {
			setDataProfile(cm_profile);
		}
		uint32_t c = processByte<true>(sin);
		update(c);
		detector.put(c);
	}
	detector.flush();
}	


std::ostream& operator << (std::ostream& sout, const CMProfile& pattern) {
	switch (pattern) {
	case kProfileText: return sout << "text";
	case kProfileBinary: return sout << "binary";
	}
	return sout << "unknown!";
}

template class CM<kCMTypeTurbo>;
template class CM<kCMTypeFast>;
template class CM<kCMTypeMid>;
template class CM<kCMTypeHigh>;
template class CM<kCMTypeMax>;
