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
#include <type_traits>
#include <vector>

#include "BracketModel.hpp"
#include "Detector.hpp"
#include "DivTable.hpp"
#include "Entropy.hpp"
#include "Huffman.hpp"
#include "Log.hpp"
#include "MatchModel.hpp"
#include "Memory.hpp"
#include "Mixer.hpp"
#include "Model.hpp"
#include "ProbMap.hpp"
#include "Range.hpp"
#include "Reorder.hpp"
#include "StateMap.hpp"
#include "Util.hpp"
#include "WordModel.hpp"
#include "SSE.hpp"

namespace cm {
  // Flags for which models are enabled.
  enum ModelType {
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
    kModelBracket,
    kModelSparse2,

    kModelSparse3,
    kModelSparse4,
    kModelSparse23,
    kModelSparse34,
    kModelWord1,

    kModelWord2,
    kModelWord12,
    kModelInterval,
    kModelInterval2,
    kModelInterval3,
    kModelSpecialChar,

    kModelCount,
  };

  class CMProfile {
    static constexpr size_t kMaxOrder = 12;
  public:
    ALWAYS_INLINE bool ModelEnabled(ModelType model, ModelType*& out) const {
      bool enabled = ModelEnabled(model);
      if (enabled && out != nullptr) *(out++) = model;
      return enabled;
    }
    ALWAYS_INLINE bool ModelEnabled(ModelType model) const {
      return (enabled_models_ & (1U << static_cast<uint32_t>(model))) != 0;
    }

    ALWAYS_INLINE void EnableModel(ModelType model) {
      enabled_models_ |= 1 << static_cast<size_t>(model);
      CalculateMaxOrder();
    }

    template <typename T>
    void EnableModels(const T* models, size_t count) {
      for (size_t i = 0; i < count; ++i) {
        EnableModel(static_cast<ModelType>(models[i]));
      }
    }

    void CalculateMaxOrder() {
      max_model_order_ = 0;
      for (size_t order = 0; order <= kMaxOrder; ++order) {
        if (ModelEnabled(static_cast<ModelType>(kModelOrder0 + order))) {
          max_model_order_ = order;
        }
      }
      max_order_ = std::max(max_model_order_, match_model_order_);
    }

    void SetMinLZPLen(size_t len) {
      min_lzp_len_ = len;
    }

    size_t MinLZPLen() const {
      return min_lzp_len_;
    }

    void SetMatchModelOrder(size_t order) {
      match_model_order_ = order ? order - 1 : 0;
      max_order_ = std::max(max_model_order_, match_model_order_);
    }

    size_t MatchModelOrder() const {
      return match_model_order_;
    }

    size_t MaxModelOrder() const {
      return max_model_order_;
    }

    size_t MaxOrder() const {
      return max_order_;
    }

    void SetMissFastPath(size_t len) {
      miss_fast_path_ = len;
    }

    size_t MissFastPath() {
      return miss_fast_path_;
    }

    static CMProfile CreateSimple(size_t inputs, size_t min_lzp_len = 10) {
      CMProfile base;
      base.EnableModel(kModelOrder0);
      size_t idx = 0;
      if (inputs > idx++) base.EnableModel(kModelOrder0);
      if (inputs > idx++) base.EnableModel(kModelOrder1);
      if (inputs > idx++) base.EnableModel(kModelOrder2);
      if (inputs > idx++) base.EnableModel(kModelOrder3);
      if (inputs > idx++) base.EnableModel(kModelOrder4);
      if (inputs > idx++) base.EnableModel(kModelOrder6);
      if (inputs > idx++) base.EnableModel(kModelOrder7);
      if (inputs > idx++) base.EnableModel(kModelOrder8);
      if (inputs > idx++) base.EnableModel(kModelOrder9);
      base.SetMatchModelOrder(8);
      base.SetMinLZPLen(min_lzp_len);
      return base;
    }

  private:
    // Parameters.
    uint64_t enabled_models_ = 0;
    size_t min_lzp_len_ = 0xFFFFFFFF;
    size_t miss_fast_path_ = 0xFFFFFFFF;

    // Calculated.
    size_t max_model_order_ = 0;
    size_t match_model_order_ = 0;
    size_t max_order_ = 0;
  };

  class ByteStateMap {
  public:
    ALWAYS_INLINE static bool IsLeaf(uint32_t state) {
      return (state >> 8) != 0;
    }

    ALWAYS_INLINE uint32_t Next(uint32_t state, uint32_t bit) const {
      return next_[state][bit];
    }

    ALWAYS_INLINE uint32_t GetBits(uint32_t state) {
      return bits_[state];
    }

    ALWAYS_INLINE void SetBits(uint32_t state, uint32_t bits) {
      bits_[state] = bits;
    }

    void SetNext(uint32_t state, uint32_t bit, uint32_t next) {
      next_[state][bit] = next;
    }

  private:
    uint16_t next_[256][2] = {};
    uint8_t bits_[256] = {};
  };

  class VoidHistoryWriter {
  public:
    uint32_t end() { return 0u; }
    void emplace(uint32_t e, uint32_t a, uint32_t b) {}
  };

  template <size_t kInputs, bool kUseSSE, typename HistoryType = VoidHistoryWriter>
  class CM : public Compressor {
  public:
    // Internal set of special profiles.
    enum DataProfile {
      kProfileText,
      kProfileBinary,
      kProfileSimple,
      kProfileCount,
    };

