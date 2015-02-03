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

template <size_t inputs = 6>
class CM : public Compressor {
public:
	// Flags
	static const bool statistics = true;
	static const bool use_prefetch = true;
	static const bool fixed_probs = false;
	// Currently, LZP sux, need to improve.
	static const bool use_lzp = true;
	static const bool kUseSSE = true;

	class SubBlockHeader {
		friend class CM;
	public:
		// Block flags
		byte flags;

		// Which profile we need to set the compressor to.
		DataProfile profile;
		
		SubBlockHeader() {
			flags = 0;
			profile = kBinary;
		}
	};

	// SS table
	static const uint32_t shift = 12;
	static const uint32_t max_value = 1 << shift;
	typedef ss_table<short, max_value, -2 * int(KB), 2 * int(KB), 8> SSTable;
	SSTable table;
	
	//typedef slowBitModel<unsigned short, shift> StationaryModel;
	typedef safeBitModel<unsigned short, shift, 5, 15> BitModel;
	typedef fastBitModel<int, shift, 9, 30> StationaryModel;
	typedef fastBitModel<int, shift, 9, 30> HPStationaryModel;

	WordModel word_model;

	Range7 ent;
	
	typedef MatchModel<HPStationaryModel> MatchModelType;
	MatchModelType match_model;

	// Hash table
	size_t hash_mask;
	size_t hash_alloc_size;
	MemMap hash_storage;
	uint8_t *hash_table;

	// Automatically zerored out.
	static const uint32_t o0size = 0x100;
	static const uint32_t o1size = 0x10000;
	uint8_t *order0, *order1;
	
	// Fast sparse
	uint8_t spar1[256 * 256];
	uint8_t spar2[256 * 256];
	uint8_t spar3[256 * 256];
	StationaryModel sparse_probs[256];

	// Maps from char to 4 bit identifier.
	uint8_t sparse_ctx_map[256];
	uint8_t text_ctx_map[256];

	// Mixers
	uint32_t mixer_mask;
#if USE_MMX
	typedef MMXMixer<inputs, 15, 1> CMMixer;
#else
	typedef Mixer<int, inputs, 17, 11> CMMixer;
#endif
	std::vector<CMMixer> mixers;
	CMMixer *mixer_base;

	// Contexts
	uint32_t owhash; // Order of sizeof(uint32_t) hash.

	// Rotating buffer.
	CyclicBuffer<byte> buffer;

	// Options
	uint32_t opt_var;
	bool use_word;
	bool use_match;
	bool use_sparse;

	// CM state table.
	static const uint32_t num_states = 256;
	uint8_t state_trans[num_states][2];

	// Huffman preprocessing.
	static const bool use_huffman = false;
	static const uint32_t huffman_len_limit = 16;
	Huffman huff;
	
	// End of block signal.
	BitModel end_of_block_mdl;
	DataProfile profile;
	
	typedef bitContextModel<BitModel, 255> BlockFlagModel;
	BlockFlagModel block_flag_model;

	typedef bitContextModel<BitModel, (uint32_t)kProfileCount+1> BlockProfileModel;
	BlockProfileModel block_profile_models[(uint32_t)kProfileCount];

	// Magic mask (tangelo).
	uint32_t mmask;

	// LZP
	HPStationaryModel lzp_match[256 * 256];
	SSE lzp_sse;
	std::vector<CMMixer> lzp_mixers;

	// SM
	typedef StationaryModel PredModel;
	static const size_t kProbCtx = inputs;
	typedef PredModel PredArray[kProbCtx][256];
	PredArray preds[(uint32_t)kProfileCount];
	PredArray* cur_preds;

	// SSE
	size_t apm_ctx;
	SSE sse;
	size_t mixer_sse_ctx;

	// Memory usage
	size_t mem_usage;

	// Statistics
	uint32_t mixer_skip[2];
	uint32_t match_count_;
	uint32_t non_match_count_;
	uint32_t other_count_;

	// TODO: Get rid of this.
	static const uint32_t eof_char = 126;
	
	forceinline uint32_t hash_lookup(hash_t hash, bool prefetch_addr = use_prefetch) {
		hash &= hash_mask;
		uint32_t ret = hash + (o0size + o1size);
		if (prefetch_addr) {	
			prefetch(hash_table + (ret & (uint32_t)~(kCacheLineSize - 1)));
		}
		return ret;
	}

	void setMemUsage(uint32_t usage) {
		mem_usage = usage;
	}

	CM() : mem_usage(8) {
		opt_var = 0;
	}

	void setOpt(uint32_t var) {
		opt_var = var;
		word_model.setOpt(var);
		match_model.setOpt(var);
	}

