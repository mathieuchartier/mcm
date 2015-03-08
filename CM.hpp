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

#ifndef _CM_HPP_
#define _CM_HPP_

#include <cstdlib>
#include <vector>
#include "Detector.hpp"
#include "DivTable.hpp"
#include "Entropy.hpp"
#include "Huffman.hpp"
#include "Log.hpp"
#include "MatchModel.hpp"
#include "Memory.hpp"
#include "Mixer.hpp"
#include "Model.hpp"
#include "Range.hpp"
#include "StateMap.hpp"
#include "Util.hpp"
#include "WordModel.hpp"
#include "SSE.hpp"

// Options.
#define USE_MMX 0

// Map from state -> probability.
class ProbMap {
public:
	class Prob {
	public:
		Prob() : p_(2048), stp_(0) {
			count_[0] = count_[1] = 0;
		}
		forceinline int16_t getSTP() {
			return stp_;
		}
		forceinline int16_t getP() {
			return p_;
		}
		forceinline void addCount(size_t bit) {
			++count_[bit];
		}
		template <typename Table>
		void update(const Table& table, size_t min_count = 10) {
			const size_t count = count_[0] + count_[1];
			if (count > min_count) {
				setP(table, 4096U * static_cast<uint32_t>(count_[1]) / count);
				count_[0] = count_[1] = 0;
			}
		}
		template <typename Table>
		void setP(const Table& table, uint16_t p) {
			p_ = p;
			stp_ = table.st(p_);
		}

	private:
		uint16_t count_[2];
		uint16_t p_;
		int16_t stp_;
	};

	Prob& getProb(size_t st) {
		return probs_[st];
	}

private:
	Prob probs_[256];
};

enum CMType {
	kCMTypeTurbo,
	kCMTypeFast,
	kCMTypeMid,
	kCMTypeHigh,
	kCMTypeMax,
};

enum CMProfile {
	kProfileText,
	kProfileBinary,
	kProfileCount,
};

std::ostream& operator << (std::ostream& sout, const CMProfile& pattern);

template <CMType kCMType = kCMTypeHigh>
class CM : public Compressor {
public:
	static const size_t inputs =
		kCMType == kCMTypeTurbo ? 3 : 
		kCMType == kCMTypeFast ? 4 : 
		kCMType == kCMTypeMid ? 6 : 
		kCMType == kCMTypeHigh ? 8 : 
		kCMType == kCMTypeMax ? 10 :
		0;
	
	// Flags
	static const bool kStatistics = true;
	static const bool kFastStats = true;
	static const bool kFixedProbs = false;
	// Currently, LZP isn't as great as it coul be.
	static const bool kUseLZP = true;
	static const bool kUseSSE = true;
	static const bool kUseSSEOnlyLZP = true;
	// Prefetching related flags.
	static const bool kUsePrefetch = false;
	static const bool kPrefetchMatchModel = true;
	static const bool kPrefetchWordModel = true;
	static const bool kFixedMatchProbs = true;

	// SS table
	static const uint32_t kShift = 12;
	static const int kMaxValue = 1 << kShift;
	static const int kMinST = -kMaxValue / 2;
	static const int kMaxST = kMaxValue / 2;
	typedef ss_table<short, kMaxValue, kMinST, kMaxST, 8> SSTable;
	SSTable table;
	
	typedef safeBitModel<unsigned short, kShift, 5, 15> BitModel;
	typedef fastBitModel<int, kShift, 9, 30> StationaryModel;
	typedef fastBitModel<int, kShift, 9, 30> HPStationaryModel;

	// Word model
	WordModel word_model;
	size_t word_model_ctx_map_[WordModel::kMaxLen + 1];

	Range7 ent;
	
	typedef MatchModel<HPStationaryModel> MatchModelType;
	MatchModelType match_model;
	size_t match_model_order_;
	std::vector<int> fixed_match_probs_;

	// Hash table
	size_t hash_mask;
	size_t hash_alloc_size;
	MemMap hash_storage;
	uint8_t *hash_table;

	// If LZP, need extra bit for the 256 ^ o0 ctx
	static const uint32_t o0size = 0x100 * (kUseLZP ? 2 : 1);
	static const uint32_t o1size = o0size * 0x100;
	static const uint32_t o2size = o0size * 0x100 * 0x100;

	// o0, o1, o2, s1, s2, s3, s4
	static const size_t o0pos = 0;
	static const size_t o1pos = o0pos + o0size; // Order 1 == sparse 1
	static const size_t o2pos = o1pos + o1size;
	static const size_t s2pos = o2pos + o2size; // Sparse 2
	static const size_t s3pos = s2pos + o1size; // Sparse 3
	static const size_t s4pos = s3pos + o1size; // Sparse 4
	static const size_t hashStart = s4pos + o1size;

	// Maps from char to 4 bit identifier.
	uint8_t* current_mask_map_;
	uint8_t binary_mask_map_[256];
	uint8_t text_mask_map_[256];

	// Mixers
	uint32_t mixer_mask;
#if USE_MMX
	typedef MMXMixer<inputs, 15, 1> CMMixer;
#else
	typedef Mixer<int, inputs+1, 17, 11> CMMixer;
#endif
	std::vector<CMMixer> mixers;
	CMMixer *mixer_base;
	CMMixer *cur_profile_mixers_;

	// Contexts
	uint32_t owhash; // Order of sizeof(uint32_t) hash.

	// Rotating buffer.
	CyclicBuffer<byte> buffer;

	// Options
	uint32_t opt_var;

	// CM state table.
	static const uint32_t num_states = 256;
	uint8_t state_trans[num_states][2];

	// Huffman preprocessing.
	static const bool use_huffman = false;
	static const uint32_t huffman_len_limit = 16;
	Huffman huff;
	
	// If force profile is true then we dont use a detector.
	bool force_profile_;
	// Current profile.
	CMProfile profile_;
	// Bytes to process.
	uint64_t remain_bytes_;

	// Mask model.
	uint32_t mask_model_;

	// LZP
	std::vector<CMMixer> lzp_mixers;
	bool lzp_enabled_;
	static const size_t kMaxMatch = 100;
	StationaryModel lzp_mdl_[kMaxMatch];

	// SM
	typedef StationaryModel PredModel;
	static const size_t kProbCtx = inputs;
	typedef PredModel PredArray[kProbCtx][256];
	static const size_t kPredArrays = static_cast<size_t>(kProfileCount) + 1;
	PredArray preds[kPredArrays];
	PredArray* cur_preds;

	// SSE
	SSE<kShift> sse;
	size_t sse_ctx;
	SSE<kShift> sse2;
	size_t mixer_sse_ctx;

	// Memory usage
	size_t mem_usage;

	// Flags for which models are enabled.
	enum Model {
		kModelOrder0,
		kModelOrder1,
		kModelOrder2,
		kModelOrder3,
		kModelOrder4,

		kModelOrder5,
		kModelOrder6,
		kModelOrder7,
		kModelOrder8,
		kModelOrder9,
		
