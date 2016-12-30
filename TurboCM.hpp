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

#ifndef _TURBO_CM_HPP_
#define _TURBO_CM_HPP_

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

template <const uint32_t level = 6>
class TurboCM : public Compressor {
public:
  // SS table
  static const uint32_t shift = 12;
  static const uint32_t max_value = 1 << shift;
  typedef ss_table<short, max_value, -2 * int(KB), 2 * int(KB), 8> SSTable;
  SSTable table;
  typedef fastBitModel<int, shift, 9, 30> StationaryModel;
  static const int kEOFChar = 233;

  // Contexts
  uint64_t owhash; // Order of sizeof(uint32_t) hash.

  // Probs
  StationaryModel probs[8][256];
  StationaryModel eof_model;
  std::vector<StationaryModel> isse1;
  std::vector<StationaryModel> isse2;
  std::vector<StationaryModel> isse3;
  std::vector<StationaryModel> isse4;

  // CM state table.
  static const uint32_t num_states = 256;
  uint8_t state_trans[num_states][2];

  // SSE.
  SSE<shift> sse_;

  // Fixed models
  uint8_t order0[256];
  uint8_t order1[256 * 256];

  // Hash table
  uint32_t hash_mask;
  MemMap hash_storage;
  uint8_t* hash_table;

  // Learn rate
  uint32_t byte_count;

  // Range encoder
  Range7 ent;

  // Match model.
  CyclicBuffer<uint8_t> buffer_;
  MatchModel<StationaryModel> match_model_;

  // Memory usage
  uint32_t mem_usage;

  // Word model.
  WordModel word_model;

  // Optimization variable.
  uint32_t opt_var = 0;

  // Mixer
  typedef Mixer<int, 4> CMMixer;
  MixerArray<CMMixer> mix1_, mix2_;

  TurboCM(size_t mem = 6) : mem_usage(mem) {}

  bool setOpt(uint32_t var) {
    opt_var = var;
    return true;
  }

  void setMemUsage(uint32_t m) {
    mem_usage = m;
  }

