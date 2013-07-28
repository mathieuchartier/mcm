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
#include "Compress.hpp"
#include "Entropy.hpp"
#include "Huffman.hpp"
#include "Log.hpp"
#include "MatchModel.hpp"
#include "Memory.hpp"
#include "Mixer.hpp"
#include "Model.hpp"
#include "Range.hpp"
#include "StateMap.hpp"
#include "WordModel.hpp"

template <const size_t inputs = 6>
class CM {
public:
	static const size_t nibble_spread = 16; //65537;
	static const short version = 5;

	static const bool statistics = false;
	static const bool use_prefetch = true;
	static const bool fixed_probs = false;

	// Archive header.
	class ArchiveHeader {
	public:
		char magic[3]; // MCM
		unsigned short version;
		byte mem_usage;

		ArchiveHeader() {
			magic[0] = 'M';
			magic[1] = 'C';
			magic[2] = 'M';
			version = CM::version;
			mem_usage = 8;
		}

		template <typename TIn>
		void read(TIn& sin) {
			for (auto& c : magic) c = sin.read();
			version = sin.read();
			version = (version << 8) | sin.read();
			mem_usage = (byte)sin.read();
		}

		template <typename TOut>
		void write(TOut& sout) {
			for (auto& c : magic) sout.write(c);
			sout.write(version >> 8);
			sout.write(version & 0xFF);
			sout.write(mem_usage);
		}
	} archive_header;

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

	// Statistics
	size_t mixer_skip[2];

	// SS table
	static const size_t shift = 12;
	static const size_t max_value = 1 << shift;
	typedef ss_table<short, max_value, -2 * int(KB), 2 * int(KB), 8> SSTable;
	SSTable table;
	
	//typedef slowBitModel<unsigned short, shift> StationaryModel;
	typedef safeBitModel<unsigned short, shift, 5, 15> BitModel;
	typedef fastBitModel<int, shift, 9, 30> StationaryModel;
	typedef fastBitModel<int, shift, 5, 30> HPStationaryModel;

	WordModel word_model;

	Range7 ent;
	
	MatchModel<HPStationaryModel> match_model;

	// Hash table
	size_t hash_mask;
	size_t hash_alloc_size;
	MemMap hash_storage;
	byte *hash_table;

	// Automatically zerored out.
	static const size_t o0size = 0x100;
	static const size_t o1size = 0x10000;
	byte *order0, *order1;
	
	// Mixers
	size_t mixer_mask;
//#define USE_MMX
#ifdef USE_MMX
	typedef MMXMixer<inputs, 15, 1> CMMixer;
#else
	typedef Mixer<int, inputs+1, 17, 1> CMMixer;
#endif
	std::vector<CMMixer> mixers;
	CMMixer *mixer_base;

	// Contexts
	hash_t base_hashes[inputs];
	size_t owhash; // Order of sizeof(size_t) hash.

	// Rotating buffer.
	SlidingWindow2<byte> buffer;

	// Options
	size_t opt_var;
	bool use_word;
	bool use_match;
	bool use_sparse;
	
	// Fast table.
	static const size_t num_states = 256;
	short state_p[num_states];
	byte state_trans[num_states][2];

	// Huffman preprocessing.
	static const bool use_huffman = true;
	static const size_t huffman_len_limit = 16;
	Huffman huff;
	
	// End of block signal.
	BitModel end_of_block_mdl;
	DataProfile profile;
	
	typedef bitContextModel<BitModel, 255> BlockFlagModel;
	BlockFlagModel block_flag_model;

	typedef bitContextModel<BitModel, (size_t)kProfileCount+1> BlockProfileModel;
	BlockProfileModel block_profile_models[(size_t)kProfileCount];

	// Division table.
	DivTable<short, shift, 1024> div_table;
	
	// SM
//#define USE_ST_PRED
#ifdef USE_ST_PRED
	// typedef fastBitSTModel<int, 16, 5> PredModel;
	typedef fastBitSTAModel<int, 12, 10, 30> PredModel;
#else
	typedef StationaryModel PredModel;
#endif
	PredModel preds[(size_t)kProfileCount][inputs][256];
	
	static const size_t eof_char = 126;
	
	forceinline size_t hash_lookup(hash_t hash, bool prefetch_addr = use_prefetch) {
		hash &= hash_mask;
		size_t ret = hash + (o0size + o1size);
		if (prefetch_addr) {	
			prefetch(hash_table + (ret & (size_t)~(kCacheLineSize - 1)));
		}
		return ret;
	}

	void setMemUsage(size_t usage) {
		archive_header.mem_usage = usage;
	}