	void init() {
		table.build(0);
		
		mixer_mask = 0xFFFF;
		mixers.resize(static_cast<uint32_t>(kProfileCount) * (mixer_mask + 1));
		for (auto& mixer : mixers) mixer.init(382);
		lzp_mixers.resize(256);
		for (auto& mixer : lzp_mixers) mixer.init(382);
	
		for (auto& s : mixer_skip) s = 0;
		
		NSStateMap<12> sm;
		sm.build();
		
		sse.init(0x10100, &table);
		lzp_sse.init(0x10100, &table);
		mixer_sse_ctx = 0;
		apm_ctx = 0;

		hash_mask = ((2 * MB) << mem_usage) / sizeof(hash_table[0]) - 1;
		hash_alloc_size = hash_mask + o0size + o1size + (1 << huffman_len_limit);
		hash_storage.resize(hash_alloc_size); // Add extra space for ctx.
		order0 = reinterpret_cast<uint8_t*>(hash_storage.getData());
		order1 = order0 + o0size;
		hash_table = order0; // Here is where the real hash table starts

		word_model.init();

		buffer.resize((MB / 4) << mem_usage, sizeof(uint32_t));

		// Models.
		end_of_block_mdl.init();
		block_flag_model.init();
		for (auto& m : block_profile_models) m.init();

		mmask = 0;

		for (auto& m : lzp_match) m.init();

		// Match model.
		match_model.resize(buffer.getSize() / 2);
		match_model.init(MatchModelType::kMinMatch, 80U, use_huffman ? 16U : 8U);
		
		// Fast sparse models.
		for (auto& c : spar1) c = 0;
		for (auto& c : spar2) c = 0;
		for (auto& c : spar3) c = 0;
		for (auto& m : sparse_probs) m.init();
		for (size_t i = 0; i < 256; ++i) {
			sparse_ctx_map[i] = (i < 1) + (i < 32) + (i < 59) + (i < 64) + (i < 91) + (i < 128) + (i < 137) + (i < 139) + 
				(i < 140) + (i < 142) + (i < 255) + (i < 131); // + (i < 58) + (i < 48);
			text_ctx_map[i] = (i < 95) + (i < 123) + (i < 47) + (i < 64) + (i < 46) + (i < 33) + (i < 58);
		}
		// Optimization
		for (uint32_t i = 0; i < num_states; ++i) {
			for (uint32_t j = 0; j < 2; ++j) {
				state_trans[i][j] = sm.getTransition(i, j);
			}
		}

		unsigned short initial_probs[][256] = {
			{1890,1430,689,498,380,292,171,165,155,137,96,101,70,81,92,72,64,85,76,57,100,65,33,44,49,40,69,39,63,29,46,55,41,63,33,38,35,24,33,32,30,28,33,51,66,28,52,2,15,0,0,1,0,797,442,182,242,194,201,183,153,135,124,171,93,85,122,67,71,93,222,32,631,896,980,647,895,164,93,1375,693,566,497,420,414,411,357,356,319,1740,649,683,681,1717,1170,1025,892,834,835,685,1727,1259,1612,1588,1778,1999,2524,2197,2613,2781,2957,2181,1664,1735,1496,1061,913,2521,1524,1935,2155,2733,1500,1282,2906,3093,2337,3337,2392,1647,3113,2435,1885,3332,2614,3384,3394,3437,3606,3432,3669,3473,2090,1598,3186,3578,3713,3533,1936,1525,2864,3689,3624,3782,3769,2293,3974,2654,3953,2693,3088,2302,1967,2558,2865,1563,2458,1805,3255,3700,1576,2840,2649,3017,1472,2467,2018,3157,3024,2338,3011,3377,3361,3394,3515,1715,3653,2480,1370,3695,3593,2812,2561,3709,3827,3780,3787,3799,3850,3817,3773,3863,3943,1946,3922,3946,3954,3952,4089,2316,4095,2515,4087,3067,2107,4095,2986,2806,3420,2255,3818,3269,3614,2293,2541,3370,3583,3572,2147,2845,2940,2999,3591,3507,1981,3072,2950,2851,3629,3890,3891,3867,2290,3846,3637,2856,4009,3996,3989,4032,4007,4023,2937,4008,4095,2048,},
			{1857,973,217,167,164,78,79,74,72,51,64,34,19,35,23,40,22,33,36,13,24,14,15,22,23,22,33,19,19,20,20,43,16,18,5,17,9,9,13,20,20,6,12,14,15,10,14,0,0,0,0,7,0,1936,466,259,266,243,282,142,161,196,93,211,103,81,143,61,92,146,545,87,1240,1846,2578,984,2556,40,27,1543,810,503,584,430,398,451,431,342,372,2337,731,726,643,1747,1264,1050,1102,999,932,736,1896,1399,1823,1733,1657,2082,2214,2133,2367,2744,2989,2493,1734,1602,1257,1158,1082,2579,1420,2305,1886,2700,1398,1266,3112,3146,2189,3268,2025,1537,2998,2650,2254,3212,2568,3283,3419,3521,3455,3317,3531,3538,1863,1524,3069,3606,3657,3473,1612,1431,2731,3589,3663,3672,3745,1839,3998,1977,3952,2104,2984,2163,2378,2655,3006,2231,2589,2609,3838,3982,1622,2114,3226,3414,2098,2708,1699,3499,3578,2265,3513,3530,3574,3777,3722,1506,3726,1863,1004,3788,3889,2675,1455,3658,3684,3796,3894,3925,3921,3962,4014,4039,4004,1350,3989,4051,3995,4061,4090,1814,4093,1804,4093,1796,1896,4089,1846,2872,2695,2539,3663,2624,2818,2843,655,1994,2834,2804,2512,1868,1897,1868,2729,2467,1893,1824,1908,1908,2845,3474,3486,3509,2946,3272,2921,2581,4067,4067,4063,4079,4077,4075,3493,4081,4095,2048,},
			{1928,1055,592,381,231,275,181,187,173,84,93,111,80,40,39,84,78,64,62,63,40,70,32,43,46,48,47,52,36,34,34,84,25,47,29,19,24,18,24,39,24,30,30,17,55,22,37,0,8,0,8,0,0,1705,354,205,289,245,199,161,131,137,120,161,150,94,112,105,72,109,713,14,1057,2128,2597,1086,2813,13,42,1580,745,591,576,476,469,312,353,337,273,2435,711,592,611,1967,1135,903,844,773,901,728,2080,1275,1745,1424,1779,2062,2495,2482,2608,2723,2862,2358,1455,1514,1034,924,883,2791,1310,2219,2045,2914,1314,1125,3152,3105,2162,3460,2070,1481,3238,2380,1521,3288,2417,3437,3513,3641,3661,3447,3616,3605,1667,1272,2988,3687,3732,3501,1518,1445,2688,3578,3710,3754,3768,1682,3993,2011,3971,2135,3125,2647,2400,2908,3211,2364,2629,2868,3613,3796,1412,3199,2994,3720,1754,2437,1452,3660,3631,2151,3707,3645,3750,3883,3732,1600,3847,1657,1028,3822,3894,2361,1321,3836,3783,3911,3876,3922,3972,3876,3911,3953,3952,1524,3890,3969,4017,4019,4091,2015,4094,1902,4073,1951,2250,4088,2141,2824,2674,2455,3349,2378,2556,2648,884,2596,2657,2597,2257,1715,1289,1369,2479,2341,2127,1726,1551,1737,2608,3368,3455,3255,2681,3104,2864,2564,4055,4057,4069,4063,4082,4070,3234,4062,4094,2048,},
			{1819,1089,439,350,201,174,135,170,86,105,95,113,81,76,52,87,44,50,72,74,30,35,49,38,35,36,47,69,9,52,41,43,43,45,43,24,27,15,24,9,27,21,11,14,19,11,27,0,0,0,8,1,1,1888,488,287,315,278,211,218,181,207,131,242,118,139,124,126,117,148,1082,115,1565,2380,2600,1110,2813,17,25,1670,736,581,578,444,435,429,367,352,282,2492,693,652,723,2026,1049,918,976,799,873,730,1923,1309,1731,1505,1823,1941,2321,2644,2678,2695,2941,2286,1278,1422,1152,1075,835,2730,1357,2679,1833,2886,1257,1188,3044,3193,2161,3468,1789,1463,3177,2291,1510,3261,2357,3373,3464,3638,3658,3415,3552,3529,1546,1188,2974,3555,3625,3528,1351,1395,2705,3512,3653,3681,3664,1428,3948,1856,3969,1835,3103,2445,2405,2840,3268,2555,2827,2908,3684,3778,1415,3138,3156,3499,1791,2488,1529,3612,3552,2027,3606,3646,3663,3850,3772,1623,3818,1655,1142,3845,3894,2040,1560,3759,3825,3878,3908,3961,3964,3908,3896,3962,3988,1403,3976,4019,4015,4013,4065,1911,4077,1896,4086,1986,2082,4074,2098,2771,2465,2430,3109,2338,1966,2641,1065,2461,2369,2555,2228,1795,952,1041,2300,2285,2348,2143,1962,2110,2375,3121,2929,2835,2503,2659,2444,2491,4062,4048,4050,4084,4063,4078,3311,4087,4095,2048,},
			{1948,1202,284,256,184,174,132,75,73,90,65,72,52,55,63,89,38,21,76,21,44,32,37,41,6,22,21,51,22,21,15,7,12,25,31,48,15,23,8,26,9,15,5,18,9,34,20,8,0,0,0,0,0,1715,393,271,335,330,342,261,179,183,160,203,118,156,129,117,91,130,874,100,1616,2406,2460,1074,2813,31,43,1474,718,552,532,422,527,393,363,295,319,2535,710,587,668,1854,1133,927,1005,753,865,758,1905,1216,1563,1561,1743,1906,2353,2623,2737,2632,2838,2247,1349,1570,1169,1100,757,2828,1419,2407,1929,2975,1187,1245,3109,3115,2147,3428,1829,1288,3116,2360,1408,3313,2425,3483,3513,3614,3671,3409,3441,3438,1607,1312,2966,3525,3568,3480,1426,1293,2730,3536,3704,3726,3727,1407,3882,1663,3958,1619,3117,2467,2512,2749,3109,2350,2753,2869,3858,3831,1534,3170,3254,3634,1940,2749,1538,3668,3705,2097,3616,3668,3707,3827,3698,1592,3762,1773,1108,3858,3828,2178,1566,3790,3748,3868,3942,3950,3968,3928,3912,4001,3972,1497,3986,4008,3982,4027,4069,1891,4082,1815,4063,1998,2097,4090,2055,2680,2411,2533,3155,2209,1840,2669,1118,2618,2266,2532,2327,1671,972,937,2204,2292,2277,1647,1606,1915,2324,2915,2908,2820,2443,2551,2357,2404,4087,4073,4074,4062,4078,4070,3348,4072,4095,2048,},
			{1866,1128,214,197,89,82,95,53,68,74,50,69,24,30,41,55,15,32,73,30,31,32,28,38,31,32,20,13,10,17,6,3,19,14,12,25,9,8,8,3,1,8,9,7,11,37,9,0,1,8,1,8,0,1488,439,297,309,299,278,205,191,197,162,250,126,122,160,117,125,122,680,60,1411,2169,2543,958,2873,63,78,1489,613,548,580,457,467,346,348,290,326,2625,639,740,649,1859,997,859,909,662,826,720,1853,1226,1625,1487,1671,1998,2374,2485,2632,2680,2943,2067,1383,1494,1090,1232,926,2641,1378,2214,2042,2836,1242,1218,3049,3069,2240,3483,1880,1239,3097,2355,1657,3334,2365,3460,3444,3676,3574,3423,3526,3489,1613,1446,2961,3606,3570,3555,1258,1041,2737,3610,3701,3759,3815,1498,3917,1425,3898,1405,3202,2528,2489,2565,3054,2362,2947,2758,3879,3891,1540,2945,3258,3531,2190,2745,1659,3571,3639,2321,3670,3628,3660,3786,3726,1605,3793,2112,1033,3827,3821,2677,1462,3785,3742,3823,3887,3926,3894,3900,4005,3997,4034,1612,4038,4042,4033,4056,4069,1901,4071,1774,4061,1823,2033,4065,2061,2744,2308,2606,3314,2279,2115,2756,980,2620,2315,2566,2265,1672,1166,1217,2286,2410,2248,1346,1413,1638,2400,3061,3171,3038,2609,2675,2467,2368,4084,4085,4092,4079,4082,4088,3658,4083,4095,2048,},
			{1890,1430,689,498,380,292,171,165,155,137,96,101,70,81,92,72,64,85,76,57,100,65,33,44,49,40,69,39,63,29,46,55,41,63,33,38,35,24,33,32,30,28,33,51,66,28,52,2,15,0,0,1,0,797,442,182,242,194,201,183,153,135,124,171,93,85,122,67,71,93,222,32,631,896,980,647,895,164,93,1375,693,566,497,420,414,411,357,356,319,1740,649,683,681,1717,1170,1025,892,834,835,685,1727,1259,1612,1588,1778,1999,2524,2197,2613,2781,2957,2181,1664,1735,1496,1061,913,2521,1524,1935,2155,2733,1500,1282,2906,3093,2337,3337,2392,1647,3113,2435,1885,3332,2614,3384,3394,3437,3606,3432,3669,3473,2090,1598,3186,3578,3713,3533,1936,1525,2864,3689,3624,3782,3769,2293,3974,2654,3953,2693,3088,2302,1967,2558,2865,1563,2458,1805,3255,3700,1576,2840,2649,3017,1472,2467,2018,3157,3024,2338,3011,3377,3361,3394,3515,1715,3653,2480,1370,3695,3593,2812,2561,3709,3827,3780,3787,3799,3850,3817,3773,3863,3943,1946,3922,3946,3954,3952,4089,2316,4095,2515,4087,3067,2107,4095,2986,2806,3420,2255,3818,3269,3614,2293,2541,3370,3583,3572,2147,2845,2940,2999,3591,3507,1981,3072,2950,2851,3629,3890,3891,3867,2290,3846,3637,2856,4009,3996,3989,4032,4007,4023,2937,4008,4095,2048,},
			{1890,1430,689,498,380,292,171,165,155,137,96,101,70,81,92,72,64,85,76,57,100,65,33,44,49,40,69,39,63,29,46,55,41,63,33,38,35,24,33,32,30,28,33,51,66,28,52,2,15,0,0,1,0,797,442,182,242,194,201,183,153,135,124,171,93,85,122,67,71,93,222,32,631,896,980,647,895,164,93,1375,693,566,497,420,414,411,357,356,319,1740,649,683,681,1717,1170,1025,892,834,835,685,1727,1259,1612,1588,1778,1999,2524,2197,2613,2781,2957,2181,1664,1735,1496,1061,913,2521,1524,1935,2155,2733,1500,1282,2906,3093,2337,3337,2392,1647,3113,2435,1885,3332,2614,3384,3394,3437,3606,3432,3669,3473,2090,1598,3186,3578,3713,3533,1936,1525,2864,3689,3624,3782,3769,2293,3974,2654,3953,2693,3088,2302,1967,2558,2865,1563,2458,1805,3255,3700,1576,2840,2649,3017,1472,2467,2018,3157,3024,2338,3011,3377,3361,3394,3515,1715,3653,2480,1370,3695,3593,2812,2561,3709,3827,3780,3787,3799,3850,3817,3773,3863,3943,1946,3922,3946,3954,3952,4089,2316,4095,2515,4087,3067,2107,4095,2986,2806,3420,2255,3818,3269,3614,2293,2541,3370,3583,3572,2147,2845,2940,2999,3591,3507,1981,3072,2950,2851,3629,3890,3891,3867,2290,3846,3637,2856,4009,3996,3989,4032,4007,4023,2937,4008,4095,2048,},
			{1890,1430,689,498,380,292,171,165,155,137,96,101,70,81,92,72,64,85,76,57,100,65,33,44,49,40,69,39,63,29,46,55,41,63,33,38,35,24,33,32,30,28,33,51,66,28,52,2,15,0,0,1,0,797,442,182,242,194,201,183,153,135,124,171,93,85,122,67,71,93,222,32,631,896,980,647,895,164,93,1375,693,566,497,420,414,411,357,356,319,1740,649,683,681,1717,1170,1025,892,834,835,685,1727,1259,1612,1588,1778,1999,2524,2197,2613,2781,2957,2181,1664,1735,1496,1061,913,2521,1524,1935,2155,2733,1500,1282,2906,3093,2337,3337,2392,1647,3113,2435,1885,3332,2614,3384,3394,3437,3606,3432,3669,3473,2090,1598,3186,3578,3713,3533,1936,1525,2864,3689,3624,3782,3769,2293,3974,2654,3953,2693,3088,2302,1967,2558,2865,1563,2458,1805,3255,3700,1576,2840,2649,3017,1472,2467,2018,3157,3024,2338,3011,3377,3361,3394,3515,1715,3653,2480,1370,3695,3593,2812,2561,3709,3827,3780,3787,3799,3850,3817,3773,3863,3943,1946,3922,3946,3954,3952,4089,2316,4095,2515,4087,3067,2107,4095,2986,2806,3420,2255,3818,3269,3614,2293,2541,3370,3583,3572,2147,2845,2940,2999,3591,3507,1981,3072,2950,2851,3629,3890,3891,3867,2290,3846,3637,2856,4009,3996,3989,4032,4007,4023,2937,4008,4095,2048,},
			{1890,1430,689,498,380,292,171,165,155,137,96,101,70,81,92,72,64,85,76,57,100,65,33,44,49,40,69,39,63,29,46,55,41,63,33,38,35,24,33,32,30,28,33,51,66,28,52,2,15,0,0,1,0,797,442,182,242,194,201,183,153,135,124,171,93,85,122,67,71,93,222,32,631,896,980,647,895,164,93,1375,693,566,497,420,414,411,357,356,319,1740,649,683,681,1717,1170,1025,892,834,835,685,1727,1259,1612,1588,1778,1999,2524,2197,2613,2781,2957,2181,1664,1735,1496,1061,913,2521,1524,1935,2155,2733,1500,1282,2906,3093,2337,3337,2392,1647,3113,2435,1885,3332,2614,3384,3394,3437,3606,3432,3669,3473,2090,1598,3186,3578,3713,3533,1936,1525,2864,3689,3624,3782,3769,2293,3974,2654,3953,2693,3088,2302,1967,2558,2865,1563,2458,1805,3255,3700,1576,2840,2649,3017,1472,2467,2018,3157,3024,2338,3011,3377,3361,3394,3515,1715,3653,2480,1370,3695,3593,2812,2561,3709,3827,3780,3787,3799,3850,3817,3773,3863,3943,1946,3922,3946,3954,3952,4089,2316,4095,2515,4087,3067,2107,4095,2986,2806,3420,2255,3818,3269,3614,2293,2541,3370,3583,3572,2147,2845,2940,2999,3591,3507,1981,3072,2950,2851,3629,3890,3891,3867,2290,3846,3637,2856,4009,3996,3989,4032,4007,4023,2937,4008,4095,2048,},
			{1890,1430,689,498,380,292,171,165,155,137,96,101,70,81,92,72,64,85,76,57,100,65,33,44,49,40,69,39,63,29,46,55,41,63,33,38,35,24,33,32,30,28,33,51,66,28,52,2,15,0,0,1,0,797,442,182,242,194,201,183,153,135,124,171,93,85,122,67,71,93,222,32,631,896,980,647,895,164,93,1375,693,566,497,420,414,411,357,356,319,1740,649,683,681,1717,1170,1025,892,834,835,685,1727,1259,1612,1588,1778,1999,2524,2197,2613,2781,2957,2181,1664,1735,1496,1061,913,2521,1524,1935,2155,2733,1500,1282,2906,3093,2337,3337,2392,1647,3113,2435,1885,3332,2614,3384,3394,3437,3606,3432,3669,3473,2090,1598,3186,3578,3713,3533,1936,1525,2864,3689,3624,3782,3769,2293,3974,2654,3953,2693,3088,2302,1967,2558,2865,1563,2458,1805,3255,3700,1576,2840,2649,3017,1472,2467,2018,3157,3024,2338,3011,3377,3361,3394,3515,1715,3653,2480,1370,3695,3593,2812,2561,3709,3827,3780,3787,3799,3850,3817,3773,3863,3943,1946,3922,3946,3954,3952,4089,2316,4095,2515,4087,3067,2107,4095,2986,2806,3420,2255,3818,3269,3614,2293,2541,3370,3583,3572,2147,2845,2940,2999,3591,3507,1981,3072,2950,2851,3629,3890,3891,3867,2290,3846,3637,2856,4009,3996,3989,4032,4007,4023,2937,4008,4095,2048,},
		};

		for (uint32_t i = 0;i < static_cast<uint32_t>(kProfileCount); ++i) {
			for (uint32_t j = 0; j < kProbCtx;++j) {
				for (uint32_t k = 0; k < num_states; ++k) {
					auto& pr = preds[i][j][k];
					pr.init();
					if (fixed_probs) {
						pr.setP(table.st(initial_probs[std::min(j, 9U)][k]));
					} else {
						pr.setP(initial_probs[std::min(j, 9U)][k]);
					}
				}
			}
		}

		setDataProfile(kBinary);
		owhash = 0;

		// Statistics
		if (statistics) {
			for (auto& c : mixer_skip) c = 0;
			other_count_ = match_count_ = non_match_count_ = 0;
		}
	}