    // Flags
    static const bool kStatistics = false;
    static const bool kFastStats = true;
    static const bool kFixedProbs = false;
    static const bool kUseLZP = true;
    static const bool kUseLZPSSE = true;
    // Prefetching related flags.
    static const bool kUsePrefetch = true;
    static const bool kPrefetchMatchModel = true;
    static const bool kPrefetchWordModel = true;
    static const bool kFixedMatchProbs = false;

    // SS table
    static const uint32_t kShift = 12;
    static const int kMaxValue = 1 << kShift;
    static const int kMinST = -kMaxValue / 2;
    static const int kMaxST = kMaxValue / 2;
    typedef ss_table<short, kMaxValue, kMinST, kMaxST, 8> SSTable;
    SSTable table_;

    typedef safeBitModel<unsigned short, kShift, 5, 15> BitModel;
    typedef fastBitModel<int, kShift, 9, 30> StationaryModel;
    typedef fastBitModel<int, kShift, 9, 30> HPStationaryModel;

    // Word model
    // XMLWordModel word_model_;
    // WordModel word_model_;
    DictXMLModel word_model_;
    size_t word_model_ctx_map_[WordModel::kMaxLen + 1];

    // Bracket model
    BracketModel bracket_;
    LastSpecialCharModel special_char_model_;

    FrequencyCounter<256> frequencies_;

    Range7 ent;

    typedef MatchModel<HPStationaryModel> MatchModelType;
    MatchModelType match_model_;
    std::vector<int> fixed_match_probs_;

    // Hash table
    size_t hash_mask_;
    size_t hash_alloc_size_;
    MemMap hash_storage_;
    uint8_t *hash_table_;

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
    static const size_t kHashStart = s4pos + o1size;

    // Maps from uint8_t to 4 bit identifier.
    uint8_t* current_interval_map_;
    uint8_t* current_interval_map2_;
    uint8_t* current_small_interval_map_;
    uint8_t binary_interval_map_[256];
    uint8_t binary_small_interval_map_[256];
    uint8_t text_interval_map_[256];
    uint8_t text_interval_map2_[256];
    uint8_t text_small_interval_map_[256];
    uint64_t interval_mask_ = 0;
    uint64_t interval2_mask_ = 0;

    // Mixers
    typedef Mixer<int, kInputs> CMMixer;
    static constexpr size_t kNumMixers = 1;
    static constexpr size_t kMixerBits16 = 15;
    static constexpr size_t kMixerBits32 = 17;
    static constexpr size_t kMixerBits = kMixerBits32;
    MixerArray<CMMixer> mixers_[kNumMixers];
    size_t interval_mixer_mask_;
    uint8_t mixer_text_learn_[kModelCount];
    uint8_t mixer_binary_learn_[kModelCount];

    // Stage 
    typedef Mixer<int, kNumMixers> CMMixer2;
    MixerArray<CMMixer2> mix2_;
    uint32_t mixer_match_ = 0;
    static const size_t kMaxLearn = 256;
    int mixer_update_rate_[kMaxLearn];

    // 8 recently seen bytes.
    uint64_t last_bytes_;

    // Rotating buffer.
    CyclicBuffer<uint8_t> buffer_;

    // Options
    uint64_t opt_var_ = 0;
    size_t dummy_opts[256] = {};
    size_t* opts_;

    // CM state table.
    static const uint32_t kNumStates = 256;
    uint8_t state_trans_[kNumStates][2];

    // Huffman preprocessing.
    static const bool use_huffman = false;
    static const uint32_t huffman_len_limit = 16;
    Huffman huff;

    // If force profile is true then we dont use a detector.
    bool force_profile_;

    // CM profiles.
    CMProfile text_profile_;
    CMProfile text_match_profile_;
    CMProfile binary_profile_;
    CMProfile binary_match_profile_;
    CMProfile simple_profile_;

    // Current (active) profile.
    CMProfile cur_profile_;
    CMProfile cur_match_profile_;

    // Interval model.
    uint64_t interval_model_ = 0;
    uint64_t interval_model2_ = 0;
    uint64_t small_interval_model_ = 0;

    // LZP
    bool lzp_enabled_;
    static const size_t kMaxMatch = 100;

    // SM
    typedef StationaryModel PredModel;
    static const size_t kProbCtxPer = kInputs + 1;
    static const size_t kProbCtx = kProbCtxPer * 2;
    //FastProbMap<StationaryModel, 256> probs_[kProbCtx];
    // DynamicProbMap<StationaryModel, 256> probs_[kProbCtx];
    FastAdaptiveProbMap<256> probs_[kProbCtx];
    int16_t fast_probs_[256];
    uint32_t prob_ctx_add_ = 0;

    // Ctx state map
    using ByteState = ByteStateMap;
    ByteState ctx_state_;

    // SSE
    SSE<kShift> sse_;
    SSE<kShift> sse2_;
    // SSE<kShift> sse3_;
    SSE<kShift> sse3_;
    // MixSSE<kShift> sse3_;
    // APM2 sse3_;
    size_t sse_ctx_;
    size_t mixer_sse_ctx_;

    // Reorder
    ReorderMap<uint8_t, 256> reorder_;
    uint8_t text_reorder_[256];
    uint8_t binary_reorder_[256];

    // Active data profile.
    DataProfile data_profile_;

    // Fast mode. TODO split this in another compressor?
    // Quickly create a probability from a 2d array.
    HPStationaryModel fast_mix_[256 * 256];

    static_assert(kModelCount <= 32, "no room in word");

    // Statistics
    uint64_t mixer_skip_[2];
    uint64_t match_count_, non_match_count_, other_count_;
    uint64_t lzp_bit_match_bytes_, lzp_bit_miss_bytes_, lzp_miss_bytes_, normal_bytes_;
    uint64_t match_hits_[kMaxMatch], match_miss_[kMaxMatch];
    static const size_t kDiffCounter = 256;
    uint64_t ctx_count_[kDiffCounter] = {};

