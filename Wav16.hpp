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

#ifndef _WAV16_HPP_
#define _WAV16_HPP_

#include <cstdlib>
#include <vector>

#include "DivTable.hpp"
#include "Entropy.hpp"
#include "GD.hpp"
#include "Huffman.hpp"
// #include "LD.hpp"
#include "Log.hpp"
#include "MatchModel.hpp"
#include "Memory.hpp"
#include "Mixer.hpp"
#include "Model.hpp"
#include "Range.hpp"
#include "StateMap.hpp"
#include "Util.hpp"
#include "WordModel.hpp"

class Wav16 : public Compressor {
public:
  // SS table
  static const uint32_t kShift = 12;
  static const uint32_t kMaxValue = 1 << kShift;
  typedef fastBitModel<int, kShift, 9, 30> StationaryModel;
  
  static const size_t kSampleShift = 4;
  static const size_t kSamplePr = 16 - kSampleShift;
  static const size_t kSampleCount = 1u << kSamplePr;

  static const size_t kContextBits = 2;
  static const size_t kContextMask = (1u << kContextBits) - 1;
  std::vector<StationaryModel> models_;

  // Range encoder
  Range7 ent;

  // Optimization variable.
  uint32_t opt_var = 0;

  // Noise bits which are just direct encoded.
  size_t noise_bits_;
  size_t non_noise_bits_;

  bool setOpt(uint32_t var) {
    opt_var = var;
    return true;
  }

  void init() {
    noise_bits_ = 4;
    non_noise_bits_ = 15 - noise_bits_;
    size_t num_ctx = 2 << (non_noise_bits_ + kContextBits);
    models_.resize(num_ctx);
    for (auto& m : models_) {
      m.init();
    }
  }

  template <typename Subtype>
  ALWAYS_INLINE static bool Detect(uint32_t last_word, Window<Subtype>& window, OffsetBlock* out) {
    if (last_word != MakeWord('R', 'I', 'F', 'F')) {
      return false;
    }
    // This is pretty bad, need a clean way to do it.
    uint32_t fpos = 0;
    const uint32_t chunk_size = window.template Read<kEndianLittle>(fpos, 4); fpos += 4;
    const uint32_t format = window.template Read<kEndianBig>(fpos, 4); fpos += 4;
    // Format subchunk.
    const uint32_t subchunk_id = window.template Read<kEndianBig>(fpos, 4); fpos += 4;
    const uint32_t kWave = MakeWord('W', 'A', 'V', 'E');
    const uint32_t kSubchunk = MakeWord('f', 'm', 't', ' ');
    if (format != kWave || subchunk_id != kSubchunk) {
      return false;
    }
    const uint32_t subchunk_size = window.template Read<kEndianLittle>(fpos, 4); fpos += 4;
    if (subchunk_size != 16 && subchunk_size != 18) {
      return false;
    }
    const uint32_t audio_format = window.template Read<kEndianLittle>(fpos, 2); fpos += 2;
    const uint32_t num_channels = window.template Read<kEndianLittle>(fpos, 2); fpos += 2;
    if (audio_format == 1 && num_channels == 2) {
      fpos += subchunk_size - 6;
      // fpos += 4; // Skip: Sample rate
      // fpos += 4; // Skip: Byte rate
      // fpos += 2; // Skip: Block align
      const uint32_t bits_per_sample = window.template Read<kEndianLittle>(fpos, 2); fpos += 2;
      for (size_t i = 0; i < 5; ++i) {
        const uint32_t subchunk2_id = window.template Read<kEndianBig>(fpos, 4); fpos += 4;
        const uint32_t subchunk2_size = window.template Read<kEndianLittle>(fpos, 4); fpos += 4;
        if (subchunk2_id == 0x64617461) {
          if (subchunk2_size >= chunk_size) {
            break;
          }
          *out = OffsetBlock{fpos, chunk_size};
          return true;
        }
        fpos += subchunk2_size;
        if (fpos >= window.size()) break;
      }
    }
    return false;
  }

  template <const bool kDecode, typename TStream>
  uint32_t processSample(TStream& stream, size_t context, size_t channel, uint32_t c = 0) {
    uint32_t code = 0;
    if (!kDecode) {
      if (true) {
        int high_bit = c >> 15;
        check(high_bit <= 1);
        ent.encodeBit(stream, high_bit);
        if (high_bit) c = ~c;
        c &= (1 << 15) - 1;
        // 63608910
      }
      code = c << (sizeof(uint32_t) * 8 - (15 - 0));
    }
    int ctx = 1;
    context = context * 2 + channel;
    context <<= non_noise_bits_;
    check(context < models_.size());
    for (uint32_t i = 0; i < non_noise_bits_; ++i) {
      auto& m = models_[context + ctx];
      int p = m.getP();
      p += p == 0;
      p -= p == kMaxValue;
      uint32_t bit;
      if (kDecode) {
        bit = ent.getDecodedBit(p, kShift);
      } else {
        bit = code >> (sizeof(uint32_t) * 8 - 1);
        code <<= 1;
        ent.encode(stream, bit, p, kShift);
      }
      m.update(bit, 6);
      ctx = ctx * 2 + bit;
      // Encode the bit / decode at the last second.
      if (kDecode) {
        ent.Normalize(stream);
      }
    }

    // Decode noisy bits (direct).
    for (size_t i = 0; i < noise_bits_; ++i) {
      if (kDecode) {
        ctx += ctx + ent.decodeBit(stream);
      } else {
        ent.encodeBit(stream, code >> 31); code <<= 1;
      }
    }

    return ctx ^ (1u << 16);
  }