	forceinline uint32_t hashFunc(uint32_t a, uint32_t b) {
		b += a;
		b += rotate_left(b, 9);
		return b ^ (b >> 6);
	}

	forceinline uint32_t getOrder0() const {
		return 0;
	}

	forceinline uint32_t getOrder1(uint32_t p0) const {
		uint32_t ret = o0size + (p0 << 8);
		// if (use_prefetch) prefetch(&hash_table[ret]);
		return ret;
	}

	forceinline CMMixer* getProfileMixers(DataProfile profile) {
		return &mixers[static_cast<uint32_t>(profile) * (mixer_mask + 1)];
	}

	void calcMixerBase() {
		const uint32_t p0 = static_cast<uint8_t>(buffer[buffer.getPos() - 1]);
		const uint32_t p1 = static_cast<uint8_t>(buffer[buffer.getPos() - 2]);
		uint32_t mixer_ctx; // >> 5;
		const uint32_t mm_len = match_model.getLength();
		if (use_word) {
			mixer_ctx = text_ctx_map[p0];
			mixer_ctx <<= 3;
			auto wlen = word_model.getLength();
			mixer_ctx |=
				(wlen >= 1) +
				(wlen >= 2) +
				(wlen >= 3) +
				(wlen >= 4) +
				(wlen >= 5) +
				(wlen >= 6) +
				(wlen >= 8) +
				0;
		} else {
			mixer_ctx = sparse_ctx_map[p0];
		}
		mixer_ctx <<= 2;
		if (mm_len) {
			if (use_word) {
				mixer_ctx |= 1 +
					(mm_len >= match_model.kMinMatch + 2) + 
					(mm_len >= match_model.kMinMatch + 7) + 
					0;
			} else {
				mixer_ctx |= 1 +
					(mm_len >= match_model.kMinMatch + 1) + 
					(mm_len >= match_model.kMinMatch + 4) + 
					0;
			}
		}		
		mixer_base = getProfileMixers(profile) + (mixer_ctx << 8);
		cur_preds = &preds[static_cast<uint32_t>(profile)];
	}