    static const uint64_t kMaxMiss = 512;
    uint64_t miss_len_;
    uint64_t miss_count_[kMaxMiss];
    uint64_t fast_bytes_;

    size_t mem_level_ = 0;

    HistoryType* out_history_ = nullptr;

    ALWAYS_INLINE uint32_t HashLookup(hash_t hash, bool prefetch_addr) {
      hash &= hash_mask_;
      const uint32_t ret = hash + kHashStart;
      if (prefetch_addr && kUsePrefetch) {
        if (opt_var_ & 1) {
          Prefetch(hash_table_ + ret);
        } else {
          Prefetch(hash_table_ + (ret & ~(kCacheLineSize - 1)));
        }
      }
      return ret;
    }

    void SetOutHistory(HistoryType* out_history) {
      out_history_ = out_history;
    }

    CM(const FrequencyCounter<256>& freq,
       uint32_t mem_level = 8,
       bool lzp_enabled = true,
       Detector::Profile profile = Detector::kProfileDetect);

    bool setOpt(uint32_t var) OVERRIDE {
      opt_var_ = var;
      word_model_.setOpt(var);
      match_model_.setOpt(var);
      // opt_var_ = var;
      // sse3_.setOpt(var);
      return true;
    }

    virtual bool setOpts(size_t* opts) OVERRIDE {
      opts_ = opts;
      special_char_model_.SetOpts(opts);
      bracket_.SetOpts(opts);
      word_model_.SetOpts(opts);
      return true;
    }

    void init();

    ALWAYS_INLINE uint32_t HashFunc(uint64_t a, uint64_t b) const {
      b += a;
      b += rotate_left(b * 7, 11);
      return b ^ (b >> 13);
    }

    void SetStates(const uint32_t* remap);
    void SetUpCtxState();
    void OptimalCtxState();

    void CalcMixerBase() {
      uint32_t mixer_ctx = 0;
      auto mm_len = match_model_.getLength();
      if (current_interval_map_ == binary_interval_map_) {
        mixer_ctx = interval_model_ & interval_mixer_mask_;
        // mixer_ctx = last_bytes_ & 0xFF;
        if (false) {
          mixer_ctx = (mixer_ctx << 2);
          // mixer_ctx |= (mm_len >= 0) + (mm_len >= match_model_.kMinMatch);
          mixer_ctx |= (mm_len > 0) +
            (mm_len >= match_model_.kMinMatch + 1) + 
					  (mm_len >= match_model_.kMinMatch + 4);
        } else {
          mixer_ctx = (mixer_ctx << 1) + (mm_len > 0);
        }
      } else {
        const size_t current_interval = small_interval_model_ & interval_mixer_mask_;
        mixer_ctx = current_interval;
        mixer_ctx = (mixer_ctx << 1) | (mm_len > 0 || word_model_.getLength() > 6);
      }
      mixers_[0].SetContext(mixer_ctx << 8);
    }

    ALWAYS_INLINE uint8_t NextState(uint32_t index, uint8_t state, size_t bit, uint32_t updater, uint32_t ctx, size_t update = 9) {
      if (!kFixedProbs) {
        probs_[ctx + prob_ctx_add_].Update(state, updater, table_, update);
      }
      return state_trans_[state][bit];
    }

    ALWAYS_INLINE int getP(uint8_t state, uint32_t ctx) const {
      return probs_[ctx + kProbCtxPer].GetP(state);
    }

    ALWAYS_INLINE int GetSTP(uint8_t state, uint32_t ctx) const {
      return probs_[ctx + prob_ctx_add_].GetSTP(state, table_);
    }

    enum BitType {
      kBitTypeLZP,
      kBitTypeNormal,
      kBitTypeNormalSSE,
    };

    uint64_t IntervalHash(uint64_t model) {
      return hashify(model);
    }