		kModelOrder10,
		kModelOrder11,
		kModelOrder12,
		kModelSparse2,
		kModelSparse3,
		
		kModelSparse4,
		kModelSparse23,
		kModelSparse34,
		kModelWord1,
		kModelWord2,

		kModelWordMask,
		kModelWord12,
		kModelMask,
		kModelMatchHashE,
		kModelCount,
	};
	static_assert(kModelCount <= 32, "no room in word");
	static const size_t kMaxOrder = 12;
	uint32_t enabled_models_;
	size_t max_order_;

	// Statistics
	uint64_t mixer_skip[2];
	uint64_t match_count_, non_match_count_, other_count_;
	uint64_t lzp_bit_match_bytes_, lzp_bit_miss_bytes_, lzp_miss_bytes_, normal_bytes_;
	uint64_t match_hits_[kMaxMatch], match_miss_[kMaxMatch];

	// TODO: Get rid of this.
	static const uint32_t eof_char = 126;
	
	forceinline uint32_t hash_lookup(hash_t hash, bool prefetch_addr = kUsePrefetch) {
		hash &= hash_mask;
		uint32_t ret = hash + hashStart;
		if (prefetch_addr) {	
			prefetch(hash_table + ret);
		}
		return ret;
	}

	void setMemUsage(uint32_t usage) {
		mem_usage = usage;
	}

	CM(uint32_t mem = 8, bool lzp_enabled = kUseLZP)
		: mem_usage(mem), opt_var(0), lzp_enabled_(lzp_enabled), profile_(kProfileBinary), force_profile_(false) {
		remain_bytes_ = std::numeric_limits<uint64_t>::max();
	}

	bool setOpt(uint32_t var) {
		// if (var >= 7 && var < 13) return false;
		opt_var = var;
		word_model.setOpt(var);
		match_model.setOpt(var);
		sse.setOpt(var);
		return true;
	}

	forceinline bool modelEnabled(Model model) const {
		return (enabled_models_ & (1U << static_cast<uint32_t>(model))) != 0;
	}
	forceinline void setEnabledModels(uint32_t models) {
		enabled_models_ = models;
		calculateMaxOrder();
	}
	forceinline void enableModel(Model model) {
		enabled_models_ |= 1 << static_cast<size_t>(model);
		calculateMaxOrder();
	}

	// This is sort of expensive, don't call often.
	void calculateMaxOrder() {
		max_order_ = 0;
		for (size_t order = 0; order <= kMaxOrder; ++order) {
			if (modelEnabled(static_cast<Model>(kModelOrder0 + order))) {
				max_order_ = order;
			}
		}
		max_order_ = std::max(match_model_order_, max_order_);
	}

	void setMatchModelOrder(size_t order) {
		match_model_order_ = order ? order - 1 : 0;
		calculateMaxOrder();
	}
	