	forceinline byte nextState(byte state, uint32_t bit, uint32_t ctx) {
		if (!fixed_probs) {
			(*cur_preds)[ctx][state].update(bit, 9);
		}
		return state_trans[state][bit];
	}
	
	forceinline int getP(byte state, uint32_t ctx, const short* st) const {
		int p = (*cur_preds)[ctx][state].getP();
		if (!fixed_probs) {
			p = st[p];
		}
		return p;
	}

	template <const bool decode, typename TStream>
	forceinline size_t processBit(TStream& stream, size_t bit, size_t* base_contexts, size_t ctx, bool use_match_p_override = false, int match_p_override = 0, size_t mixer_ctx = 0) {
		if (!use_match_p_override) {
			mixer_ctx = ctx;
		}
		auto* const cur_mixer = &mixer_base[mixer_ctx];
		const auto mm_l = match_model.getLength();
	
		byte* const ht = hash_table;
		const short* st = table.getStretchPtr();

		uint8_t 
			*no_alias sp0 = nullptr, *no_alias sp1 = nullptr, *no_alias sp2 = nullptr, *no_alias sp3 = nullptr, *no_alias sp4 = nullptr,
			*no_alias sp5 = nullptr, *no_alias sp6 = nullptr, *no_alias sp7 = nullptr, *no_alias sp8 = nullptr, *no_alias sp9 = nullptr;
		size_t s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0, s5 = 0, s6 = 0, s7 = 0, s8 = 0, s9 = 0;

		uint32_t p;
		int p0 = 0, p1 = 0, p2 = 0, p3 = 0, p4 = 0, p5 = 0, p6 = 0, p7 = 0, p8 = 0, p9 = 0;
		if (inputs > 1) { sp1 = &ht[base_contexts[1] ^ ctx]; s1 = *sp1; p1 = getP(s1, 1, st); }
		if (inputs > 2) { sp2 = &ht[base_contexts[2] ^ ctx]; s2 = *sp2; p2 = getP(s2, 2, st); }
		if (inputs > 3) { sp3 = &ht[base_contexts[3] ^ ctx]; s3 = *sp3; p3 = getP(s3, 3, st); }
		if (inputs > 4) { sp4 = &ht[base_contexts[4] ^ ctx]; s4 = *sp4; p4 = getP(s4, 4, st); }
		if (inputs > 5) { sp5 = &ht[base_contexts[5] ^ ctx]; s5 = *sp5; p5 = getP(s5, 5, st); }
		if (inputs > 6) { sp6 = &ht[base_contexts[6] ^ ctx]; s6 = *sp6; p6 = getP(s6, 6, st); }
		if (inputs > 7) { sp7 = &ht[base_contexts[7] ^ ctx]; s7 = *sp7; p7 = getP(s7, 7, st); }
		if (inputs > 8) { sp8 = &ht[base_contexts[8] ^ ctx]; s8 = *sp8; p8 = getP(s8, 8, st); }
		if (inputs > 9) { sp9 = &ht[base_contexts[9] ^ ctx]; s9 = *sp9; p9 = getP(s9, 9, st); }
		if (inputs > 0) {
			if (mm_l == 0) {
				sp0 = &ht[base_contexts[0] ^ ctx]; s0 = *sp0;
				assert(sp0 >= ht && sp0 <= ht + hash_alloc_size);
				p0 = getP(s0, 0, st);
			} else {
				p0 = use_match_p_override ? match_p_override : match_model.getP(st);
			}
		}

#if 0
		uint8_t* const sparse1 = &spar1[(owhash & 0xFF00) + ctx];
		uint8_t* const sparse2 = &spar2[((owhash & 0xFF0000) >> 8) + ctx];
		uint8_t* const sparse3 = &spar3[((owhash & 0xFF000000) >> 16) + ctx];
		if (false && use_sparse) {
			int sparsep =
				16 * table.st((*cur_preds)[1][*sparse1].getP()) +
				13 * table.st((*cur_preds)[1][*sparse2].getP()) +
				18 * table.st((*cur_preds)[1][*sparse3].getP());
			p6 = sparsep / 16;
		}
#endif

#if USE_MMX
		__m128i wa = _mm_cvtsi32_si128(static_cast<unsigned short>(((inputs > 0) ? (mm_p ? mm_p : getP(*s0, 0, st)) : 0)));
		__m128i wb = _mm_cvtsi32_si128((inputs > 1) ? (int(getP(*s1, 1, st)) << 16) : 0);
		if (inputs > 2) wa = _mm_insert_epi16(wa, getP(*s2, 2, st), 2);
		if (inputs > 3) wb = _mm_insert_epi16(wb, getP(*s3, 3, st), 3);
		if (inputs > 4) wa = _mm_insert_epi16(wa, getP(*s4, 4, st), 4);
		if (inputs > 5) wb = _mm_insert_epi16(wb, getP(*s5, 5, st), 5);
		if (inputs > 6) wa = _mm_insert_epi16(wa, getP(*s6, 6, st), 6);
		if (inputs > 7) wb = _mm_insert_epi16(wb, getP(*s7, 7, st), 7);
		__m128i wp = _mm_or_si128(wa, wb);
		int stp = cur_mixer->p(wp);
		int mixer_p = table.sq(stp); // Mix probabilities.
#else
		int stp = cur_mixer->p(11, p0, p1, p2, p3, p4, p5, p6, p7, p8, p9);
		int mixer_p = table.sq(stp); // Mix probabilities.
#endif
		p = mixer_p;
		if (kUseSSE) {
			if (use_lzp) {
				if (use_match_p_override) {
					p = p * 3 + 13 * lzp_sse.p(st[p] + 2048, apm_ctx + mm_l);
					p /= 16;
				}
				if (apm_ctx) {
					p = sse.p(st[p] + 2048, apm_ctx + mixer_ctx);
				} else {
					p = p * 3 + 13 * sse.p(st[p] + 2048, mixer_ctx);
					p /= 16;
				}
			} else {
				p = (2 * p + 2 * sse.p(st[p] + 2048, (mixer_sse_ctx & 0xFF) * 256 + mixer_ctx)) / 4;
			}
			p += p < 2048;
		}

		if (decode) { 
			bit = ent.getDecodedBit(p, shift);
		}
		dcheck(bit < 2);

#if USE_MMX
		const bool ret = cur_mixer->update(wp, mixer_p, bit);
#else
		const bool ret = cur_mixer->update(mixer_p, bit, 12, 24, opt_var, p0, p1, p2, p3, p4, p5, p6, p7, p8, p9);
#endif

		if (kUseSSE) {
			if (use_match_p_override) {
				lzp_sse.update(bit);
			}
			sse.update(bit);
			mixer_sse_ctx = mixer_sse_ctx * 2 + ret;
		}

		if (statistics) {
			// Returns false if we skipped the update due to a low error,
			// should happen frequently on highly compressible files.
			++mixer_skip[ret];
		}
			
		// Only update the states / predictions if the mixer was far enough from the bounds, helps 15k on enwik8 and 1-2sec.
		if (ret) {
			if (inputs > 0 && mm_l == 0) *sp0 = nextState(s0, bit, 0);
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
		if (!use_match_p_override) {
			match_model.updateBit(bit);
		}
		if (decode) {
			ent.Normalize(stream);
		} else {
			ent.encode(stream, bit, p, shift);
		}
		return bit;
	}

	template <const bool decode, typename TStream>
	size_t processNibble(TStream& stream, size_t c, size_t* base_contexts, size_t ctx_add) {
		// Maximize performance!
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
			bit = processBit<decode>(stream, bit, base_contexts, ctx);
			// *sparse1 = state_trans[*sparse1][bit];
			// *sparse2 = state_trans[*sparse2][bit];
			// *sparse3 = state_trans[*sparse3][bit];

			// Encode the bit / decode at the last second.
			if (use_huffman) {
				huff_state = huff.getTransition(huff_state, bit);
				if (huff.isLeaf(huff_state)) {
					return huff_state;
				}
			} else {
				base_ctx = base_ctx * 2 + bit;
				if (base_ctx >= 16) break;
			}
		}
		return base_ctx ^ 16;
	}