		template <const bool kDecode, BitType kBitType, size_t kBits, typename TStream>
		size_t ProcessBits(TStream& stream, const size_t c, size_t* base_contexts, size_t ctx_add) {
			uint32_t code = 0;
			if (!kDecode) {
        code = c << (sizeof(uint32_t) * kBitsPerByte - kBits);
			}
      size_t base_ctx = 0;
      size_t cur_ctx = 0;
      size_t bits = kBits;
			do {
        const size_t mixer_ctx = base_ctx + cur_ctx;
        const size_t ctx = mixer_ctx + ctx_add;
				size_t bit = 0;
				if (!kDecode) {
          bit = code >> (sizeof(uint32_t) * 8 - 1);
          code <<= 1;
				}
				const auto mm_l = match_model_.getLength();
				uint8_t
					*rst sp0, *rst sp1, *rst sp2, *rst sp3, *rst sp4, *rst sp5, *rst sp6, *rst sp7,
					*rst sp8, *rst sp9, *rst sp10, *rst sp11, *rst sp12, *rst sp13, *rst sp14, *rst sp15;
				uint8_t
					s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0, s5 = 0, s6 = 0, s7 = 0,
					s8 = 0, s9 = 0, s10 = 0, s11 = 0, s12 = 0, s13 = 0, s14 = 0, s15 = 0;

				uint32_t p;
				int32_t
					p0 = 0, p1 = 0, p2 = 0, p3 = 0, p4 = 0, p5 = 0, p6 = 0, p7 = 0,
					p8 = 0, p9 = 0, p10 = 0, p11 = 0, p12 = 0, p13 = 0, p14 = 0, p15 = 0;
				constexpr bool kUseAdd = false;
				auto ctx_xor = kUseAdd ? 0 : ctx;
				auto ht = kUseAdd ? hash_table_ + ctx : hash_table_;
				if (kBitType == kBitTypeLZP) {
					if (kInputs > 0) {
						if (kFixedMatchProbs) {
							p0 = fixed_match_probs_[mm_l * 2 + 1];
						} else {
							p0 = match_model_.getP(table_.getStretchPtr(), 1);
						}
					}
				} else if (mm_l == 0) {
					if (kInputs > 0) {
						sp0 = &ht[base_contexts[0] ^ ctx_xor];
						s0 = *sp0;
						p0 = GetSTP(s0, 0);
					}
				} else {
					if (kInputs > 0) {
						if (kFixedMatchProbs) {
							p0 = fixed_match_probs_[mm_l * 2 + match_model_.GetExpectedBit()];
						} else {
							p0 = match_model_.getP(table_.getStretchPtr(), match_model_.GetExpectedBit());
						}
					}
				}
				if (kInputs > 1) s1 = *(sp1 = &ht[base_contexts[1] ^ ctx_xor]);
				if (kInputs > 2) s2 = *(sp2 = &ht[base_contexts[2] ^ ctx_xor]);
				if (kInputs > 3) s3 = *(sp3 = &ht[base_contexts[3] ^ ctx_xor]);
				if (kInputs > 4) s4 = *(sp4 = &ht[base_contexts[4] ^ ctx_xor]);
				if (kInputs > 5) s5 = *(sp5 = &ht[base_contexts[5] ^ ctx_xor]);
				if (kInputs > 6) s6 = *(sp6 = &ht[base_contexts[6] ^ ctx_xor]);
				if (kInputs > 7) s7 = *(sp7 = &ht[base_contexts[7] ^ ctx_xor]);
				if (kInputs > 8) s8 = *(sp8 = &ht[base_contexts[8] ^ ctx_xor]);
				if (kInputs > 9) s9 = *(sp9 = &ht[base_contexts[9] ^ ctx_xor]);
				if (kInputs > 10) s10 = *(sp10 = &ht[base_contexts[10] ^ ctx_xor]);
				if (kInputs > 11) s11 = *(sp11 = &ht[base_contexts[11] ^ ctx_xor]);
				if (kInputs > 12) s12 = *(sp12 = &ht[base_contexts[12] ^ ctx_xor]);
				if (kInputs > 13) s13 = *(sp13 = &ht[base_contexts[13] ^ ctx_xor]);
				if (kInputs > 14) s14 = *(sp14 = &ht[base_contexts[14] ^ ctx_xor]);
				if (kInputs > 15) s15 = *(sp15 = &ht[base_contexts[15] ^ ctx_xor]);

				if (kInputs > 1) p1 = GetSTP(s1, 1);
				if (kInputs > 2) p2 = GetSTP(s2, 2);
				if (kInputs > 3) p3 = GetSTP(s3, 3);
				if (kInputs > 4) p4 = GetSTP(s4, 4);
				if (kInputs > 5) p5 = GetSTP(s5, 5);
				if (kInputs > 6) p6 = GetSTP(s6, 6);
				if (kInputs > 7) p7 = GetSTP(s7, 7);
				if (kInputs > 8) p8 = GetSTP(s8, 8);
				if (kInputs > 9) p9 = GetSTP(s9, 9);
				if (kInputs > 10) p10 = GetSTP(s10, 10);
				if (kInputs > 11) p11 = GetSTP(s11, 11);
				if (kInputs > 12) p12 = GetSTP(s12, 12);
				if (kInputs > 13) p13 = GetSTP(s13, 13);
				if (kInputs > 14) p14 = GetSTP(s14, 14);
				if (kInputs > 15) p15 = GetSTP(s15, 15);
				int m0p, m1p, m2p, stage2p;
				CMMixer* m0 = mixers_[0].GetMixer() + mixer_ctx;
				m0p = m0->P(kMixerBits, p0, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15);
				int stp = m0p;
				int mixer_p = table_.sqfast(stp); // Mix probabilities.
				p = mixer_p;
				bool sse3 = false;
				if (kUseLZP) {
					if (kUseLZPSSE) {
						if (kBitType == kBitTypeLZP || kBitType == kBitTypeNormalSSE) {
							stp = Clamp(stp, kMinST, kMaxST - 1);
              if (kBitType == kBitTypeLZP) {
                p = sse2_.p(stp + kMaxValue / 2, sse_ctx_ + mm_l);
              } else {
                p = sse_.p(stp + kMaxValue / 2, sse_ctx_ + mixer_ctx);
              }
              p += p == 0;
            } else if (kUseSSE) {
              stp = Clamp(stp, kMinST, kMaxST - 1);
              constexpr uint32_t kDiv = 32;
              const uint32_t blend = 14;
              int input_p = stp + kMaxValue / 2;
							p = (p * blend + sse3_.p(input_p, (last_bytes_ & 0xFF) * 256 + mixer_ctx) * (kDiv - blend)) / kDiv;
							// p = (p * opt_var_ + sse3_.p(stp + kMaxValue / 2, (interval_model_ & 0xFF) * 256 + mixer_ctx) * (kDiv - opt_var_)) / kDiv;
							// p = sse3_.p(stp + kMaxValue / 2, (interval_model_ & 0xFF) * 256 + mixer_ctx);
							// p = sse3_.p(stp + kMaxValue / 2, mixer_ctx);
							// p = (p * 1 + sse3_.p(stp + kMaxValue / 2, (mix1_.GetContext() & 0xFF00) + mixer_ctx) * 15) / 16;
							p += p == 0;
							mixer_p = p;
							sse3 = true;
						}
					}
				}
				else if (true) {
					p = (p * 1 + sse3_.p(stp + kMaxValue / 2, (last_bytes_ & 0xFF) * 256 + mixer_ctx) * 15) / 16;
					p += p == 0;
					// mixer_p = p;
					sse3 = true;
				}

				if (kDecode) {
					bit = ent.getDecodedBit(p, kShift);
				}
				dcheck(bit < 2);

				const size_t kLimit = kMaxLearn - 1;
				const size_t kDelta = 5;
				// Returns false if we skipped the update due to a low error, should happen moderately frequently on highly compressible files.
				bool ret = m0->Update(
					mixer_p, bit,
					kShift, kLimit, 600, 1,
					// mixer_update_rate_[m0->NextLearn(8)], 16,
					mixer_update_rate_[m0->GetLearn()], 16,
					p0, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15
				);
				// Only update the states / predictions if the mixer was far enough from the bounds, helps 60k on enwik8 and 1-2sec.
				const bool kOptP = false;
				if (ret) {
					auto updater = probs_[0].GetUpdater(bit);
          if (kBitType != kBitTypeLZP && mm_l == 0) {
            if (kInputs > 0) *sp0 = NextState(sp0 - hash_table_, s0, bit, updater, 0, kOptP ? opts_[0] : 23);
          }
					if (kInputs > 1) *sp1 = NextState(sp1 - hash_table_, s1, bit, updater, 1, kOptP ? opts_[1] : 10);
					if (kInputs > 2) *sp2 = NextState(sp2 - hash_table_, s2, bit, updater, 2, kOptP ? opts_[2] : 9);
					if (kInputs > 3) *sp3 = NextState(sp3 - hash_table_, s3, bit, updater, 3, kOptP ? opts_[3] : 9);
					if (kInputs > 4) *sp4 = NextState(sp4 - hash_table_, s4, bit, updater, 4, kOptP ? opts_[4] : 9);
					if (kInputs > 5) *sp5 = NextState(sp5 - hash_table_, s5, bit, updater, 5, kOptP ? opts_[5] : 9);
					if (kInputs > 6) *sp6 = NextState(sp6 - hash_table_, s6, bit, updater, 6, kOptP ? opts_[6] : 9);
					if (kInputs > 7) *sp7 = NextState(sp7 - hash_table_, s7, bit, updater, 7, kOptP ? opts_[7] : 9);
					if (kInputs > 8) *sp8 = NextState(sp8 - hash_table_, s8, bit, updater, 8, kOptP ? opts_[8] : 9);
					if (kInputs > 9) *sp9 = NextState(sp9 - hash_table_, s9, bit, updater, 9, kOptP ? opts_[9] : 9);
					if (kInputs > 10) *sp10 = NextState(sp10 - hash_table_, s10, bit, updater, 10, kOptP ? opts_[10] : 9);
					if (kInputs > 11) *sp11 = NextState(sp11 - hash_table_, s11, bit, updater, 11, kOptP ? opts_[11] : 9);
					if (kInputs > 12) *sp12 = NextState(sp12 - hash_table_, s12, bit, updater, 12, kOptP ? opts_[12] : 9);
					if (kInputs > 13) *sp13 = NextState(sp13 - hash_table_, s13, bit, updater, 13, kOptP ? opts_[13] : 9);
					if (kInputs > 14) *sp14 = NextState(sp14 - hash_table_, s14, bit, updater, 14, kOptP ? opts_[14] : 9);
					if (kInputs > 15) *sp15 = NextState(sp15 - hash_table_, s15, bit, updater, 15, kOptP ? opts_[15] : 9);
				}
				if (kUseLZP) {
					if (kUseLZPSSE) {
						if (kBitType == kBitTypeLZP) {
							sse2_.update(bit);
						} else if (kBitType == kBitTypeNormalSSE) {
							sse_.update(bit);
						}
					}
				}
				if (sse3) {
					sse3_.update(bit);
				}
				if (kBitType != kBitTypeLZP) {
					match_model_.UpdateBit(bit, true, 7);
				}
				if (kStatistics) ++mixer_skip_[ret];

				if (kDecode) {
					ent.Normalize(stream);
				} else {
					ent.encode(stream, bit, p, kShift);
				}
        if (bits == 1) {
          // ++ctx_count_[cur_ctx];  // Only for last context.
        }
        cur_ctx = ctx_state_.Next(cur_ctx, bit);
        if (kDecode) {
          code = (code << 1) | bit;
        }
        if (--bits == 4) {
          auto nibble = ctx_state_.GetBits(cur_ctx);
          if (!kDecode) {
            dcheck(nibble == c / 16);
          }
          if (kPrefetchMatchModel) {
            match_model_.Fetch(nibble << 4);
          }
        }
			} while (bits != 0);
			return kDecode ? code : c;
		}