  template <typename SOut>
  void CompressBlock(SOut& sout, int16_t* samples, size_t num_samples, float* last_w, size_t iterations = 20) {
    for (size_t j = 0; j < 1; ++j) for (size_t i = num_samples - 1; i >= 2; --i) samples[i] -= samples[i - 2];
    std::vector<float> vars;
    std::vector<float> actual;
    static const size_t kNumSamples = 4;
    static const size_t kNumOrder = 1;
    static const size_t kNumSkew = 0;
    const size_t num_grad = 8; // kNumSkew + kNumSamples * kNumOrder;
    // Order 3: 9.09
    for (size_t j = 0; j < num_samples; ++j) {
      actual.push_back(samples[j] / 32767.0);
    }
    std::vector<float> coeff(num_grad);
    // ForwardLinearPrediction(coeff, actual);

    bool use_gd = false; // opt_var != 0;
    LinearPredictor<float, float, LogPredictor> p(num_grad);
    if (use_gd) {
      for (size_t j = 0; j < num_samples; ++j) {
        for (size_t i = 1; i <= kNumOrder; ++i) {
          for (size_t k = 1; k <= kNumSamples; ++k) {
            if (j >= k) {
              vars.push_back(pow(actual[j - k], i));
            } else {
              vars.push_back(0.0);
            }
          }
        }
        if (kNumSkew) vars.push_back(0.0f);
      }
      // for (size_t i = 0; i < num_grad; ++i) p.SetWeight(i, last_w[i]);
      for (size_t i = 0; i < num_grad; ++i) p.SetWeight(i, -coeff[i]);
    } else {
      iterations = 0;
    }

    float last_cost = 0.0;
    // float learn = 0.13;
    float start_learn = 0.15;
    float end_learn = 0.05;
    int bad_count = 0;
    float cost;
    const bool kVerbose = false;
    std::vector<float> last_delta(num_grad);
    auto start = clock();
    for (size_t iter = 0; iter < iterations; ++iter) {
      if (kVerbose || iter == iterations - 1) {
        cost = p.AverageCost(&vars[0], &actual[0], num_samples);
      }
      std::vector<float> delta(num_grad);
      std::vector<float> total_delta(num_grad);
      p.UpdateAll(&vars[0], &actual[0], &delta[0], num_samples);
      for (size_t i = 0; i < num_grad; ++i) delta[i] += last_delta[i] * 0.65;
      // for (size_t i = 0; i < kNumGrad; ++i) total_delta[i] = delta[i] + last_delta[i] * 0.35;
      auto cur_learn = start_learn + (end_learn - start_learn) * float(iter) / float(iterations - 1);
      p.UpdateWeights(&delta[0], cur_learn);
      last_cost = cost;
      if (kVerbose) {
        std::cerr << iter << " " << cost << " learn=" << cur_learn << " " << p.DumpWeights() << std::endl;
      }
      for (size_t i = 0; i < num_grad; ++i) last_delta[i] = delta[i];
    }
    // std::cout << "PREDICTOR " << p.DumpWeights() << std::endl;
    // for (float f : coeff) std::cout << -f << " "; std::cout << std::endl;
    for (size_t i = 0; i < num_grad; ++i) last_w[i] = p.GetWeight(i);
    // std::cerr << " cost=" << cost << " fvar=" << fvar << " learn=" << learn << " " << p.DumpWeights() << " " << clockToSeconds(clock() - start) << "s" << std::endl;
    // Encode block.
    static const size_t kLastMask = 7;
    int16_t last[kLastMask + 1] = {};
    size_t last_pos = 0;
    for (size_t i = 0; i < num_samples; ++i) {
      const uint16_t a = samples[i];
      float fpred = 0.0f;
      for (size_t j = 0; j < num_grad; ++j) {
        if (use_gd) fpred += last[(last_pos - 1 - j) & kLastMask] * p.GetWeight(j);
        else fpred += last[(last_pos - 1 - j) & kLastMask] * -coeff[j];
      }
      const int kExtraLimit = 100 * 0;
      fpred = Clamp(fpred, -32768 + kExtraLimit, 32767 - kExtraLimit);
      uint16_t pred = fpred;
      uint16_t error = pred - a;
      processSample<false>(sout, 0, i & 1, static_cast<uint16_t>(error));
      last[last_pos++ & kLastMask] = a;
    }
  }