	template <const bool decode, typename TStream>
	uint32_t processByte(TStream& stream, uint32_t c = 0) {
		size_t base_contexts[inputs]; // Base contexts

		const size_t bpos = buffer.getPos();
		const size_t blast = bpos - 1; // Last seen char
		const size_t
			p0 = static_cast<byte>(owhash >> 0),
			p1 = static_cast<byte>(owhash >> 8),
			p2 = static_cast<byte>(owhash >> 16),
			p3 = static_cast<byte>(owhash >> 24),
			p4 = static_cast<byte>(buffer[blast - 4]);

		if (use_sparse) {
			mmask <<= 4;
			mmask |= sparse_ctx_map[p0];
			//mmask |= (!p0 ? 0 : isalpha(p0) ? 1 : ispunct(p0) ? 2 : isspace(p0) ? 3 : (p0 == 255) ? 4 : (p0 < 16) ? 5 : (p0 < 64) ? 6 : 7);
		} else {
			mmask <<= 3;
			mmask |= text_ctx_map[p0];
		}
		// 

		size_t start = 0;
		if (inputs > 8) {
			base_contexts[start++] = getOrder0();
		}
		base_contexts[start++] = getOrder1(p0); // Order 1
		const bool cur_use_word = use_word;
		if (use_word) {
			if (start < inputs) {
				base_contexts[start++] = hash_lookup(word_model.getHash(), false); // Word model
				if (inputs > 7) {
					base_contexts[start++] = hash_lookup(word_model.getHash() ^ word_model.getPrevHash(), false); // Word model
				}
			}
		}
		// if ((inputs > 6 || use_sparse) && start < inputs) base_contexts[start++] = hash_lookup(mmask * 98765431);
		if (use_sparse) {
			if (start < inputs) base_contexts[start++] = hash_lookup(hashFunc(p1, 0x4B1BEC1D)); // Order 12
			// if (start < inputs) base_contexts[start++] = hash_lookup(hashFunc(p2, 0x651A833E)); // Order 23
			// if (start < inputs) base_contexts[start++] = hash_lookup(hashFunc(p3, 0x37220B98)); // Order 34
			if (start < inputs) base_contexts[start++] = hash_lookup(hashFunc(p2, hashFunc(p1, 0x37220B98))); // Order 12
			if (start < inputs) base_contexts[start++] = hash_lookup(hashFunc(p3, hashFunc(p2, 0x651A833E))); // Order 23
			// if (opt_var & 32 && start < inputs) base_contexts[start++] = hash_lookup(hashFunc(p4, hashFunc(p3, 0x4B1BEC1D))); // Order 34
		}
		uint32_t h = hashFunc(p0, 0x32017044);
		uint32_t order = 2;  // Start at order 2.

		for (;;) {
			h = hashFunc(buffer[bpos - order], h);
			if (start >= inputs) {
				break;
			}
			if (order < 5 || order == 6 || order >= 8) {
				base_contexts[start++] = hash_lookup(h);
			}
			order++;
		}
		match_model.setHash(h);

		const size_t mm_len = match_model.getLength();
		size_t expected_char = 0;
		size_t extra_add = 0;
		apm_ctx = 0;
		if (mm_len > 0) {
			// mixer_base = getProfileMixers(profile) + (mm_len * 256);
			// mixer_base = getProfileMixers(profile) + (mm_len - match_model.getMinMatch()) * 256;
			expected_char = match_model.getExpectedChar(buffer);
			if (statistics && !decode) {
				++(expected_char == c ? match_count_ : non_match_count_);
			}
			if (use_lzp) {
				mixer_base = &lzp_mixers[mm_len];
				apm_ctx = 256 * (1 + expected_char);
				size_t bit = decode ? 0 : expected_char == c;

				auto& m = lzp_match[expected_char * 256 + mm_len];
				if (true) {
					int match_p2 = table.st(m.getP());
					bit = processBit<decode>(stream, bit, base_contexts, 256 + expected_char, true, match_p2, 0);
				} else {
					if (decode) {
						bit = ent.decode(stream, m.getP(), shift);
					} else {
						ent.encode(stream, bit, m.getP(), shift);
					}
				}
				m.update(bit);

				if (bit) {
					return match_model.getExpectedChar(buffer);
				}
			}
		} else if (statistics) {
			++other_count_;
		}
		calcMixerBase();

		// Non match, do normal encoding.
		size_t huff_state = 0;
		if (use_huffman) {
			huff_state = processNibble<decode>(stream, c, base_contexts, 0);
			if (decode) {
				c = huff.getChar(huff_state);
			}
		} else {
			size_t n1 = processNibble<decode>(stream, c >> 4, base_contexts, 0);
			size_t n2 = processNibble<decode>(stream, c & 0xF, base_contexts, 15 + (n1 * 15));
			if (decode) {
				c = n2 | (n1 << 4);
			}
		}

		return c;
	}