    uint64_t opt_op(uint64_t a, uint64_t b) const {
      if (opt_var_ & 1) {
        a ^= b >> (opt_var_ / 2);
      } else {
        a += b >> (opt_var_ / 2);
      }
      return a;
    }

    uint32_t hashify(uint64_t h) const {
      if (false) {
        h += h >> 29;
        h += h >> 1;
        h += h >> 2;
      } else {
        // 29
        // h ^= h * (24 * opt_var + 45);
        // h ^= (h >> opt_var_);
        h ^= h >> 9;
        // h ^= h * ((1 << 0) - 5 + 96);
        h ^= h * (1 + 2 * 174 + 34 * 191 + 94);
        h += h >> 13;
      }
      return h;
    }

    void SetMixerUpdateRates(size_t max_stem, size_t base_stem) {
      for (size_t i = 0; i < kMaxLearn; ++i) {
        mixer_update_rate_[i] = base_stem + max_stem / (3 + i);
      }
    }

    ALWAYS_INLINE void GetHashes(uint32_t& h, const CMProfile& cur, size_t* ctx_ptr, ModelType* enabled) {
      const size_t
        p0 = static_cast<uint8_t>(last_bytes_ >> 0),
        p1 = static_cast<uint8_t>(last_bytes_ >> 8),
        p2 = static_cast<uint8_t>(last_bytes_ >> 16),
        p3 = static_cast<uint8_t>(last_bytes_ >> 24);

      if (cur.ModelEnabled(kModelOrder0, enabled)) {
        *(ctx_ptr++) = o0pos;
      }
      if (cur.ModelEnabled(kModelSpecialChar, enabled)) {
        *(ctx_ptr++) = HashLookup(special_char_model_.GetHash(), true);
      }
      if (cur.ModelEnabled(kModelOrder1, enabled)) {
        *(ctx_ptr++) = o1pos + p0 * o0size;
      }
      if (cur.ModelEnabled(kModelSparse2, enabled)) {
        *(ctx_ptr++) = s2pos + p1 * o0size;
      }
      if (cur.ModelEnabled(kModelSparse3, enabled)) {
        *(ctx_ptr++) = s3pos + p2 * o0size;
      }
      if (cur.ModelEnabled(kModelSparse4, enabled)) {
        *(ctx_ptr++) = s4pos + p3 * o0size;
      }
      if (cur.ModelEnabled(kModelSparse23, enabled)) {
        *(ctx_ptr++) = HashLookup(HashFunc(p2, HashFunc(p1, 0x37220B98)), false); // Order 23
      }
      if (cur.ModelEnabled(kModelSparse34, enabled)) {
        *(ctx_ptr++) = HashLookup(HashFunc(p3, HashFunc(p2, 0x651A833E)), false); // Order 34
      }
      if (cur.ModelEnabled(kModelOrder2, enabled)) {
        *(ctx_ptr++) = o2pos + (last_bytes_ & 0xFFFF) * o0size;
      }
      uint32_t order = 3;
      for (; order <= cur.MaxOrder(); ++order) {
        h = HashFunc(buffer_[buffer_.Pos() - order], h);
        if (cur.ModelEnabled(static_cast<ModelType>(kModelOrder0 + order), enabled)) {
          *(ctx_ptr++) = HashLookup(h, true);
        }
      }
      if (cur.ModelEnabled(kModelWord1, enabled)) {
        *(ctx_ptr++) = HashLookup(word_model_.getMixedHash() + 99912312, false); // Already prefetched.
      }
      if (cur.ModelEnabled(kModelWord2, enabled)) {
        *(ctx_ptr++) = HashLookup(word_model_.getPrevHash() + 111992, false);
      }
      if (cur.ModelEnabled(kModelWord12, enabled)) {
        *(ctx_ptr++) = HashLookup(word_model_.get01Hash() + 5111321, false); // Already prefetched.
      }
      if (cur.ModelEnabled(kModelInterval, enabled)) {
        uint64_t hash = interval_model_ & interval_mask_;
        // hash = hash
        const uint32_t interval_add = 7 * 0x97654321;
        *(ctx_ptr++) = HashLookup(IntervalHash(hash) + interval_add, true);
      }
      if (cur.ModelEnabled(kModelInterval2, enabled)) {
        *(ctx_ptr++) = HashLookup(hashify(interval_model2_ & interval2_mask_) + (22 * 123456781 + 1), true);
      }
      if (cur.ModelEnabled(kModelBracket, enabled)) {
        auto hash = bracket_.GetHash();
        *(ctx_ptr++) = HashLookup(hashify(hash + 82123123 * 9) + 0x20019412, false);
      }
    }