	void init() {
		const auto start = clock();

		table.build(opt_var);
		
		mixer_mask = 0x1FFFF;
		mixers.resize(static_cast<uint32_t>(kProfileCount) * (mixer_mask + 1));
		for (auto& mixer : mixers) mixer.init(382);

		// Lzp
		lzp_mixers.resize(256);
		for (auto& mixer : lzp_mixers) mixer.init(382);
		for (size_t len = 1; len < kMaxMatch; ++len) {
			lzp_mdl_[len].setP(static_cast<uint32_t>(kMaxValue - (kMaxValue * 2) / len));
		}

		for (auto& s : mixer_skip) s = 0;
		
		NSStateMap<kShift> sm;
		sm.build();

		sse.init(257 * 256, &table);
		sse2.init(257 * 256, &table);
		mixer_sse_ctx = 0;
		sse_ctx = 0;
		
		hash_mask = ((2 * MB) << mem_usage) / sizeof(hash_table[0]) - 1;
		hash_alloc_size = hash_mask + hashStart + (1 << huffman_len_limit);
		hash_storage.resize(hash_alloc_size); // Add extra space for ctx.
		hash_table = reinterpret_cast<uint8_t*>(hash_storage.getData()); // Here is where the real hash table starts

		buffer.resize((MB / 4) << mem_usage, sizeof(uint32_t));

		// Match model.
		match_model.resize(buffer.getSize() / 2);
		match_model.init(MatchModelType::kMinMatch, 80U);
		match_model_order_ = 0;
		fixed_match_probs_.resize(81 * 2);
		int magic_array[100];
		for (size_t i = 1; i < 100; ++i) {
			magic_array[i] = (kMaxValue / 2) / i;
		}
		if (true) {
			magic_array[10] -= 69;
			magic_array[11] -= 19;
			magic_array[12] -= 4;
			magic_array[13] += 0;
			magic_array[14] += 0;
		}
		for (size_t i = 2; i < fixed_match_probs_.size(); ++i) {
			// const size_t len = 6 + i / 2;
			const size_t len = 6 + i / 2;
			auto delta = magic_array[len];
			if ((i & 1) != 0) {
				fixed_match_probs_[i] = table.st(kMaxValue - 1 - delta);
			} else {
				fixed_match_probs_[i] = table.st(delta); 
			}
		}

		for (size_t i = 0; i < 256; ++i) {
			binary_mask_map_[i] = (i < 1) + (i < 32) + (i < 64) + (i < 128) + (i < 255) + (i < 142) + (i < 138) +
				(i < 140) + (i < 137) + (i < 97);
			// binary_mask_map_[i] = (i < 1) + (i < 32) + (i < 59) + (i < 64) + (i < 91) + (i < 142) + (i < 255); // + (i < 58) + (i < 48);
			// binary_mask_map_[i] += (i < 128) + (i < 131) + (i < 137) + (i < 139) + (i < 140) + (i < 142);
			text_mask_map_[i] = (i < 91) + (i < 123) + (i < 47) + (i < 62) + (i < 46) + (i < 33) + (i < 28);
			text_mask_map_[i] += (i < 58) + (i < 210) + (i < 92) + (i < 40) + (i < 97) + (i < 42) + (i < 59) + (i < 48);
		}
		current_mask_map_ = binary_mask_map_;

		word_model.init();
		for (size_t i = 0; i <= WordModel::kMaxLen; ++i) {
			word_model_ctx_map_[i] = (i >= 1) + (i >= 2) + (i >= 3) + (i >= 4) + (i >= 5) + (i >= 6) + (i >= 8);
		}

		// Optimization
		for (uint32_t i = 0; i < num_states; ++i) {
			for (uint32_t j = 0; j < 2; ++j) {
				state_trans[i][j] = sm.getTransition(i, j);
			}
		}

		unsigned short initial_probs[][256] = {
			{1895,1286,725,499,357,303,156,155,154,117,107,117,98,66,125,64,51,107,78,74,66,68,47,61,56,61,77,46,43,59,40,41,28,22,37,42,37,33,25,29,40,42,26,47,64,31,39,0,0,1,19,6,20,1058,391,195,265,194,240,132,107,125,151,113,110,91,90,95,56,105,300,22,831,997,1248,719,1194,159,156,1381,689,581,476,400,403,388,372,360,377,1802,626,740,664,1708,1141,1012,973,780,883,713,1816,1381,1621,1528,1865,2123,2456,2201,2565,2822,3017,2301,1766,1681,1472,1082,983,2585,1504,1909,2058,2844,1611,1349,2973,3084,2293,3283,2350,1689,3093,2502,1759,3351,2638,3395,3450,3430,3552,3374,3536,3560,2203,1412,3112,3591,3673,3588,1939,1529,2819,3655,3643,3731,3764,2350,3943,2640,3962,2619,3166,2244,1949,2579,2873,1683,2512,1876,3197,3712,1678,3099,3020,3308,1671,2608,1843,3487,3465,2304,3384,3577,3689,3671,3691,1861,3809,2346,1243,3790,3868,2764,2330,3795,3850,3864,3903,3933,3963,3818,3720,3908,3899,1950,3964,3924,3954,3960,4091,2509,4089,2512,4087,2783,2073,4084,2656,2455,3104,2222,3683,2815,3304,2268,1759,2878,3295,3253,2094,2254,2267,2303,3201,3013,1860,2471,2396,2311,3345,3731,3705,3709,2179,3580,3350,2332,4009,3996,3989,4032,4007,4023,2937,4008,4095,2048,},
			{2065,1488,826,573,462,381,254,263,197,158,175,57,107,95,95,104,89,69,76,86,83,61,44,64,49,53,63,46,80,29,57,28,55,35,41,33,43,42,37,57,20,35,53,25,11,10,29,16,16,9,27,15,17,1459,370,266,306,333,253,202,152,115,151,212,135,142,148,128,93,102,810,80,1314,2025,2116,846,2617,189,195,1539,775,651,586,526,456,419,400,335,407,2075,710,678,810,1889,1219,1059,891,785,933,859,2125,1325,1680,1445,1761,2054,2635,2366,2499,2835,2996,2167,1536,1676,1342,1198,874,2695,1548,2002,2400,2904,1517,1281,2981,3177,2402,3366,2235,1535,3171,2282,1681,3201,2525,3405,3438,3542,3535,3510,3501,3514,2019,1518,3151,3598,3618,3597,1904,1542,2903,3630,3655,3671,3761,2054,3895,2512,3935,2451,3159,2323,2223,2722,3020,2033,2557,2441,3333,3707,1993,3154,3352,3576,2153,2849,1992,3625,3629,2459,3643,3703,3703,3769,3753,2274,3860,2421,1565,3859,3877,2580,2061,3781,3807,3864,3931,3907,3924,3807,3835,3852,3910,2197,3903,3946,3962,3975,4068,2662,4062,2662,4052,2696,2080,4067,2645,2424,2010,2325,3186,1931,2033,2514,831,2116,2060,2148,1988,1528,1034,938,2016,1837,1916,1512,1536,1553,2036,2841,2827,3000,2444,2571,2151,2078,4067,4067,4063,4079,4077,4075,3493,4081,4095,2048,},
			{1910,1427,670,442,319,253,222,167,183,142,117,119,118,95,82,50,88,92,71,57,53,56,58,52,58,57,32,47,71,37,37,44,42,43,30,25,22,44,16,21,28,64,15,53,27,24,24,12,7,41,28,8,11,1377,414,343,397,329,276,233,200,190,194,230,178,161,157,133,122,110,1006,139,1270,1940,1896,871,2411,215,255,1637,860,576,586,531,573,407,465,353,320,2027,693,759,830,1964,1163,1078,919,923,944,703,2011,1305,1743,1554,1819,2005,2562,2213,2577,2828,2864,2184,1509,1725,1389,1359,1029,2409,1423,2011,2221,2769,1406,1234,2842,3177,2267,3392,2201,1607,3069,2339,1684,3275,2443,3346,3431,3444,3558,3382,3482,3425,1811,1558,3048,3603,3603,3486,1724,1504,2796,3632,3716,3647,3709,2010,3928,2231,3865,2188,3083,2329,2202,2520,2953,2157,2497,2367,3480,3727,1990,3121,3313,3536,2251,2838,2068,3694,3517,2316,3656,3637,3679,3800,3674,2215,3807,2371,1565,3879,3785,2440,2056,3853,3849,3850,3931,3946,3955,3807,3819,3902,3926,2196,3906,3978,3947,3964,4058,2636,4050,2637,4071,2692,2176,4063,2627,2233,1749,2178,2683,1561,1526,2220,947,1973,1801,1902,1652,1434,843,675,1630,1784,1890,1413,1368,1618,1703,2574,2651,2421,2088,2120,1785,2026,4055,4057,4069,4063,4082,4070,3234,4062,4094,2048,},
			{2059,1591,559,339,374,264,145,105,137,137,113,124,89,76,105,69,59,61,41,74,54,46,39,61,36,22,24,57,49,52,41,42,19,37,36,16,21,63,16,50,14,25,33,15,25,23,56,14,41,25,19,20,13,1378,495,348,498,406,255,305,240,237,202,221,187,177,208,164,115,143,1141,178,1250,1984,1770,933,2323,220,259,1482,827,568,666,542,455,466,463,375,399,1998,709,753,811,1770,1225,1182,934,845,873,795,1878,1411,1724,1540,1831,2137,2448,2288,2549,2774,2899,2147,1595,1792,1337,1326,1090,2446,1533,1917,2166,2716,1451,1257,2764,3052,2192,3290,2226,1538,3140,2297,1851,3263,2449,3282,3337,3473,3549,3339,3556,3311,1969,1639,2901,3429,3552,3539,1659,1449,2781,3589,3654,3651,3729,2009,3843,2082,3844,2049,3033,2384,2209,2469,2874,2024,2558,2329,3586,3690,1944,3195,3289,3547,2334,2926,2180,3595,3566,2560,3595,3614,3682,3774,3709,2318,3769,2519,1656,3865,3858,2350,2069,3780,3802,3893,3846,3982,3933,3806,3853,3913,3949,2322,3971,3971,3992,3977,4063,2625,4055,2608,4057,2665,2001,4064,2640,2131,1699,2123,2618,1591,1392,2225,1075,1879,1758,1834,1658,1410,804,688,1600,1852,2015,1685,1551,1629,1527,2435,2387,2210,1995,2023,1816,1711,4062,4048,4050,4084,4063,4078,3311,4087,4095,2048,},
			{2053,1468,621,350,253,230,171,142,158,137,103,145,133,95,88,71,43,80,98,55,56,61,71,32,47,42,61,44,64,58,42,71,53,40,37,50,33,31,40,50,33,41,36,52,19,49,41,22,33,17,14,24,31,1515,550,403,470,433,374,362,248,286,233,323,251,198,194,188,148,206,953,122,1485,2197,1844,900,2585,277,215,1556,779,615,621,474,531,468,423,362,408,1887,732,859,945,1751,1141,1067,944,898,874,827,1897,1434,1687,1547,1970,2091,2394,2273,2407,2650,2800,2175,1743,1792,1483,1415,1007,2307,1796,1966,2150,2664,1658,1451,2632,3041,2183,3301,2170,1498,2999,2249,1843,3304,2498,3368,3352,3476,3499,3377,3559,3182,1964,1888,2852,3462,3489,3505,1819,1221,2735,3547,3643,3659,3692,2179,3828,1987,3847,1935,3153,2245,2185,2242,2730,2101,2765,2617,3576,3760,2085,3019,3170,3437,2529,2931,2149,3559,3553,2645,3608,3620,3684,3778,3673,2334,3798,2607,1520,3834,3817,2530,2147,3749,3782,3862,3801,3905,3928,3835,3936,3912,3981,2355,4004,3956,3958,4019,4046,2497,4009,2571,4038,2574,1910,4039,2602,2191,1783,2207,2882,1689,1505,2423,1110,2271,1854,1908,1848,1341,949,796,1767,1749,2199,1705,1588,1824,1842,2575,2532,2408,2125,2224,1904,1720,4087,4073,4074,4062,4078,4070,3348,4072,4095,2048,},
			{2084,1461,630,443,264,243,221,190,164,167,122,120,134,75,126,88,97,102,145,62,60,80,109,47,97,75,66,89,73,59,76,59,53,68,67,31,53,56,38,11,31,60,54,39,35,51,16,26,33,13,12,21,50,1439,595,447,492,450,354,316,286,253,208,343,276,226,202,141,150,170,756,104,1178,2141,2050,923,2665,287,202,1634,839,631,691,486,518,480,468,414,370,2020,850,832,884,1787,1192,1161,1011,885,902,758,1817,1455,1634,1617,1886,2059,2354,2340,2403,2665,2782,2163,1773,1870,1545,1449,1108,2245,1765,2002,2106,2623,1679,1474,2604,3028,2176,3308,2215,1626,3066,2318,1870,3252,2463,3323,3346,3424,3497,3390,3536,3216,1961,1842,2877,3462,3557,3501,1761,1173,2731,3527,3645,3661,3748,2165,3792,1949,3895,1884,3080,2346,2157,2194,2664,1977,2828,2613,3562,3779,2221,3022,3146,3408,2585,2942,2311,3611,3532,2736,3578,3545,3732,3695,3679,2376,3755,2676,1454,3825,3860,2804,2097,3763,3737,3830,3832,3881,3917,3832,3879,3887,3950,2306,3963,3905,3960,3975,4027,2522,4027,2515,4034,2587,1894,4040,2519,2212,1862,2288,3082,1842,1741,2598,1069,2414,1966,1972,1960,1383,962,993,1922,1843,2264,1467,1434,1684,2075,2831,2781,2743,2412,2407,2090,1955,4084,4085,4092,4079,4082,4088,3658,4083,4095,2048,},
			{2106,1571,503,376,243,143,106,181,107,103,86,81,66,92,79,47,49,68,89,97,49,38,63,60,35,52,65,27,39,46,44,39,37,30,43,42,41,26,25,39,21,26,10,23,25,36,46,24,17,15,11,10,9,1102,414,254,423,342,302,220,245,165,204,250,174,131,166,100,124,152,410,76,1183,1391,1836,839,1641,162,213,1461,791,656,555,443,493,437,439,407,343,1754,744,673,839,1759,1417,1115,1046,983,974,817,1900,1495,1875,1602,1861,2144,2460,2187,2468,2884,2887,2272,1784,1729,1395,1307,1019,2544,1400,1775,2214,2763,1616,1241,2823,3273,2277,3306,2345,1790,3142,2593,2017,3238,2590,3314,3399,3390,3470,3448,3525,3427,2256,1696,3071,3478,3624,3531,1949,1750,2745,3622,3698,3644,3737,2369,3919,2601,3970,2452,3073,2151,2179,2479,2660,1748,2587,2109,3606,3878,2263,2790,3108,3401,2257,2828,2077,3482,3429,2347,3558,3587,3552,3679,3699,2178,3796,2402,1565,3844,3884,2782,2055,3784,3756,3827,3906,3917,3907,3915,3965,3927,4017,2069,3996,4005,4003,3969,4079,2655,4073,2573,4055,2712,2083,4074,2587,2371,2294,2267,3486,2107,2788,2183,1566,2605,2683,2611,1919,1839,1724,1811,2726,2248,1971,2383,2230,2225,2706,3489,3383,3408,2149,3095,2434,2097,4009,3996,3989,4032,4007,4023,2937,4008,4095,2048,},
			{1977,1521,731,469,506,321,288,197,190,139,144,119,169,181,108,135,77,96,63,84,61,57,67,86,33,86,42,70,64,70,74,46,33,31,34,61,29,54,30,26,53,44,28,48,28,58,50,7,26,16,22,28,26,1185,496,372,461,364,305,209,255,243,279,292,202,200,198,154,180,155,551,54,1041,1505,1869,781,1749,289,251,1439,922,725,635,570,604,580,492,414,380,1902,763,803,902,1813,1395,1311,1122,1017,1069,966,1846,1486,1739,1535,1841,2022,2438,2237,2463,2709,2739,2295,1809,1730,1488,1241,1112,2272,1601,1839,2084,2595,1587,1517,2816,3064,2257,3397,2309,1867,3132,2534,2035,3183,2692,3348,3367,3339,3495,3388,3528,3485,2086,1683,3148,3509,3683,3558,2008,1612,2961,3602,3664,3685,3668,2167,3841,2558,3870,2587,3072,2152,2067,2435,2862,2092,2575,1983,3379,3716,2084,3023,3311,3511,2375,3030,2155,3608,3611,2509,3596,3641,3683,3714,3674,2310,3779,2613,1651,3839,3860,2715,2155,3769,3802,3890,3934,3915,3924,3714,3841,3856,3900,2172,3922,3885,3924,3999,4015,2798,4005,2788,4004,2633,2133,4000,2810,2295,1763,2062,3069,1694,2062,2103,1226,2346,2008,2150,1728,1436,1039,1082,1962,1656,1906,2112,2053,1924,2119,3190,3197,3038,1880,2626,2009,1999,4009,3996,3989,4032,4007,4023,2937,4008,4095,2048,},
			{1977,1521,731,469,506,321,288,197,190,139,144,119,169,181,108,135,77,96,63,84,61,57,67,86,33,86,42,70,64,70,74,46,33,31,34,61,29,54,30,26,53,44,28,48,28,58,50,7,26,16,22,28,26,1185,496,372,461,364,305,209,255,243,279,292,202,200,198,154,180,155,551,54,1041,1505,1869,781,1749,289,251,1439,922,725,635,570,604,580,492,414,380,1902,763,803,902,1813,1395,1311,1122,1017,1069,966,1846,1486,1739,1535,1841,2022,2438,2237,2463,2709,2739,2295,1809,1730,1488,1241,1112,2272,1601,1839,2084,2595,1587,1517,2816,3064,2257,3397,2309,1867,3132,2534,2035,3183,2692,3348,3367,3339,3495,3388,3528,3485,2086,1683,3148,3509,3683,3558,2008,1612,2961,3602,3664,3685,3668,2167,3841,2558,3870,2587,3072,2152,2067,2435,2862,2092,2575,1983,3379,3716,2084,3023,3311,3511,2375,3030,2155,3608,3611,2509,3596,3641,3683,3714,3674,2310,3779,2613,1651,3839,3860,2715,2155,3769,3802,3890,3934,3915,3924,3714,3841,3856,3900,2172,3922,3885,3924,3999,4015,2798,4005,2788,4004,2633,2133,4000,2810,2295,1763,2062,3069,1694,2062,2103,1226,2346,2008,2150,1728,1436,1039,1082,1962,1656,1906,2112,2053,1924,2119,3190,3197,3038,1880,2626,2009,1999,4009,3996,3989,4032,4007,4023,2937,4008,4095,2048,},
			{1977,1521,731,469,506,321,288,197,190,139,144,119,169,181,108,135,77,96,63,84,61,57,67,86,33,86,42,70,64,70,74,46,33,31,34,61,29,54,30,26,53,44,28,48,28,58,50,7,26,16,22,28,26,1185,496,372,461,364,305,209,255,243,279,292,202,200,198,154,180,155,551,54,1041,1505,1869,781,1749,289,251,1439,922,725,635,570,604,580,492,414,380,1902,763,803,902,1813,1395,1311,1122,1017,1069,966,1846,1486,1739,1535,1841,2022,2438,2237,2463,2709,2739,2295,1809,1730,1488,1241,1112,2272,1601,1839,2084,2595,1587,1517,2816,3064,2257,3397,2309,1867,3132,2534,2035,3183,2692,3348,3367,3339,3495,3388,3528,3485,2086,1683,3148,3509,3683,3558,2008,1612,2961,3602,3664,3685,3668,2167,3841,2558,3870,2587,3072,2152,2067,2435,2862,2092,2575,1983,3379,3716,2084,3023,3311,3511,2375,3030,2155,3608,3611,2509,3596,3641,3683,3714,3674,2310,3779,2613,1651,3839,3860,2715,2155,3769,3802,3890,3934,3915,3924,3714,3841,3856,3900,2172,3922,3885,3924,3999,4015,2798,4005,2788,4004,2633,2133,4000,2810,2295,1763,2062,3069,1694,2062,2103,1226,2346,2008,2150,1728,1436,1039,1082,1962,1656,1906,2112,2053,1924,2119,3190,3197,3038,1880,2626,2009,1999,4009,3996,3989,4032,4007,4023,2937,4008,4095,2048,},
		};

		for (uint32_t i = 0;i < kPredArrays; ++i) {
			for (uint32_t j = 0; j < kProbCtx;++j) {
				for (uint32_t k = 0; k < num_states; ++k) {
					auto& pr = preds[i][j][k];
					pr.init();
					if (kFixedProbs) {
						pr.setP(table.st(initial_probs[std::min(j, 9U)][k]));
					} else {
						pr.setP(initial_probs[std::min(j, 9U)][k]);
					}
				}
			}
		}

		setDataProfile(kProfileBinary);
		owhash = 0;

		// Statistics
		if (kStatistics) {
			for (auto& c : mixer_skip) c = 0;
			other_count_ = match_count_ = non_match_count_ = 0;
			std::cout << "Setup took: " << clock() - start << std::endl << std::endl;
			lzp_bit_match_bytes_ = lzp_bit_miss_bytes_ = lzp_miss_bytes_ = normal_bytes_ = 0;
			for (auto& len : match_hits_) len = 0;
			for (auto& len : match_miss_) len = 0;
		}
	}

