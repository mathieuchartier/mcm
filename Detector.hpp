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

#ifndef _DETECTOR_HPP_
#define _DETECTOR_HPP_

#include <fstream>
#include <deque>
#include "Util.hpp"
#include "SlidingWindow.hpp"
#include "Transform.hpp"
#include "UTF8.hpp"

// Detects blocks and data type from input data

class Detector {
	bool is_forbidden[256]; // Chars which don't appear in text often.
	
	DataProfile profile; // Current profile.
	size_t profile_length; // Length.

	// MZ pattern, todo replace with better detection.
	typedef std::vector<byte> Pattern;
	Pattern exe_pattern;

	// Buffer
	std::deque<byte> buffer;
public:
	Detector() {
		init();
	}

	void init() {
		profile_length = 0;
		profile = kBinary;
		for (auto& b : is_forbidden) b = false;

		byte forbidden_arr[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 14, 15, 16, 17, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
		for (auto c : forbidden_arr) is_forbidden[c] = true;
		
		// Exe pattern
		byte p[] = {0x4D, 0x5A, 0x90, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0xFF, 0xFF,};
		exe_pattern.clear();
		for (auto& c : p) exe_pattern.push_back(c);
	}

	template <typename TIn>
	void fill(TIn& sin) {
		while (buffer.size() < 128 * KB) {
			int c = sin.read();
			if (c == EOF) break;
			buffer.push_back(c);
		}
	}

	forceinline bool empty() const {
		return size() == 0;
	}

	forceinline size_t size() const {
		return buffer.size();
	}

	forceinline size_t at(size_t index) const {
		assert(index < buffer.size());
		return buffer[index];
	}

	int read() {
		if (empty()) {
			return EOF;
		}
		assert(profile_length > 0);
		--profile_length;
		size_t ret = buffer.front();
		buffer.pop_front();
		return ret;
	}

	forceinline size_t readBytes(size_t pos, size_t bytes = 4, bool big_endian = true) {
		size_t w = 0;
		if (pos + bytes <= size()) {
			// Past the end of buffer :(
			if(big_endian) {
				for (size_t i = 0; i < bytes; ++i) {
					w = (w << 8) | at(pos + i);
				}
			} else {
				for (size_t i = bytes; i; --i) {
					w = (w << 8) | at(pos + i - 1);
				}
			}
		}
		return w;
	}

	bool matches_pattern(Pattern& pattern) const {
		if (size() < pattern.size()) {
			return false;
		}
		for (size_t i = 0;i < pattern.size();++i) {
			if (pattern[i] != at(i)) {
				return false;
			}
		}
		return true;
	}

	DataProfile detect() {
		if (profile_length) {
			return profile;
		}

		if (false) {
			profile_length = size();
			return profile = kText;
		}

		const auto total = size();
		UTF8Decoder<true> decoder;
		size_t text_length = 0, i = 0;
		for (;i < total;++i) {
			auto c = buffer[i];
			decoder.update(c);
			if (decoder.err() || is_forbidden[(byte)c]) {
				break; // Error state?
			}
			if (decoder.done()) {
				text_length = i + 1;
			}
		}
		
		if (text_length >= std::min(total, static_cast<size_t>(64))) {
			profile = kText;
			profile_length = text_length;
		} else {
			// This is pretty bad, need a clean way to do it.
			size_t fpos = 0;
			size_t w0 = readBytes(fpos); fpos += 4;
			if (w0 == 0x52494646) {
				size_t chunk_size = readBytes(fpos); fpos += 4;
				size_t format = readBytes(fpos); fpos += 4;
				// Format subchunk.
				size_t subchunk_id = readBytes(fpos); fpos += 4;
				if (format == 0x57415645 && subchunk_id == 0x666d7420) {
					size_t subchunk_size = readBytes(fpos, 4, false); fpos += 4;
					if (subchunk_size == 16) {
						size_t audio_format = readBytes(fpos, 2, false); fpos += 2;
						size_t num_channels = readBytes(fpos, 2, false); fpos += 2;
						if (audio_format == 1 && (num_channels == 1 || num_channels == 2)) {
							fpos += 4; // Skip: Sample rate
							fpos += 4; // Skip: Byte rate
							fpos += 2; // Skip: Block align
							size_t bits_per_sample = readBytes(fpos, 2, false); fpos += 2;
							size_t subchunk2_id = readBytes(fpos, 4); fpos += 4;
							if (subchunk2_id == 0x64617461) {
								size_t subchunk2_size = readBytes(fpos, 4, false); fpos += 4;
								// Read wave header, TODO binary block as big as fpos?? Need to be able to queue subblocks then.
								profile_length = fpos + subchunk2_size;
								profile = kWave;
								return profile;
							}
						}
					}
				} 
			}

			profile = kBinary;
			profile_length = 1; //std::max(i, (size_t)16);
		}
		
		return profile;
	}
};

#endif