    // Optimal leaf algorithm.
    uint64_t SolveOptimalLeaves(const uint64_t* cost) {
      // DP array [node][remain] = max leaf values
      static const size_t kCount = 1 << 16;
      int64_t total[kCount];
      std::fill_n(total, kCount, -1);
      return DPOptimalLeaves(cost, total, 0, 64);
    }

    int NextNibbleLeaf(int node, size_t next) {
      // 0
      // 1(1) 2(10)
      // 3(11) 4 (100) 5 (101) 6 (110)
      // 7-14
      // 15-30
      size_t first_nibble = 0;
      size_t second_nibble = 0;
      if (node < 15) {
        node = node * 2 + next + 1;
        if (node < 15) {
          return node;
        }
        first_nibble = (node + 1) ^ 16;
        second_nibble = 0;
      } else {
        first_nibble = node / 15 - 1;
        second_nibble = node % 15;
        second_nibble = second_nibble * 2 + next + 1;
        if (second_nibble >= 15) {
          return 256 + first_nibble * 16 + ((1 + second_nibble) ^ 16);
        }
      }
      return (first_nibble + 1) * 15 + second_nibble;
    }

    uint64_t OptimalByteStates(const int64_t* cost, int64_t* total, size_t node, size_t remain) {
      auto& slot = total[256 * node + remain];
      if (slot != -1) {
        return slot;
      }
      if (remain == 0) {
        return 0;
      }
      int64_t base_cost = cost[node];
      if (base_cost == -1) {
        base_cost = 0;
      } else {
        // -1 means we don't need to add.
        --remain;
      }
      // Try all combinations left and right.
      const auto next_a = node * 2 + 1;
      const auto next_b = node * 2 + 2;
      for (size_t i = 0; i <= remain; ++i) {
        int64_t cur = base_cost;
        if (next_a < 255) {
          cur += OptimalByteStates(cost, total, next_a, i);
        }
        if (next_b < 255) {
          cur += OptimalByteStates(cost, total, next_b, remain - i);
        }
        slot = std::max(slot, cur);
      }
      return slot;
    }

