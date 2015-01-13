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

#ifndef _RANGE_HPP_
#define _RANGE_HPP_

#include "Compressor.hpp"
#include <assert.h>
#include "Model.hpp"

// From 7zip, added single bit functions
class Range7 {
private:
	uint Range, Code, _cacheSize;
	uint64_t Low;
	byte _cache;

	static const uint TopBits = 24, TopValue = 1 << TopBits, Top = 0xFFFFFFFF;

	template <typename TOut>
	forceinline void shiftLow(TOut& sout) { //Emit the top byte 
		if (size_t(Low) < size_t(0xFF << TopBits) || Low >> 32) {
			byte temp = _cache;
			do {
				sout.put(byte(temp + byte(Low >> 32)));
				temp = 0xFF;
			} while(--_cacheSize);
			_cache = byte(uint(Low >> 24));
		}
		++_cacheSize;
		Low = size_t(Low << 8);
	}
public:
	void init() {
		Code = 0;
		Low = 0;
		Range = Top;
		_cache = 0;
		_cacheSize = 1;
	}

	template <typename TOut>
	forceinline void IncreaseRange(TOut& out) {
		while (Range < TopValue) {
			Range <<= 8;
			shiftLow(out);
		}
	}

	forceinline size_t getDecodedBit(size_t p, size_t shift) {
		const size_t mid = (Range >> shift) * p;
		size_t bit = Code < mid;
		if (bit) {
			Range = mid;
		} else {
			Code -= mid;
			Range -= mid;
		}
		return bit;
	}

	template <typename TOut>
	forceinline void encode(TOut& out, size_t bit, size_t p, size_t shift) {
		assert(p < (1U << shift));
		assert(p != 0U);
		const size_t mid = (Range >> shift) * p;
		if (bit) {
			Range = mid;
		} else {
			Low += mid;
			Range -= mid;
		}
		IncreaseRange(out);
	}

	template <typename TIn>
	forceinline size_t decode(TIn& in, size_t p, size_t shift) {
		assert(p < (1U << shift));
		assert(p != 0U);
		auto ret = getDecodedBit(p, shift);
		Normalize(in);
		return ret;
	}

	template <typename TOut>
	inline void encodeBit( TOut& out, size_t bit) {
		Range >>= 1;
		if (bit) {
			Low += Range;
		}
		while (Range < TopValue) {
			Range <<= 8;
			shiftLow(out);
		}
	}

	template <typename TIn>
	inline size_t decodeBit(TIn& in) {
		size_t top = Range;
		Range >>= 1;
		if (Code >= Range) {
			Code -= Range;
			Normalize(in);
			return 1;
		} else {
			Normalize(in);
			return 0;
		}
	}

	template <typename TOut>
	void EncodeBits(TOut& Out, uint value, int numBits) {
		for (numBits--; numBits >= 0; numBits--) {
			Range >>= 1;
			Low += Range & (0 - ((value >> numBits) & 1));
			while (Range < TopValue) {
				Range <<= 8;
				shiftLow(Out);
			}
		}
	}

	template <typename TIn>
	uint DecodeDirectBits(TIn& In, int numTotalBits) {
		uint range = Range, code = Code, result = 0;
		for (int i = numTotalBits; i != 0; i--) {
			range >>= 1;
			uint t = (code - range) >> 31;
			code -= range & (t - 1);
			result = (result << 1) | (1 - t); 
			if (range < TopValue) {
				code = (code << 8) | (In.read() & 0xFF);
				range <<= 8;
			}
		}
		Range = range;
		Code = code;
		return result;
	}

	template <typename TIn>
	uint DecodeDirectBit(TIn& sin) {
		Range >>= 1;
		size_t t = (Code - Range) >> 31;
		Code -= Range & (t - 1);
		Normalize(sin);
		return t ^ 1;
	}

	template <typename TOut>
	inline void Encode(TOut& Out, size_t start, size_t size, size_t total) {
		assert(size > 0);
		assert(start < total);
		assert(start + size <= total);
		Range /= total;
		Low += start * Range;
		Range *= size;
		while (Range < TopValue) {
			Range <<= 8;
			shiftLow(Out);
		}
	}

	template <typename TIn>
	inline void Decode(TIn& In, uint start, uint size) {
		Code -= start * Range;
		Range *= size;
		Normalize(In);
	}

	template <typename TOut>
	inline void encodeDirect(TOut& Out, size_t start, size_t total) {
		assert(start < total);
		Range /= total;
		Low += start * Range;
		while (Range < TopValue) {
			Range <<= 8;
			shiftLow(Out);
		}
	}

	template <typename TIn>
	inline uint decodeByte(TIn& In) {
		Range >>= 8;
		size_t start = Code / Range;
		Code -= start * Range;
		Normalize(In);
		return start;
	}

	template <typename TIn>
	inline uint decodeDirect(TIn& In, uint Total) {
		uint start = GetThreshold(Total);
		Code -= start * Range;
		Normalize(In);
		return start;
	}

	template <typename TOut>
	void flush(TOut& Out) {
		for (uint i = 0; i < 5; i++)
			 shiftLow(Out);
	}

	template <typename TIn>
	void initDecoder(TIn& In)
	{
		init();
		Code = 0;
		Range = Top;
		for(uint i = 0;i < 5;i++)
			Code = (Code << 8) | (In.get() & 0xFF);	
	}

	inline uint GetThreshold(uint Total) {
		return Code / (Range /= Total);
	}

	template <typename TIn>
	forceinline void Normalize(TIn& In) {
		while (Range < TopValue) {
			Code = (Code << 8) | (In.get() & 0xFF);
			Range <<= 8;
		}
	}
};

#endif