	forceinline uint32_t hashFunc(uint32_t a, uint32_t b) const {
		b += a;
		b += rotate_left(b, 11);
		return b ^ (b >> 6);
	}

	forceinline CMMixer* getProfileMixers(CMProfile profile) {
		return &mixers[static_cast<uint32_t>(profile) * (mixer_mask + 1)];
	}

	void calcMixerBase() {
		uint32_t mixer_ctx = current_mask_map_[owhash & 0xFF];
		const bool word_enabled = modelEnabled(kModelWord1);
		if (word_enabled) {
			mixer_ctx <<= 3;
			mixer_ctx |= word_model_ctx_map_[word_model.getLength()];
		}
		
		const size_t mm_len = match_model.getLength();
		mixer_ctx <<= 2;
		if (mm_len != 0) {
			if (word_enabled) {
				mixer_ctx |= 1 +
					(mm_len >= match_model.kMinMatch + 2) + 
					(mm_len >= match_model.kMinMatch + 6) + 
					0;
			} else {
				mixer_ctx |= 1 +
					(mm_len >= match_model.kMinMatch + 1) + 
					(mm_len >= match_model.kMinMatch + 4) +
					0;
			}
		}		
		mixer_base = cur_profile_mixers_ + (mixer_ctx << 8);
	}

