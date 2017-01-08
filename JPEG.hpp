/*	MCM file compressor

  Copyright (C) 2016, Google Inc.
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

#ifndef _JPEG_HPP_
#define _JPEG_HPP_

#include "Compressor.hpp"
#include "CyclicBuffer.hpp"

class JPEGCompressor : public Compressor {
public:
  // State of parser
  enum {
    SOF0 = 0xC0,
    SOF1,
    SOF2,
    SOF3,
    DHT,
    RST0 = 0xD0,
    SOI = 0xD8,
    EOI,
    SOS,
    DQT,
    DNL,
    DRI,
    APP0=0xE0,
    COM=0xFE,
    FF,
  };

  static bool IsJPEGHeader(const uint8_t* b) {
    return b[0] != 0xFF && b[1] == SOI;
  }
  
  bool TryParseHeader() {
    return false;
  }

  template <typename Subtype>
  ALWAYS_INLINE static bool Detect(uint32_t last_word, Window<Subtype>& window, OffsetBlock* out) {
    if (last_word != MakeWord('R', 'I', 'F', 'F')) {
      return false;
    }
    return false;
  }
};

#endif