  template <typename SOut>
  void CompressBlockDBL(SOut& sout, int16_t* samples, size_t num_samples) {
    for (size_t i = num_samples - 1; i > 2; --i) samples[i] -= samples[i - 2];
    for (size_t i = num_samples - 1; i > 2; --i) samples[i] -= samples[i - 2];
    for (size_t i = 0; i < num_samples; ++i) {
      processSample<false>(sout, 0, i & 1, static_cast<uint16_t>(samples[i]));
    }
  }

  virtual void compress(Stream* in_stream, Stream* out_stream, uint64_t max_count) {
    BufferedStreamReader<4 * KB> sin(in_stream);
    BufferedStreamWriter<4 * KB> sout(out_stream);
    assert(in_stream != nullptr);
    assert(out_stream != nullptr);
    init();
    ent = Range7();

    // size_t max_block_size = 1 * MB;
    size_t max_block_size = 64 << 6;
    std::vector<int16_t> block(max_block_size);
    size_t block_size = 0;
    size_t block_idx = 0;
    float last_w[64] = {};
    for (size_t i = 0; i < max_count; i += 2) {
      int c1 = sin.get(), c2 = sin.get();
      if (sin.done()) break;
      int16_t a = c1 + c2 * 256;
      block[block_size++] = a;
      if (block_size >= max_block_size) {
        CompressBlock(sout, &block[0], block_size, last_w, (block_idx == 0) ? (20 << 5) : (1 << 7));
        if (false && block_idx == 25) {
          std::ofstream of("wavform.txt");
          std::vector<float> vals;
          for (size_t i = 0; i < block_size; ++i) vals.push_back(block[i] / 32767.0f);
          for (size_t j = 0; j < 2; ++j) {
            std::vector<float> d1, d2, d3;
            float total_d1 = 0.0f, total_d2 = 0.0f, total_d3 = 0.0f;
            
            // for (size_t i = 2; i < vals.size(); ++i) d1.push_back(vals[i] - (2.0f * vals[i - 2] - vals[i]));
            for (size_t i = j; i < vals.size(); i += 2) d1.push_back(vals[i] - vals[i - 2]);
            // for (size_t i = 2; i < d1.size(); ++i) d2.push_back(d1[i] - (2.0f * d1[i - 2] - d1[i]));
            for (size_t i = 1; i < d1.size(); ++i) d2.push_back(d1[i] - d1[i - 1]);
            // for (size_t i = 1; i < d2.size(); ++i) d3.push_back(d2[i] - d2[i - 1]);
            for (size_t i = 4 + j; i < vals.size(); i += 2) d3.push_back(vals[i] - (vals[i - 1] * last_w[0] + vals[i - 2] * last_w[1] + vals[i - 3] * last_w[2] + vals[i - 4] * last_w[3]));
            for (size_t i = 0; i < d3.size(); ++i) {
              total_d1 += log2(1.0 + std::abs(d1[i] * 32767.0));
              total_d2 += log2(1.0 + std::abs(d2[i] * 32767.0));
              total_d3 += log2(1.0 + std::abs(d3[i] * 32767.0));
              of << j << "," << vals[i] << "," << d1[i] << "," << d2[i] << std::endl;
            }
            of << total_d1 / float(d1.size()) << " " << total_d2 / float(d2.size()) << " " << total_d3 / float(d3.size()) << std::endl;
          }
          for (size_t i = 0; i < 4; ++i) of << last_w[i] << ","; of << std::endl;
        }
        block_size = 0;
        ++block_idx;
      }
    }
    if (block_size > 0) {
      CompressBlock(sout, &block[0], block_size, last_w, 20); block_size = 0;
    }
    ent.flush(sout);
    sout.flush();
  }

  virtual void decompress(Stream* in_stream, Stream* out_stream, uint64_t max_count) {
    BufferedStreamReader<4 * KB> sin(in_stream);
    BufferedStreamWriter<4 * KB> sout(out_stream);
    auto start = in_stream->tell();
    init();
    ent.initDecoder(sin);
    uint16_t last_a = 0, last_b = 0;
    uint16_t last_a2 = 0, last_b2 = 0;
    uint16_t last_a3 = 0, last_b3 = 0;
    while (max_count > 0) {
      uint16_t pred_a = 2 * last_a - last_a2;
      uint16_t pred_b = 2 * last_b - last_b2;
      uint16_t a = pred_a + processSample<true>(sin, 0, 0);
      uint16_t b = pred_b + processSample<true>(sin, 0, 1);
      if (max_count > 0) { --max_count; sout.put(a & 0xFF); }
      if (max_count > 0) { --max_count; sout.put(a >> 8); }
      if (max_count > 0) { --max_count; sout.put(b & 0xFF); }
      if (max_count > 0) { --max_count; sout.put(b >> 8); }
      last_a3 = last_a2;
      last_b3 = last_b2;
      last_a2 = last_a;
      last_b2 = last_b;
      last_a = a;
      last_b = b;
    }
    sout.flush();
    size_t remain = sin.remain();
    if (remain > 0) {
      // Go back all the characters we didn't actually read.
      auto target = in_stream->tell() - remain;
      in_stream->seek(target);
    }
  }
};


#endif