	void calcProbBase() {
		cur_preds = &preds[static_cast<size_t>(profile_)];
	}

	forceinline byte nextState(uint8_t state, uint32_t bit, uint32_t ctx) {
		if (!kFixedProbs) {
			(*cur_preds)[ctx][state].update(bit, 9);
		}
		return state_trans[state][bit];
	}
	
	forceinline int getP(uint8_t state, uint32_t ctx) const {
		int p = (*cur_preds)[ctx][state].getP();
		if (!kFixedProbs) {
			p = table.st(p);
		}
		return p;
	}

	enum BitType {
		kBitTypeLZP,
		kBitTypeMatch,
		kBitTypeNormal,
	};

	template <const bool decode, BitType kBitType, typename TStream>
	size_t processBit(TStream& stream, size_t bit, size_t* base_contexts, size_t ctx, size_t mixer_ctx) {
		const auto mm_l = match_model.getLength();
	
		uint8_t 
			*no_alias sp0 = nullptr, *no_alias sp1 = nullptr, *no_alias sp2 = nullptr, *no_alias sp3 = nullptr, *no_alias sp4 = nullptr,
			*no_alias sp5 = nullptr, *no_alias sp6 = nullptr, *no_alias sp7 = nullptr, *no_alias sp8 = nullptr, *no_alias sp9 = nullptr;
		uint8_t s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0, s5 = 0, s6 = 0, s7 = 0, s8 = 0, s9 = 0;

		uint32_t p;
		int p0 = 0, p1 = 0, p2 = 0, p3 = 0, p4 = 0, p5 = 0, p6 = 0, p7 = 0, p8 = 0, p9 = 0;
		if (kBitType == kBitTypeLZP) {
			if (inputs > 0) {
				if (kFixedMatchProbs) {
					p0 = fixed_match_probs_[mm_l * 2 + 1];
				} else {
					p0 = match_model.getP(table.getStretchPtr(), 1);
				}
			}
		} else if (mm_l == 0) {
			if (inputs > 0) {
				sp0 = &hash_table[base_contexts[0] ^ ctx];
				s0 = *sp0;
				p0 = getP(s0, 0);
			}
			if (false && kBitType != kBitTypeLZP && profile_ == kProfileBinary) {
				p0 += getP(hash_table[s2pos + (owhash & 0xFF00) + ctx], 0);
				p0 += getP(hash_table[s3pos + ((owhash & 0xFF0000) >> 8) + ctx], 0);
				p0 += getP(hash_table[s4pos + ((owhash & 0xFF000000) >> 16) + ctx], 0);
			}
		} else {
			if (inputs > 0) {
				if (kFixedMatchProbs) {
					p0 = fixed_match_probs_[mm_l * 2 + match_model.getExpectedBit()];
				} else {
					p0 = match_model.getP(table.getStretchPtr(), match_model.getExpectedBit());
				}
			}
		}
		if (inputs > 1) {
			sp1 = &hash_table[base_contexts[1] ^ ctx];
			s1 = *sp1;
			p1 = getP(s1, 1);
		}
		if (inputs > 2) {
			sp2 = &hash_table[base_contexts[2] ^ ctx];
			s2 = *sp2;
			p2 = getP(s2, 2);
		}
		if (inputs > 3) {
			sp3 = &hash_table[base_contexts[3] ^ ctx];
			s3 = *sp3;
			p3 = getP(s3, 3);
		}
		if (inputs > 4) {
			sp4 = &hash_table[base_contexts[4] ^ ctx];
			s4 = *sp4;
			p4 = getP(s4, 4);
		}
		if (inputs > 5) {
			sp5 = &hash_table[base_contexts[5] ^ ctx];
			s5 = *sp5;
			p5 = getP(s5, 5);
		}
		if (inputs > 6) {
			sp6 = &hash_table[base_contexts[6] ^ ctx];
			s6 = *sp6;
			p6 = getP(s6, 6);
		}
		if (inputs > 7) {
			sp7 = &hash_table[base_contexts[7] ^ ctx];
			s7 = *sp7;
			p7 = getP(s7, 7);
		}
		if (inputs > 8) {
			sp8 = &hash_table[base_contexts[8] ^ ctx];
			s8 = *sp8;
			p8 = getP(s8, 8);
		}
		if (inputs > 9) {
			sp9 = &hash_table[base_contexts[9] ^ ctx];
			s9 = *sp9;
			p9 = getP(s9, 9);
		}
		auto* const cur_mixer = &mixer_base[mixer_ctx];
		int stp = cur_mixer->p(9, p0, p1, p2, p3, p4, p5, p6, p7, p8, p9);
		if (stp < kMinST) stp = kMinST;
		if (stp >= kMaxST) stp = kMaxST - 1;
		int mixer_p = table.squnsafe(stp); // Mix probabilities.
		p = mixer_p;
		if (kUseSSE) {
			if (kUseLZP) {
				if (!kUseSSEOnlyLZP || kBitType == kBitTypeLZP || sse_ctx != 0) {
					if (kBitType == kBitTypeLZP) {
						// p = sse2.p(stp + kMaxValue / 2, sse_ctx + mm_l);
						p = sse2.p(stp + kMaxValue / 2, sse_ctx + mm_l);
					} else {
						p = sse.p(stp + kMaxValue / 2, sse_ctx + mixer_ctx);
					}
					p += p == 0;
				} else if (false) {
					// This SSE is disabled for speed.
					p = (p + sse.p(stp, mixer_sse_ctx & 0xFF)) / 2;
					p += p == 0;
				}
			} else if (false) {
				p = (p + sse.p(stp + kMaxValue / 2, (mixer_sse_ctx & 0xFF) * 256 + mixer_ctx)) / 2;
				p += p == 0;
			}
		}

		if (decode) { 
			bit = ent.getDecodedBit(p, kShift);
		}
		dcheck(bit < 2);

		// Returns false if we skipped the update due to a low error, should happen moderately frequently on highly compressible files.
		const bool ret = cur_mixer->update(mixer_p, bit, kShift, 28, 1, p0, p1, p2, p3, p4, p5, p6, p7, p8, p9);
		// Only update the states / predictions if the mixer was far enough from the bounds, helps 15k on enwik8 and 1-2sec.
		if (ret) {
			if (kBitType == kBitTypeLZP) {
				match_model.updateCurMdl(1, bit);
			} else if (mm_l == 0) {
				if (inputs > 0) *sp0 = nextState(s0, bit, 0);
				if (false && kBitType != kBitTypeLZP && profile_ == kProfileBinary) {
					hash_table[s2pos + (owhash & 0xFF00) + ctx] = nextState(hash_table[s2pos + (owhash & 0xFF00) + ctx], bit, 0);
					hash_table[s3pos + ((owhash & 0xFF0000) >> 8) + ctx] = nextState(hash_table[s3pos + ((owhash & 0xFF0000) >> 8) + ctx], bit, 0);
					hash_table[s4pos + ((owhash & 0xFF000000) >> 16) + ctx] = nextState(hash_table[s4pos + ((owhash & 0xFF000000) >> 16) + ctx], bit, 0);
				}
			}
			if (inputs > 1) *sp1 = nextState(s1, bit, 1);
			if (inputs > 2) *sp2 = nextState(s2, bit, 2);
			if (inputs > 3) *sp3 = nextState(s3, bit, 3);
			if (inputs > 4) *sp4 = nextState(s4, bit, 4);
			if (inputs > 5) *sp5 = nextState(s5, bit, 5);
			if (inputs > 6) *sp6 = nextState(s6, bit, 6);
			if (inputs > 7) *sp7 = nextState(s7, bit, 7);
			if (inputs > 8) *sp8 = nextState(s8, bit, 8);
			if (inputs > 9) *sp9 = nextState(s9, bit, 9);
		}
		if (kUseSSE) {
			if (kUseLZP) {
				if (kBitType == kBitTypeLZP) {
					sse2.update(bit);
				} else if (!kUseSSEOnlyLZP || sse_ctx != 0) {
					sse.update(bit);
				}
			} else {
				mixer_sse_ctx = mixer_sse_ctx * 2 + ret;
			}
		}
		if (kBitType != kBitTypeLZP) {
			match_model.updateBit(bit);
		}
		if (kStatistics) ++mixer_skip[ret];

		if (decode) {
			ent.Normalize(stream);
		} else {
			ent.encode(stream, bit, p, kShift);
		}
		return bit;
	}