	CM() {
		opt_var = 0;
	}

	void init() {
		table.build();

		mixer_mask = 0xFFFF;
		mixers.resize(static_cast<size_t>(kProfileCount) * (mixer_mask + 1));
		for (auto& mixer : mixers) {
			mixer.init();
		}
	
		NSStateMap<12> sm;
		sm.build();
		
		hash_mask = ((2 * MB) << archive_header.mem_usage) / sizeof(hash_table[0]) - 1;
		hash_alloc_size = hash_mask + o0size + o1size + kPageSize + (1 << huffman_len_limit);
		hash_storage.resize(hash_alloc_size); // Add extra space for ctx.
		order0 = reinterpret_cast<byte*>(hash_storage.getData());
		order1 = order0 + o0size;
		hash_table = order0; // Here is where the real hash table starts

		word_model.init();

		buffer.resize((MB / 4) << archive_header.mem_usage);
		buffer.fill(0);

		// Models.
		end_of_block_mdl.init();
		block_flag_model.init();
		for (auto& m : block_profile_models) m.init();

		// Match model.
		match_model.resize(buffer.getSize() / 2);
		match_model.init();
		
		// Optimization
		for (size_t i = 0; i < num_states; ++i) {
			// Pre stretch state map.
			state_p[i] = table.st(sm.p(i));
			for (size_t j = 0; j < 2; ++j) {
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
		};

		for (size_t i = 0;i < (size_t)kProfileCount; ++i) {
			for (size_t j = 0; j < inputs;++j) {
				for (size_t k = 0; k < num_states; ++k) {
					auto& pr = preds[i][j][k];
					pr.init();
#ifdef USE_ST_PRED
					pr.setP(initial_probs[j][k]);
#else
					if (fixed_probs) {
						pr.setP(table.st(initial_probs[j][k]));
					} else {
						pr.setP(initial_probs[j][k]);
					}
#endif
				}
			}
		}

		div_table.init();
		for (size_t i = 0; i < div_table.size(); ++i) {
			div_table[i] = table.st(div_table[i]);
		}

		setDataProfile(kBinary);
		owhash = 0;

		// Statistics
		if (statistics) {
			for (auto& c : mixer_skip) c = 0;
		}
	}

	forceinline hash_t hashFunc(size_t a, hash_t b) {
		b += a;
		b += rotate_left(b, 9);
		return b ^ (b >> 6);
	}

	forceinline size_t getOrder0() const {
		return 0;
	}

	forceinline size_t getOrder1(size_t p0) const {
		size_t ret = o0size + (p0 << 8);
		// if (use_prefetch) prefetch(&hash_table[ret]);
		return ret;
	}

	forceinline CMMixer* getProfileMixers(DataProfile profile) {
		return &mixers[static_cast<size_t>(profile) * (mixer_mask + 1)];
	}

	void calcMixerBase() {
		size_t p0 = (byte)buffer[buffer.getPos() - 1];
		size_t p1 = (byte)buffer[buffer.getPos() - 2];
		size_t mixer_ctx = p0 >> 4;
		size_t mm_len = match_model.getLength();
		mixer_ctx <<= 3;
		if (mm_len) {
			mixer_ctx |=
				(mm_len >= match_model.min_match + 0) + 
				(mm_len >= match_model.min_match + 4) + 
				(mm_len >= match_model.min_match + 8) + 
				(mm_len >= match_model.min_match + 16) + 
				(mm_len >= match_model.min_match + 32) + 
				(mm_len >= match_model.min_match + 50);
		}
		mixer_ctx <<= 1;
		if (use_word) {
			auto wlen = word_model.getLength();
			mixer_ctx |= (wlen > 2);
		}
		mixer_base = getProfileMixers(profile) + (mixer_ctx << 8);
	}

	forceinline byte nextState(byte t, size_t bit, size_t smi = 0) {
		if (!fixed_probs) {
#ifdef USE_ST_PRED
			preds[0][smi][t].update(bit, table);
#else
			preds[0][smi][t].update(bit);
#endif
		}
		return state_trans[t][bit];
	}
	
	forceinline int getP(byte state, size_t smi, const short* no_alias st) const {
		if (fixed_probs) {
			return preds[0][smi][state].getP();
		} else {
#ifdef USE_ST_PRED
			return preds[0][smi][state].getSTP();
#else
			return st[preds[0][smi][state].getP()];
#endif
		}
	}

	template <const bool decode, typename TStream>
	size_t processByte(TStream& stream, size_t c = 0) {
		size_t base_contexts[inputs]; // Base contexts

		const size_t blast = buffer.getPos() - 1; // Last seen char
		const size_t
			p0 = (byte)(owhash >> 0),
			p1 = (byte)(owhash >> 8),
			p2 = (byte)(owhash >> 16),
			p3 = (byte)(owhash >> 24),
			p4 = (byte)buffer[blast - 4];

		size_t start = 0;
		base_contexts[start++] = getOrder1(p0); // Order 1
		if (use_word) {
			base_contexts[start++] = hash_lookup(word_model.getHash(), false); // Word model
		}

		if (use_sparse) {
			base_contexts[start++] = hash_lookup(hashFunc(p2, hashFunc(p1, 0x429702E9))); // Order 12
			base_contexts[start++] = hash_lookup(hashFunc(p3, hashFunc(p2, 0x53204647))); // Order 23
			base_contexts[start++] = hash_lookup(hashFunc(p4, hashFunc(p3, 0xD55F1ADB))); // Order 34
		}

		hash_t h = hashFunc(0x32017044, p0);
		for (size_t i = 1; ; ++i) {
			const auto order = i + 1;
			h = hashFunc((byte)buffer[blast - i], h);
			if (order > 1 && order != 5) {
				if (start < inputs)
					base_contexts[start++] = hash_lookup(h);
				else {
					match_model.setHash(h);
					break;
				}
			}
		}
		calcMixerBase();

		// Maximize performance!
		byte* no_alias ht = hash_table;

		const short* no_alias st = table.getStretchPtr();
		size_t huff_state = huff.start_state, code = 0;
		if (!decode) {
			const auto& huff_code = huff.getCode(c);
			code = huff_code.value << (sizeof(size_t) * 8 - huff_code.length);
		}
		for (;;) {
			// Get match model prediction.
			int mm_p = match_model.getP(st);
			size_t ctx = huff_state;

			byte
				*no_alias s0 = nullptr, *no_alias s1 = nullptr, *no_alias s2 = nullptr, *no_alias s3 = nullptr, 
				*no_alias s4 = nullptr, *no_alias s5 = nullptr, *no_alias s6 = nullptr, *no_alias s7 = nullptr;
			
			if (inputs > 1) { s1 = &ht[base_contexts[1] ^ ctx]; }
			if (inputs > 2) { s2 = &ht[base_contexts[2] ^ ctx]; }
			if (inputs > 3) { s3 = &ht[base_contexts[3] ^ ctx]; }
			if (inputs > 4) { s4 = &ht[base_contexts[4] ^ ctx]; }
			if (inputs > 5) { s5 = &ht[base_contexts[5] ^ ctx]; }
			if (inputs > 6) { s6 = &ht[base_contexts[6] ^ ctx]; }
			if (inputs > 7) { s7 = &ht[base_contexts[7] ^ ctx]; }
			if (inputs > 0 && !mm_p) { s0 = &ht[base_contexts[0] ^ ctx]; }

			if (s0 != nullptr) assert(s0 >= ht && s0 <= ht + hash_alloc_size);
			assert(s1 >= ht && s1 <= ht + hash_alloc_size);
			assert(s2 >= ht && s2 <= ht + hash_alloc_size);
			assert(s3 >= ht && s3 <= ht + hash_alloc_size);
			assert(s4 >= ht && s4 <= ht + hash_alloc_size);
			assert(s5 >= ht && s5 <= ht + hash_alloc_size);

			CMMixer* cur_mixer = &mixer_base[ctx];

#if defined(USE_MMX)
			__m128i wa = _mm_cvtsi32_si128(ushort((inputs > 0) ? (mm_p ? mm_p : getP<fixed_probs>(*s0, 0, st)) : 0));
			__m128i wb = _mm_cvtsi32_si128((inputs > 1) ? (int(getP<fixed_probs>(*s1, 1, st)) << 16) : 0);
			if (inputs > 2) wa = _mm_insert_epi16(wa, getP(*s2, 2, st), 2);
			if (inputs > 3) wb = _mm_insert_epi16(wb, getP(*s3, 3, st), 3);
			if (inputs > 4) wa = _mm_insert_epi16(wa, getP(*s4, 4, st), 4);
			if (inputs > 5) wb = _mm_insert_epi16(wb, getP(*s5, 5, st), 5);
			if (inputs > 6) wa = _mm_insert_epi16(wa, getP(*s6, 6, st), 6);
			if (inputs > 7) wb = _mm_insert_epi16(wb, getP(*s7, 7, st), 7);
			__m128i wp = _mm_or_si128(wa, wb);
			int stp = cur_mixer->p(wp);
			size_t p = table.sq(stp); // Mix probabilities.
#else
			int p0 = (inputs > 0) ? (mm_p ? mm_p : getP(*s0, 0, st)) : 0;
			int p1 = (inputs > 1) ? getP(*s1, 1, st) : 0;
			int p2 = (inputs > 2) ? getP(*s2, 2, st) : 0;
			int p3 = (inputs > 3) ? getP(*s3, 3, st) : 0;
			int p4 = (inputs > 4) ? getP(*s4, 4, st) : 0;
			int p5 = (inputs > 5) ? getP(*s5, 5, st) : 0;
			int p6 = (inputs > 6) ? getP(*s6, 6, st) : 0;
			int p7 = (inputs > 7) ? getP(*s7, 7, st) : 0;
			p6 = div_table[match_model.getLength()];
			if (match_model.getExpectedBit()) p6 = -p6;

			int stp = cur_mixer->p(p0, p1, p2, p3, p4, p5, p6, p7);
			size_t p = table.sq(stp); // Mix probabilities.
#endif
			
			size_t bit;
			if (decode) { 
				bit = ent.getDecodedBit(p, shift);
			} else {
				bit = code >> (sizeof(size_t) * 8 - 1);
				code <<= 1;
				ent.encode(stream, bit, p, shift);
			}

#if defined(USE_MMX)
			bool ret = cur_mixer->update(wp, p, bit);
#else
			bool ret = cur_mixer->update(p0, p1, p2, p3, p4, p5, p6, p7, p, bit);
#endif

			if (statistics) {
				// Returns false if we skipped the update due to a low error,
				// should happen frequently on highly compressible files.
				++mixer_skip[(size_t)ret];
			}

			if (inputs > 0 && !mm_p) *s0 = nextState(*s0, bit, 0);
			if (inputs > 1) *s1 = nextState(*s1, bit, 1);
			if (inputs > 2) *s2 = nextState(*s2, bit, 2);
			if (inputs > 3) *s3 = nextState(*s3, bit, 3);
			if (inputs > 4) *s4 = nextState(*s4, bit, 4);
			if (inputs > 5) *s5 = nextState(*s5, bit, 5);
			if (inputs > 6) *s6 = nextState(*s6, bit, 6);
			if (inputs > 7) *s7 = nextState(*s7, bit, 7);

			match_model.updateBit(bit);

			// Encode the bit / decode at the last second.
			if (decode) {
				ent.Normalize(stream);
			}
			huff_state = huff.state_trans[huff_state][bit];
			if (huff.isLeaf(huff_state)) {
				break;
			}
		}

		if (decode) {
			c = huff.getChar(huff_state);
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

	void update(char c) {
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
				size_t expected_char = match_model.getExpectedChar(buffer);
				size_t expected_bits = use_huffman ? huff.getCode(expected_char).length : 8;
				if (use_huffman) expected_char = huff.getCode(expected_char).value;
				match_model.updateExpectedCode(expected_char, expected_bits);
			}
		} else {
			match_model.resetMatch();
		}
		owhash = (owhash << 8) | (size_t)(byte)c;
	}

	template <typename TStream>
	void readSubBlock(TStream& sin, SubBlockHeader& block) {
		block.flags = block_flag_model.decode(ent, sin);
		block.profile = (DataProfile)block_profile_models[(size_t)profile].decode(ent, sin);
	}

	template <typename TStream>
	void writeSubBlock(TStream& sout, const SubBlockHeader& block) {
		block_flag_model.encode(ent, sout, block.flags);
		block_profile_models[(size_t)profile].encode(ent, sout, (size_t)block.profile);
	}

	void BuildHuffCodes(Huffman::Tree<size_t>* tree) {
		// Get the compression codes.
		tree->getCodes(&huff_codes[0]);
	}

	template <typename TOut, typename TIn>
	size_t Compress(TOut& sout, TIn& sin) {
		ProgressMeter meter;
		Detector detector;
		detector.init();

		// Compression profiles.
		std::vector<size_t>
			profile_counts((size_t)kProfileCount, 0),
			profile_len((size_t)kProfileCount, 0);

		// Start by writing out archive header.
		archive_header.write(sout);
		init();
		ent.init();

		if (use_huffman) {
			clock_t start = clock();
			size_t freqs[256];
			std::cout << "Building huffman tree" << std::endl;
			for (auto& f : freqs) f = 0;
			for (;;) {
				int c = sin.read();
				if (c == EOF) break;
				++freqs[c];
			}
			auto* tree = Huffman::buildTreePackageMerge(freqs, 256, huffman_len_limit);
			tree->printRatio("LL");
			Huffman::writeTree(ent, sout, tree, 256, huffman_len_limit);
			huff.build(tree);
			sin.restart();
			std::cout << "Building huffman tree took: " << clock() - start << " MS" << std::endl;
		}

		detector.fill(sin);
		for (;;) {
			if (!detector.size()) break;

			// Block header
			SubBlockHeader block;
			// Initial detection.
			block.profile = detector.detect();
			++profile_counts[(size_t)block.profile];
			writeSubBlock(sout, block);
			setDataProfile(block.profile);

			// Code a block.
			size_t block_size = 0;
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

				processByte<false>(sout, c);
				update(c);

				if (UNLIKELY(c == eof_char)) {
					ent.encode(sout, is_end_of_block, end_of_block_mdl.getP(), shift);
					end_of_block_mdl.update(is_end_of_block);
					if (is_end_of_block) break;
				}

				meter.addBytePrint((size_t)sout.getTotal());
			}
			profile_len[(size_t)block.profile] += block_size;
		}
		_mm_empty();
		std::cout << std::endl;

		// Encode the end of block subblock.
		SubBlockHeader eof_block;
		eof_block.profile = kEOF;
		writeSubBlock(sout, eof_block);
		ent.flush(sout);
		
		// TODO: Put in statistics??
		for (size_t i = 0; i < kProfileCount; ++i) {
			auto cnt = profile_counts[i];
			if (cnt) {
				std::cout << (DataProfile)i << " : " << cnt << "(" << profile_len[i] / KB << "KB)" << std::endl;
			}
		}

		if (statistics) {
			// Dump weights
			std::ofstream fout("probs.txt");
			for (size_t i = 0; i < inputs; ++i) {
				fout << "{";
				for (size_t j = 0; j < 256; ++j) fout << preds[0][i][j].getP() << ",";
				fout << "}," << std::endl;
			}

			// Print average weights so that we find out which contexts are good and which are not!
			for (size_t cur_p = 0; cur_p < static_cast<size_t>(kProfileCount); ++cur_p) {
				auto cur_profile = (DataProfile)cur_p;
				double w1[inputs] = { 0 }, w2[inputs] = { 0 };
				size_t t1 = 0, t2 = 0;
				CMMixer* mixers = getProfileMixers(cur_profile);

				for (size_t i = 0; i <= mixer_mask; ++i) {
					auto& m = mixers[i];
					// Only mixers which have been used at least a few times.
					if (m.getLearn() < 15) {
						if ((i & 0xF00) == 0) { // No match?
							++t1;
							for (size_t i = 0; i < inputs;++i) {
								w1[i] += double(m.getWeight(i)) / double(1 << 16);
							}
						} else {
							++t2;
							for (size_t i = 0; i < inputs;++i) {
								w2[i] += double(m.getWeight(i)) / double(1 << 16);
							}
						}
					}
				}

				std::cout << "Mixer weights for profile " << cur_profile << std::endl;
				if (t1) {
					std::cout << "No match weights: ";
					for (auto& w : w1) std::cout << w / (double(t1) / double(1 << 16)) << " ";
					std::cout << std::endl;
				}
				if (t2) {
					std::cout << "Match weights: ";
					for (auto& w : w2) std::cout << w / (double(t2) / double(1 << 16)) << " ";
					std::cout << std::endl;
				}
			}
			// State count.
			size_t z = 0, nz = 0;
			for (size_t i = 0;i < hash_mask + 1;++i) {
				if (hash_table[i]) ++nz;
				else ++z;
			}
			std::cout << "zero " << z << " nz " << nz << std::endl;
			std::cout << "mixer skip " << mixer_skip[0] << " " << mixer_skip[1] << std::endl;
		}

		return (size_t)sout.getTotal();
	}

	template <typename TOut, typename TIn>
	bool DeCompress(TOut& sout, TIn& sin) {
		// Read the header from the input stream.
		archive_header.read(sin);

		// Check magic && version.
		if (archive_header.magic[0] != 'M' ||
			archive_header.magic[1] != 'C' ||
			archive_header.magic[2] != 'M' ||
			archive_header.version != version) {
			return false;
		}
		
		Transform transform;
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
			transform.setTransform(Transform::kTTAdd1);

			for (;;) {
				size_t c = processByte<true>(sin);
				update(c);

				if (c == eof_char) {
					int eob = ent.decode(sin, end_of_block_mdl.getP(), shift);
					end_of_block_mdl.update(eob);
					if (eob) {
						break; // Hit end of block, go to next block.
					}
				}

				sout.write(c);
				meter.addBytePrint(sin.getTotal());
			}
		}
		_mm_empty();
		std::cout << std::endl;
		return true;
	}	
};

#endif