    template <const bool decode, typename TStream>
    size_t processByte(TStream& stream, uint32_t c = 0) {
      size_t base_contexts[kInputs] = {};
      auto* ctx_ptr = base_contexts;

      const size_t bpos = buffer_.Pos();
      const size_t blast = bpos - 1; // Last seen char
      const size_t
        p0 = static_cast<uint8_t>(last_bytes_ >> 0),
        p1 = static_cast<uint8_t>(last_bytes_ >> 8),
        p2 = static_cast<uint8_t>(last_bytes_ >> 16),
        p3 = static_cast<uint8_t>(last_bytes_ >> 24);

      size_t expected_char = 0;
      size_t mm_len = 0;
      const size_t mm_order = cur_profile_.MatchModelOrder();
      if (mm_order != 0) {
        match_model_.update(buffer_);
        if (mm_len = match_model_.getLength()) {
          miss_len_ = 0;
          match_model_.setCtx(interval_model_ & 0xFF);
          match_model_.updateCurMdl();
          expected_char = match_model_.getExpectedChar(buffer_);
          uint32_t expected_bits = use_huffman ? huff.getCode(expected_char).length : 8;
          size_t expected_code = use_huffman ? huff.getCode(expected_char).value : expected_char;
          match_model_.updateExpectedCode(expected_code, expected_bits);
        }
      }

      uint32_t h = HashFunc((last_bytes_ & 0xFFFF) * 3, 0x4ec457c1 * 19);
      if (mm_len == 0) {
        ++miss_len_;
        if (kStatistics) {
          ++other_count_;
          ++miss_count_[std::min(kMaxMiss - 1, miss_len_ / 32)];
        }
        if (miss_len_ >= cur_profile_.MissFastPath()) {
          if (kStatistics) ++fast_bytes_;

          uint32_t mm_hash = h;
          for (size_t order = 3; order <= mm_order; ++order) {
            mm_hash = HashFunc(buffer_[bpos - order], mm_hash);
          }
          match_model_.setHash(mm_hash);

          if (false) {
            if (decode) {
              c = ent.DecodeDirectBits(stream, 8);
            } else {
              ent.EncodeBits(stream, c, 8);
            }
          } else {
            auto* s0 = &hash_table_[o2pos + (last_bytes_ & 0xFFFF) * o0size];
            auto* s1 = &hash_table_[o1pos + p0 * o0size];
            auto* s2 = &hash_table_[o0pos];
            size_t ctx = 1;
            uint32_t ch = c << 24;
            bool second_nibble = false;
            size_t base_ctx = 0;
            for (;;) {
              auto* st0 = s0 + ctx;
              auto* st1 = s1 + ctx;
              auto* st2 = s2 + ctx;
              uint32_t idx0 = (fast_probs_[*st0] + 2048) >> (4 + 4);
              uint32_t idx1 = (fast_probs_[*st1] + 2048) >> (4 + 4);
              uint32_t idx2 = (fast_probs_[*st2] + 2048) >> (4 + 4);
              size_t cur = idx0;
              cur = (cur << 4) | idx1;
              cur = (cur << 4) | idx2;
              // if (opt_var_ == 0) cur = (cur << 8) | (base_ctx + ctx);
              // else if (opt_var_ == 1) cur = (cur << 8) | idx2;
              auto* pr = &fast_mix_[cur];
              auto p = pr->getP();
              p += p == 0;
              p -= p == kMaxValue;
              size_t bit;
              if (decode) {
                bit = ent.getDecodedBit(p, kShift);
                ent.Normalize(stream);
              } else {
                bit = ch >> 31;
                ent.encode(stream, bit, p, kShift);
                ch <<= 1;
              }
              pr->update(bit, 10);
              *st0 = state_trans_[*st0][bit];
              *st1 = state_trans_[*st1][bit];
              *st2 = state_trans_[*st2][bit];
              ctx += ctx + bit;
              if (ctx & 0x10) {
                if (second_nibble) {
                  break;
                }
                base_ctx = 15 + (ctx ^ 0x10) * 15;
                s0 += base_ctx;
                s1 += base_ctx;
                s2 += base_ctx;
                ctx = 1;
                second_nibble = true;
              }
            }
            if (decode) {
              c = ctx & 0xFF;
            }
          }
          return c;
        }
      }

      CMProfile cur;
      if (mm_len != 0) {
        cur = cur_match_profile_;
        prob_ctx_add_ = kProbCtxPer;
      } else {
        cur = cur_profile_;
        prob_ctx_add_ = 0;
      }
      GetHashes(h, cur, ctx_ptr, nullptr);
      match_model_.setHash(h);
      dcheck(ctx_ptr - base_contexts <= kInputs + 1);
      sse_ctx_ = 0;

      uint64_t cur_pos = kStatistics ? stream.tell() : 0;

      CalcMixerBase();
      if (mm_len > 0) {
        if (kStatistics) {
          if (!decode) {
            ++(expected_char == c ? match_count_ : non_match_count_);
          }
        }
        if (mm_len >= cur_profile_.MinLZPLen()) {
          size_t extra_len = mm_len - match_model_.getMinMatch();
          dcheck(mm_len >= match_model_.getMinMatch());
          size_t bit = decode ? 0 : expected_char == c;
          sse_ctx_ = 256 * (1 + expected_char);
          bit = ProcessBits<decode, kBitTypeLZP, 1u>(stream, bit, base_contexts, expected_char ^ 256);
          // CalcMixerBase(false);
          if (kStatistics) {
            const uint64_t after_pos = kStatistics ? stream.tell() : 0;
            (bit ? lzp_bit_match_bytes_ : lzp_bit_miss_bytes_) += after_pos - cur_pos;
            cur_pos = after_pos;
            ++(bit ? match_hits_ : match_miss_)[mm_len + cur_profile_.MatchModelOrder() - 4];
          }
          if (bit) {
            return expected_char;
          }
        }
      }
      if (false) {
        match_model_.resetMatch();
        return c;
      }
      // Non match, do normal encoding.
      size_t n = (sse_ctx_ != 0) ?
        ProcessBits<decode, kBitTypeNormalSSE, kBitsPerByte>(stream, c, base_contexts, 0) :
				ProcessBits<decode, kBitTypeNormal, kBitsPerByte>(stream, c, base_contexts, 0);
      if (decode) {
				c = n;
      }
      if (kStatistics) {
        (sse_ctx_ != 0 ? lzp_miss_bytes_ : normal_bytes_) += stream.tell() - cur_pos;
      }

      return c;
    }