	template <const bool decode, typename TStream>
	size_t processNibble(TStream& stream, size_t c, size_t* base_contexts, size_t ctx_add, size_t ctx_add2) {
		uint32_t huff_state = huff.start_state, code = 0;
		if (!decode) {
			if (use_huffman) {
				const auto& huff_code = huff.getCode(c);
				code = huff_code.value << (sizeof(uint32_t) * 8 - huff_code.length);
			} else {
				code = c << (sizeof(uint32_t) * 8 - 4);
			}
		}
		size_t base_ctx = 1;
		for (;;) {
			size_t ctx;
			// Get match model prediction.
			if (use_huffman) {
				ctx = huff_state;
			} else {
				ctx = ctx_add + base_ctx;
			}
			size_t bit = 0;
			if (!decode) {
				bit = code >> (sizeof(uint32_t) * 8 - 1);
				code <<= 1;
			}
			bit = processBit<decode, kBitTypeNormal>(stream, bit, base_contexts, ctx_add2 + ctx, ctx);

			// Encode the bit / decode at the last second.
			if (use_huffman) {
				huff_state = huff.getTransition(huff_state, bit);
				if (huff.isLeaf(huff_state)) {
					return huff_state;
				}
			} else {
				base_ctx = base_ctx * 2 + bit;
				if ((base_ctx & 16) != 0) break;
			}
		}
		return base_ctx ^ 16;
	}