  void init() {
    ReorderMap<uint8_t, 256> reorder;
    ent = Range7();
    for (auto& c : order0) c = 0;
    for (auto& c : order1) c = 0;
    table.build(0);

    hash_mask = ((2 * MB) << mem_usage) / sizeof(hash_table[0]) - 1;
    buffer_.Resize((MB / 4) << mem_usage, sizeof(uint32_t));
    hash_storage.resize(hash_mask + 1); // Add extra space for ctx.
    match_model_.resize(buffer_.Size() / 2);
    match_model_.init(5, 100);
    std::cout << hash_mask + 1 << std::endl;
    hash_table = reinterpret_cast<uint8_t*>(hash_storage.getData());

    mix1_.Init(0x10000, 382);
    mix2_.Init(0x10000, 382);

    sse_.init(256 * 256, &table);

    NSStateMap<12> sm;
    sm.build();

    // Optimization
    for (uint32_t i = 0; i < num_states; ++i) {
      // Pre stretch state map.
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

    for (uint32_t j = 0; j < 8;++j) {
      for (uint32_t k = 0; k < num_states; ++k) {
        auto& pr = probs[j][k];
        pr.init();
        pr.setP(std::max(initial_probs[j][k], static_cast<unsigned short>(1)));
      }
    }

    isse1.resize(256 * 256); for (auto& pr : isse1) pr.init();
    isse2.resize(256 * 256);
#if 1
    for (size_t i = 0; i < 256; ++i) {
      for (size_t j = 0; j < 256; ++j) {
        int prob = table.sq(table.st(initial_probs[0][i]) + table.st(initial_probs[0][j]));
        isse2[i * 256 + j].init(prob);
      }
    }
#else
    for (auto& pr : isse2) pr.init();
#endif
    isse3.resize(256 * 256); for (auto& pr : isse3) pr.init();
    isse4.resize(256 * 256); for (auto& pr : isse4) pr.init();

    eof_model.init();

    word_model.Init(reorder);

    owhash = 0;
    byte_count = 0;
  }

  ALWAYS_INLINE uint8_t nextState(uint8_t t, uint32_t bit, uint32_t smi = 0) {
    return state_trans[t][bit];
  }

  uint32_t hashify(uint64_t h) const {
    if (false) {
      h += h >> 29;
      h += h >> 1;
      h += h >> 2;
    } else {
      // 29
      // h ^= h * (24 * opt_var + 45);
      h ^= h * ((1 << 13) - 5 + 96);
      h ^= (h >> 15);
    }
    return h;
  }

  template <const bool kDecode, typename TStream>
  uint32_t processLZPBit(TStream& stream, size_t expected_char, uint32_t bit = 0) {
    size_t lzp_hash = owhash;
    size_t lzp_bit_hash = (hashify(lzp_hash ^ (lzp_hash >> 26))) & hash_mask;
    const uint32_t learn_rate = 9;
    uint8_t *no_alias s0 = &hash_table[lzp_bit_hash];
    auto& pr0 = probs[4][*s0];
    int p = pr0.getP();
    p += p == 0;
    if (kDecode) {
      bit = ent.getDecodedBit(p, shift);
    } else {
      ent.encode(stream, bit, p, shift);
    }
    pr0.update(bit, learn_rate);
    // Encode the bit / decode at the last second.
    if (kDecode) {
      ent.Normalize(stream);
    }
    return bit;
  }

  template <const bool kDecode, typename TStream>
  uint32_t processByte(TStream& stream, uint32_t c = 0) {
    size_t mm_len = 0;
    if (true) {
      match_model_.setHash(hashify(owhash ^ (owhash >> 26)));
      match_model_.update(buffer_);
      if (mm_len = match_model_.getLength()) {
        uint32_t p0 = owhash & 0xFF;
        match_model_.setCtx(p0);
        match_model_.updateCurMdl();
        auto expected_char = match_model_.getExpectedChar(buffer_);
        match_model_.updateExpectedCode(expected_char, 8);
      }
    }
    static const bool kLZP = true;
    const size_t mm_l = match_model_.getLength();
    if (mm_l != 0 && kLZP) {
      size_t expected_char = match_model_.getExpectedChar(buffer_);
      size_t bit = 0;
      if (!kDecode) {
        bit = expected_char == c;
      }
      bit = processLZPBit<kDecode>(stream, expected_char, bit);
      if (bit) {
        return expected_char;
      }
      // Mispredicted, need to encode char.
      return processByteInternal<kDecode, /*kSSE*/true, kLZP>(stream, c, expected_char * 256);
    } else {
      return processByteInternal<kDecode, /*kSSE*/false, kLZP>(stream, c, 0);
    }
  }

  template <const bool kDecode, bool kSSE, bool kLZP, typename TStream>
  uint32_t processByteInternal(TStream& stream, uint32_t c = 0, size_t sse_ctx = 0) {
    uint32_t p0 = owhash & 0xFF;
    uint8_t* o0ptr = &order0[0];
    uint8_t* o1ptr = &order1[p0 << 8];
    uint32_t o2h = (hashify(owhash & 0xFFFF) + 0x3124) & hash_mask;
    uint32_t o3h = (hashify(owhash & 0xFFFFFF) + 997 * 0) & hash_mask;
    uint32_t o4h = (hashify(owhash & 0xFFFFFFFF) + 798765431) & hash_mask;
    uint32_t wh = hashify(word_model.getHash()) & hash_mask;
    const uint32_t learn_rate = 9;

    uint32_t code = 0;
    if (!kDecode) {
      code = c << (sizeof(uint32_t) * 8 - 8);
    }
    uint32_t len = word_model.getLength();
    int ctx = 1;
    do {
      uint8_t
        *no_alias s0 = nullptr, *no_alias s1 = nullptr, *no_alias s2 = nullptr, *no_alias s3 = nullptr,
        *no_alias s4 = nullptr, *no_alias s5 = nullptr, *no_alias s6 = nullptr, *no_alias s7 = nullptr;

      s0 = &o1ptr[ctx];
      s1 = &hash_table[o2h ^ ctx];
      s2 = &hash_table[o4h ^ ctx];
      s3 = &hash_table[wh ^ ctx];

      size_t st[] = { *s0, *s1, *s2, *s3 };
      auto& mixer = kSSE ? mix2_.GetMixer()[sse_ctx + ctx] : mix1_.GetMixer()[p0 * 256 + ctx];
      // auto& mixer = mixers_.GetMixer()[static_cast<size_t>(*s3) * 256 + st[opt_var & 3]];

      const size_t kDiv = (16u << opt_var);
#define USE_ISSE 0
#if USE_ISSE == 1
      auto& pr0 = probs[0][*s0];
      // auto& pr1 = probs[1][*s1];
      // auto& pr2 = probs[2][*s2];
      // auto& pr3 = probs[3][*s3];

      int p = pr0.getP();
      const size_t kDiv = (16u << opt_var);
      // auto& is1 = isse1[(p / kDiv) * 256 + *s1]; p = is1.getP();
      // auto& is2 = isse2[(p / kDiv) * 256 + *s2]; p = is2.getP();
      // auto& is3 = isse3[(p / kDiv) * 256 + *s3]; p = is3.getP();
      auto& is1 = isse1[static_cast<size_t>(*s1) * 256 + *s0];
      auto& is2 = isse2[static_cast<size_t>(*s3) * 256 + *s2];
      auto& is3 = isse3[static_cast<size_t>(is2.getP() / kDiv) * 256 + is1.getP() / kDiv];
      p = is3.getP();
      p += p == 0;
#elif USE_ISSE == 2
      auto& pr0 = probs[0][*s0];

      int p = pr0.getP();
      auto& is1 = isse1[(p / kDiv) * 256 + *s1]; p = is1.getP();
      auto& is2 = isse2[(p / kDiv) * 256 + *s2]; p = is2.getP();
      auto& is3 = isse3[(p / kDiv) * 256 + *s3]; p = is3.getP();
      p = is3.getP();
      p += p == 0;
#else
      auto& pr0 = probs[0][*s0];
      auto& pr1 = probs[1][*s1];
      auto& pr2 = probs[2][*s2];
      auto& pr3 = probs[3][*s3];

      int mm_p = 0;
      int p0 = table.st(pr0.getP());
      if (!kLZP && match_model_.getLength()) {
        mm_p = match_model_.getP(table.getStretchPtr(), match_model_.GetExpectedBit());
        p0 = mm_p;
      }
      int p1 = table.st(pr1.getP());
      int p2 = table.st(pr2.getP());
      int p3 = table.st(pr3.getP());
      int stp = 0; // mixer.p(9, p0, p1, p2, p3);
      int mp1 = table.sq(stp);
      int p;
      if (kSSE) {
        p = sse_.p(2048 + Clamp(stp, -2047, 2047), sse_ctx + ctx);
        p += 0;
      } else {
        p = mp1;
      }
#endif

      uint32_t bit;
      if (kDecode) {
        bit = ent.getDecodedBit(p, shift);
      } else {
        bit = code >> (sizeof(uint32_t) * 8 - 1);
        code <<= 1;
        ent.encode(stream, bit, p, shift);
      }
      ctx = ctx * 2 + bit;

      bool update = true;
#if USE_ISSE != 0
      is1.update(bit, learn_rate);
      is2.update(bit, learn_rate);
      is3.update(bit, learn_rate);
      // is3.update(bit, learn_rate);
      pr0.update(bit, learn_rate);
      // pr1.update(bit, learn_rate);
      // pr2.update(bit, learn_rate);
      // pr3.update(bit, learn_rate);
#else
      update = true; // mixer.update(mp1, bit, 12, 16, 1, p0, p1, p2, p3, 0, 0, 0, 0);
      if (update) {
        pr0.update(bit, learn_rate);
        pr1.update(bit, learn_rate);
        pr2.update(bit, learn_rate);
        pr3.update(bit, learn_rate);
      }
#endif
      if (update) {
        *s0 = nextState(*s0, bit, 0);
        *s1 = nextState(*s1, bit, 1);
        *s2 = nextState(*s2, bit, 2);
        *s3 = nextState(*s3, bit, 3);
      }
      if (!kLZP && match_model_.getLength()) {
        match_model_.UpdateBit(bit);
      }
      if (kSSE) {
        sse_.update(bit);
      }

      // Encode the bit / decode at the last second.
      if (kDecode) {
        ent.Normalize(stream);
      }
    } while ((ctx & 0x100) == 0);
    return ctx ^ 256;
  }

  void update(char c) {
    word_model.update(c);
    buffer_.Push(c);
    ++byte_count;
  }

  void compress(Stream* in_stream, Stream* out_stream, uint64_t max_count) {
    BufferedStreamReader<4 * KB> sin(in_stream);
    BufferedStreamWriter<4 * KB> sout(out_stream);
    assert(in_stream != nullptr);
    assert(out_stream != nullptr);
    init();
    while (max_count != 0) {
      int c = sin.get();
      if (c == EOF) break;
      processByte<false>(sout, c);
      if (c == kEOFChar) {
        ent.encodeBit(sout, 0);
      }
      update(c);
      --max_count;
    }
    processByte<false>(sout, kEOFChar);
    ent.encodeBit(sout, 1);
    ent.flush(sout);
  }

  void decompress(Stream* in_stream, Stream* out_stream, uint64_t max_count) {
    BufferedStreamReader<4 * KB> sin(in_stream);
    BufferedStreamWriter<4 * KB> sout(out_stream);
    assert(in_stream != nullptr);
    assert(out_stream != nullptr);
    init();
    ent.initDecoder(sin);
    while (max_count != 0) {
      int c = processByte<true>(sin);
      if (c == kEOFChar) {
        if (ent.decodeBit(sin) == 1) {
          break;
        }
      }
      sout.put(c);
      update(c);
      --max_count;
    }
  }
};

#endif