    static DataProfile profileForDetectorProfile(Detector::Profile profile) {
      switch (profile) {
      case Detector::kProfileText: return kProfileText;
      case Detector::kProfileSimple: return kProfileSimple;
      }
      return kProfileBinary;
    }

    void UpdateLearnRates() {
      size_t base_ctx[kInputs] = {};
      ModelType enabled[kInputs] = {};
      ModelType match_enabled[kInputs] = {};
      uint8_t* learn = (current_interval_map_ == text_interval_map_) ? mixer_text_learn_ : mixer_binary_learn_;
      uint32_t h = 0;
      GetHashes(h, cur_profile_, base_ctx, enabled);
      GetHashes(h, cur_match_profile_, base_ctx, match_enabled);
      for (size_t i = 0; i < kInputs; ++i) {
        for (size_t j = 0; j < 256; ++j) {
          probs_[i].SetLearn(j, learn[static_cast<size_t>(enabled[i])]);
          probs_[i + kProbCtxPer].SetLearn(j, learn[static_cast<size_t>(match_enabled[i])]);
        }
      }
    }

    void SetDataProfile(DataProfile new_profile) {
      if (!force_profile_) {
        data_profile_ = new_profile;
      }
      interval_model_ = 0;
      small_interval_model_ = 0;
      word_model_.reset();
      current_interval_map_ = binary_interval_map_;
      current_interval_map2_ = binary_interval_map_;
      current_small_interval_map_ = binary_small_interval_map_;
      interval_mask_ = (static_cast<uint64_t>(1) << static_cast<uint64_t>(32)) - 1;
      interval2_mask_ = (static_cast<uint64_t>(1) << static_cast<uint64_t>(4 * 8)) - 1;
      const size_t mixer_n_ctx = mixers_[0].Size() / 256;
      interval_mixer_mask_ = (mixer_n_ctx / 4) - 1;
      uint8_t* reorder = text_reorder_;
      SetMixerUpdateRates(31 * 100, 60);
      switch (data_profile_) {
      case kProfileSimple:
        cur_match_profile_ = cur_profile_ = simple_profile_;
        cur_profile_ = text_profile_;
        break;
      case kProfileText:
        interval_mixer_mask_ = mixer_n_ctx / 2 - 1;
        cur_profile_ = text_profile_;
        cur_match_profile_ = text_match_profile_;
        current_interval_map_ = text_interval_map_;
        current_interval_map2_ = text_interval_map2_;
        current_small_interval_map_ = text_small_interval_map_;
        interval_mask_ = (static_cast<uint64_t>(1) << static_cast<uint64_t>(49)) - 1;
        SetMixerUpdateRates(25 * 100, 31);
        break;
      default:  // Binary.
        cur_profile_ = binary_profile_;
        cur_match_profile_ = binary_match_profile_;
        reorder = binary_reorder_;
        break;
      }
      reorder_.Copy(reorder);
      UpdateLearnRates();
    }

    void update(uint32_t c) {
      if (cur_profile_.ModelEnabled(kModelWord1) ||
        cur_profile_.ModelEnabled(kModelWord2) ||
        cur_profile_.ModelEnabled(kModelWord12) ||
        true) {
        word_model_.update(c);
        if (kPrefetchWordModel && word_model_.getLength() > 2) {
          HashLookup(word_model_.getHash(), true);
        }
        if (kPrefetchWordModel && cur_profile_.ModelEnabled(kModelWord12)) {
          HashLookup(word_model_.get01Hash(), true);
        }
      }
      buffer_.Push(c);
      interval_model_ = (interval_model_ << 4) | current_interval_map_[c];
      interval_model2_ = (interval_model2_ << 4) | current_interval_map2_[c];
      small_interval_model_ = (small_interval_model_ * 8) + current_small_interval_map_[c];
      last_bytes_ = (last_bytes_ << 8) | static_cast<uint8_t>(c);
      bracket_.Update(c);
      special_char_model_.Update(c);
    }

    virtual void compress(Stream* in_stream, Stream* out_stream, uint64_t max_count);
    virtual void decompress(Stream* in_stream, Stream* out_stream, uint64_t max_count);
  };

}

std::ostream& operator << (std::ostream& sout, const cm::CMProfile& pattern);

#endif