	template <const bool decode, typename TStream>
	size_t processByte(TStream& stream, uint32_t c = 0) {
		size_t base_contexts[inputs] = { o0pos }; // Base contexts
		auto* ctx_ptr = base_contexts;

		uint32_t random_words[] = {
			0x4ec457ce, 0x2f85195b, 0x4f4c033c, 0xc0de7294, 0x224eb711,
			0x9f358562, 0x00d46a63, 0x0fb6c35c, 0xc4dca450, 0x9ddc89f7,
			0x6f4a0403, 0x1fff619f, 0x83e56bd9, 0x0448a62c, 0x4de22c02,
			0x418700b2, 0x7e546bf8, 0xac2bb7a9, 0xc9e6cbcb, 0x4a8b4a07,
			0x486b3b68, 0x9e944172, 0xb11b7dd5, 0xaa0cd8a7, 0x4a6c6fa7,
		};

		const size_t bpos = buffer.getPos();
		const size_t blast = bpos - 1; // Last seen char
		const size_t
			p0 = static_cast<byte>(owhash >> 0),
			p1 = static_cast<byte>(owhash >> 8),
			p2 = static_cast<byte>(owhash >> 16),
			p3 = static_cast<byte>(owhash >> 24);
		if (modelEnabled(kModelOrder0)) {
			*(ctx_ptr++) = o0pos;
		}
		if (modelEnabled(kModelOrder1)) {
			*(ctx_ptr++) = o1pos + p0 * o0size;
		}
		if (modelEnabled(kModelSparse2)) {
			*(ctx_ptr++) = s2pos + p1 * o0size;
		}
		if (modelEnabled(kModelSparse3)) {
			*(ctx_ptr++) = s3pos + p2 * o0size;
		}
		if (modelEnabled(kModelSparse4)) {
			*(ctx_ptr++) = s4pos + p3 * o0size;
		}
		if (modelEnabled(kModelSparse23)) {
			*(ctx_ptr++) = hash_lookup(hashFunc(p2, hashFunc(p1, 0x37220B98))); // Order 23
		}
		if (modelEnabled(kModelSparse34)) {
			*(ctx_ptr++) = hash_lookup(hashFunc(p3, hashFunc(p2, 0x651A833E))); // Order 34
		}
		if (modelEnabled(kModelWordMask)) {
			uint32_t mask = 0xFFFFFFFF;
			mask ^= 1u << 10u;
			mask ^= 1u << 26u;
			mask ^= 1u << 24u;
			mask ^= 1u << 4u;
			mask ^= 1u << 25u;
			mask ^= 1u << 28u;
			mask ^= 1u << 5u;
			mask ^= 1u << 27u;
			mask ^= 1u << 3u;
			mask ^= 1u << 29u;
			//mask ^= 1u << opt_var;
			*(ctx_ptr++) = hash_lookup((owhash & mask) * 0xac2bb7a9 + 19299412415); // Order 34
		}
		if (modelEnabled(kModelOrder2)) {
			*(ctx_ptr++) = o2pos + (owhash & 0xFFFF) * o0size;
		}
		uint32_t h = hashFunc(owhash & 0xFFFF, 0x4ec457ce);
		uint32_t order = 3;
		size_t expected_char = 0;

		size_t mm_len = 0;
		if (match_model_order_ != 0) {
			match_model.update(buffer);
			if (mm_len = match_model.getLength()) {
				match_model.setCtx(h & 0xFF);
				match_model.updateCurMdl();
				expected_char = match_model.getExpectedChar(buffer);
				uint32_t expected_bits = use_huffman ? huff.getCode(expected_char).length : 8;
				size_t expected_code = use_huffman ? huff.getCode(expected_char).value : expected_char;
				match_model.updateExpectedCode(expected_code, expected_bits);
			}
		}

		for (; order <= max_order_; ++order) {
			h = hashFunc(buffer[bpos - order], h);
			if (modelEnabled(static_cast<Model>(kModelOrder0 + order))) {
				*(ctx_ptr++) = hash_lookup(false ? hashFunc(expected_char, h) : h, true);
			}
		}
		dcheck(order - 1 == match_model_order_);

		if (modelEnabled(kModelWord1)) {
			*(ctx_ptr++) = hash_lookup(word_model.getHash(), false); // Already prefetched.
		}
		if (modelEnabled(kModelWord2)) {
			*(ctx_ptr++) = hash_lookup(word_model.getPrevHash(), false);
		}
		if (modelEnabled(kModelWord12)) {
			*(ctx_ptr++) = hash_lookup(word_model.get01Hash(), false); // Already prefetched.
		}
		if (modelEnabled(kModelMask)) {
			// Model idea from Tangelo, thanks Jan Ondrus.
			mask_model_ *= 16;
			mask_model_ += current_mask_map_[p0];
			*(ctx_ptr++) = hash_lookup(hashFunc(0xaa0cd8a7, mask_model_ * 313), true);
		}
		if (modelEnabled(kModelMatchHashE)) {
			*(ctx_ptr++) = hash_lookup(match_model.getHash(), false);
		}
		match_model.setHash(h);
		dcheck(ctx_ptr - base_contexts <= inputs + 1);
		sse_ctx = 0;

		uint64_t cur_pos = kStatistics ? stream.tell() : 0;

		calcMixerBase();
		if (mm_len > 0) {
			if (kStatistics && !decode) {
				++(expected_char == c ? match_count_ : non_match_count_);
			}
			if (lzp_enabled_) {
				size_t extra_len = mm_len - match_model.getMinMatch();
				cur_preds = &preds[kPredArrays - 1];
				dcheck(mm_len >= match_model.getMinMatch());
				size_t bit = decode ? 0 : expected_char == c;
				sse_ctx = 256 * (1 + expected_char);
#if 1
				// mixer_base = getProfileMixers(profile) + 256 * expected_char;
				bit = processBit<decode, kBitTypeLZP>(stream, bit, base_contexts, expected_char ^ 256, 0);
				// calcMixerBase();
#else 
				auto& mdl = lzp_mdl_[mm_len];
				int p = mdl.getP();
				p += p == 0;
				if (decode) {
					bit = ent.decode(stream, p, kShift);
				} else {
					ent.encode(stream, bit, p, kShift);
				}
				mdl.update(bit, 7);
#endif
				if (kStatistics) {
					const uint64_t after_pos = kStatistics ? stream.tell() : 0;
					(bit ? lzp_bit_match_bytes_ : lzp_bit_miss_bytes_) += after_pos - cur_pos; 
					cur_pos = after_pos;
					++(bit ? match_hits_ : match_miss_)[mm_len + match_model_order_ - 4];
				}
				if (bit) {
					return expected_char;
				}
			} 
			calcProbBase();
		} else if (kStatistics) ++other_count_;
		if (false) {
			match_model.resetMatch();
			return c;
		}
		// Non match, do normal encoding.
		size_t huff_state = 0;
		if (use_huffman) {
			huff_state = processNibble<decode>(stream, c, base_contexts, 0, 0);
			if (decode) {
				c = huff.getChar(huff_state);
			}
		} else {
			size_t n1 = processNibble<decode>(stream, c >> 4, base_contexts, 0, 0);
			if (kPrefetchMatchModel) {
				match_model.fetch(n1 << 4);
			}
			size_t n2 = processNibble<decode>(stream, c & 0xF, base_contexts, 15 + (n1 * 15), 0);
			if (decode) {
				c = n2 | (n1 << 4);
			}
			if (kStatistics) {
				(sse_ctx != 0 ? lzp_miss_bytes_ : normal_bytes_) += stream.tell() - cur_pos;
			}
		}

		return c;
	}