	void setDataProfile(DataProfile new_profile) {
		profile = new_profile;
		word_model.reset();
		switch (profile) {
		case kWave:
			use_word = false;
			use_match = false;
			use_sparse = true;
			break;
		case kText:
			use_word = true;
			// use_word = false;
			use_match = true;
			use_sparse = false;
			break;
		default: // Binary data types
			use_word = false;
			use_match = true;
			use_sparse = true;
			break;
		}
	};

	void update(uint32_t c) {
		if (use_word) {
			word_model.updateUTF(c);
			if (word_model.getLength()) {
				if (use_prefetch) hash_lookup(word_model.getHash());
			}
		}
		buffer.push(c);
		if (use_match) {
			match_model.update(buffer);
			if (match_model.getLength()) {
				uint32_t expected_char = match_model.getExpectedChar(buffer);
				uint32_t expected_bits = use_huffman ? huff.getCode(expected_char).length : 8;
				if (use_huffman) expected_char = huff.getCode(expected_char).value;
				match_model.updateExpectedCode(expected_char, expected_bits);
			}
		} else {
			match_model.resetMatch();
		}
		owhash = (owhash << 8) | static_cast<byte>(c);
	}

	template <typename TStream>
	void readSubBlock(TStream& sin, SubBlockHeader& block) {
		block.flags = block_flag_model.decode(ent, sin);
		block.profile = (DataProfile)block_profile_models[(uint32_t)profile].decode(ent, sin);
	}

	template <typename TStream>
	void writeSubBlock(TStream& sout, const SubBlockHeader& block) {
		block_flag_model.encode(ent, sout, block.flags);
		block_profile_models[(uint32_t)profile].encode(ent, sout, (uint32_t)block.profile);
	}

	void compress(Stream* in_stream, Stream* out_stream);
	void decompress(Stream* in_stream, Stream* out_stream);	
};

#endif