	static CMProfile profileForDetectorProfile(Detector::Profile profile) {
		if (profile == Detector::kProfileText) {
			return kProfileText;
		}
		return kProfileBinary;
	}

	void setDataProfile(CMProfile new_profile) {
		profile_ = new_profile;
		cur_profile_mixers_ = getProfileMixers(profile_);
		mask_model_ = 0;
		word_model.reset();
		setEnabledModels(0);
		size_t idx = 0;
		switch (profile_) {
		case kProfileText: // Text data types (tuned for xml)
#if 0
			if (inputs > idx++) enableModel(kModelOrder0);
			if (inputs > idx++) enableModel(kModelOrder4);
			if (inputs > idx++) enableModel(kModelOrder8);
			if (inputs > idx++) enableModel(kModelOrder2);
			if (inputs > idx++) enableModel(kModelMask);
			if (inputs > idx++) enableModel(kModelWord);
			if (inputs > idx++) enableModel(static_cast<Model>(opt_var));
			setMatchModelOrder(10);
#elif 1
			if (inputs > idx++) enableModel(kModelOrder4);
			if (inputs > idx++) enableModel(kModelWord1);
			if (inputs > idx++) enableModel(kModelOrder6);
			if (inputs > idx++) enableModel(kModelOrder2);
			if (inputs > idx++) enableModel(kModelOrder1);
			if (inputs > idx++) enableModel(kModelMask);
			if (inputs > idx++) enableModel(kModelOrder3);
			if (inputs > idx++) enableModel(kModelOrder8);
			if (inputs > idx++) enableModel(kModelOrder0);
			if (inputs > idx++) enableModel(kModelWord12);
			setMatchModelOrder(10);
#else
			if (inputs > idx++) enableModel(kModelOrder0);
			if (inputs > idx++) enableModel(kModelOrder1);
			if (inputs > idx++) enableModel(kModelOrder2);
			if (inputs > idx++) enableModel(kModelOrder3);
			if (inputs > idx++) enableModel(kModelOrder4);
			if (inputs > idx++) enableModel(kModelOrder5);
			if (inputs > idx++) enableModel(kModelOrder6);
			if (inputs > idx++) enableModel(kModelOrder7);
			setMatchModelOrder(0);
#endif
			// if (inputs > idx++) enableModel(static_cast<Model>(opt_var));
			current_mask_map_ = text_mask_map_;
			break;
		default: // Binary
			assert(profile_ == kProfileBinary);
#if 0
			if (inputs > idx++) enableModel(kModelOrder0);
			if (inputs > idx++) enableModel(kModelOrder4);
			if (inputs > idx++) enableModel(kModelOrder2);
			if (inputs > idx++) enableModel(kModelOrder6);
			// if (inputs > idx++) enableModel(kModelOrder1);
			// if (inputs > idx++) enableModel(kModelOrder3);
			if (inputs > idx++) enableModel(static_cast<Model>(opt_var));
#elif 1
			// Default
			if (inputs > idx++) enableModel(kModelOrder1);
			if (inputs > idx++) enableModel(kModelOrder2);
			if (inputs > idx++) enableModel(kModelSparse34);
			// if (opt_var && inputs > idx++) enableModel(kModelWordMask);
			if (inputs > idx++) enableModel(kModelOrder4);
			if (inputs > idx++) enableModel(kModelSparse23);
			if (inputs > idx++) enableModel(kModelMask);
			if (inputs > idx++) enableModel(kModelSparse4);
			if (inputs > idx++) enableModel(kModelOrder3);
			if (inputs > idx++) enableModel(kModelSparse2);
			if (inputs > idx++) enableModel(kModelSparse3);
#elif 1
			// bitmap profile (rafale.bmp)
			if (inputs > idx++) enableModel(kModelOrder4);
			if (inputs > idx++) enableModel(kModelOrder2);
			if (inputs > idx++) enableModel(kModelOrder12);
			if (inputs > idx++) enableModel(kModelSparse34);
			if (inputs > idx++) enableModel(kModelOrder5);
			if (inputs > idx++) enableModel(kModelMask);
			if (inputs > idx++) enableModel(kModelOrder1);
			if (inputs > idx++) enableModel(kModelOrder7);
#elif 1
			// exe profile (acrord32.exe)
			if (inputs > idx++) enableModel(kModelOrder1);
			if (inputs > idx++) enableModel(kModelOrder2);
			if (inputs > idx++) enableModel(kModelSparse34);
			if (inputs > idx++) enableModel(kModelSparse23);
			if (inputs > idx++) enableModel(kModelMask);
			if (inputs > idx++) enableModel(kModelSparse4);
			if (inputs > idx++) enableModel(kModelOrder3);
			if (inputs > idx++) enableModel(kModelSparse2);
			if (inputs > idx++) enableModel(kModelSparse3);
			if (inputs > idx++) enableModel(kModelOrder0);
#endif
			setMatchModelOrder(7);
			current_mask_map_ = binary_mask_map_;
			break;
		}
		calcProbBase();
	};

	void update(uint32_t c) {;
		if (modelEnabled(kModelWord1) || modelEnabled(kModelWord2) || modelEnabled(kModelWord12)) {
			word_model.update(c);
			if (word_model.getLength() > 2) {
				if (kPrefetchWordModel) {
					hash_lookup(word_model.getHash(), true);
				}
			}
			if (kPrefetchWordModel && modelEnabled(kModelWord12)) {
				hash_lookup(word_model.get01Hash(), true);
			}
		}
		buffer.push(c);
		owhash = (owhash << 8) | static_cast<byte>(c);
	}

	virtual void compress(Stream* in_stream, Stream* out_stream);
	virtual void decompress(Stream* in_stream, Stream* out_stream);
};

#endif
