/*	MCM file compressor

	Copyright (C) 2014, Google Inc.
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

#include "Archive.hpp"

#include "X86Binary.hpp"

#include <cstring>

Archive::Header::Header() : major_version_(kCurMajorVersion), minor_version_(kCurMinorVersion) {
	memcpy(magic_, getMagic(), kMagicStringLength);
}

void Archive::Header::read(Stream* stream) {
	stream->read(reinterpret_cast<uint8_t*>(magic_), kMagicStringLength);
	major_version_ = stream->get16();
	minor_version_ = stream->get16();
}

void Archive::Header::write(Stream* stream) {
	stream->write(reinterpret_cast<uint8_t*>(magic_), kMagicStringLength);
	stream->put16(major_version_);
	stream->put16(minor_version_);
}

bool Archive::Header::isArchive() const {
	return memcmp(magic_, getMagic(), kMagicStringLength) == 0;
}

bool Archive::Header::isSameVersion() const {
	return major_version_ == kCurMajorVersion && minor_version_ == kCurMinorVersion;
}

Archive::Algorithm::Algorithm(const CompressionOptions& options, Detector::Profile profile) : profile_(profile) {
	mem_usage_ = options.mem_usage_;
	algorithm_ = Compressor::kTypeStore;
	switch (options.comp_level_) {
	case kCompLevelStore:
		algorithm_ = Compressor::kTypeStore;
		break;
	case kCompLevelTurbo:
		algorithm_ = Compressor::kTypeCMTurbo;
		break;
	case kCompLevelFast:
		algorithm_ = Compressor::kTypeCMFast;
		break;
	case kCompLevelMid:
		algorithm_ = Compressor::kTypeCMMid;
		break;
	case kCompLevelHigh:
		algorithm_ = Compressor::kTypeCMHigh;
		break;
	case kCompLevelMax:
		algorithm_ = Compressor::kTypeCMMax;
		break;
	}
	switch (profile) {
	case Detector::kProfileBinary:
		lzp_enabled_ = true;
		filter_ = kFilterTypeX86;
		break;
	case Detector::kProfileText:
		lzp_enabled_ = true;
		filter_ = kFilterTypeDict;
		break;
	}
	// OVerrrides.
	if (options.lzp_type_ == kLZPTypeEnable) lzp_enabled_ = true;
	else if (options.lzp_type_ == kLZPTypeDisable) lzp_enabled_ = false;
	// Force filter.
	if (options.filter_type_ != kFilterTypeAuto) {
		filter_ = options.filter_type_;
	}
}

Archive::Algorithm::Algorithm(Stream* stream) {
	read(stream);
}

void Archive::init() {
	opt_var_ = 0;
}

Archive::Archive(Stream* stream, const CompressionOptions& options) : stream_(stream), options_(options) {
	init();
	header_.write(stream_);
}

Archive::Archive(Stream* stream) : stream_(stream), opt_var_(0) {
	init();
	header_.read(stream_);
}

Compressor* Archive::Algorithm::createCompressor() {
	switch (algorithm_) {
	case Compressor::kTypeStore:
		return new Store;
	case Compressor::kTypeCMTurbo:
		return new CM<kCMTypeTurbo>(mem_usage_, lzp_enabled_, profile_);
	case Compressor::kTypeCMFast:
		return new CM<kCMTypeFast>(mem_usage_, lzp_enabled_, profile_);
	case Compressor::kTypeCMMid:
		return new CM<kCMTypeMid>(mem_usage_, lzp_enabled_, profile_);
	case Compressor::kTypeCMHigh:
		return new CM<kCMTypeHigh>(mem_usage_, lzp_enabled_, profile_);
	case Compressor::kTypeCMMax:
		return new CM<kCMTypeMax>(mem_usage_, lzp_enabled_, profile_);
	}
	return nullptr;
}

void Archive::Algorithm::read(Stream* stream) {
	mem_usage_ = static_cast<uint8_t>(stream->get());
	algorithm_ = static_cast<Compressor::Type>(stream->get());
	lzp_enabled_ = stream->get() != 0 ? true : false;
	filter_ = static_cast<FilterType>(stream->get());
	profile_ = static_cast<Detector::Profile>(stream->get());
}

void Archive::Algorithm::write(Stream* stream) {
	stream->put(mem_usage_);
	stream->put(algorithm_);
	stream->put(lzp_enabled_);
	stream->put(filter_);
	stream->put(profile_);
}

std::ostream& operator<<(std::ostream& os, CompLevel comp_level) {
	switch (comp_level) {
	case kCompLevelStore: return os << "store";
	case kCompLevelTurbo: return os << "turbo";
	case kCompLevelFast: return os << "fast";
	case kCompLevelMid: return os << "mid";
	case kCompLevelHigh: return os << "high";
	case kCompLevelMax: return os << "max";
	}
	return os << "unknown";
}

Filter* Archive::Algorithm::createFilter(Stream* stream, Analyzer* analyzer) {
	switch (filter_) {
	case kFilterTypeDict:
		if (analyzer) {
			auto& builder = analyzer->getDictBuilder();
			Dict::CodeWordGeneratorFast generator;
			Dict::CodeWordSet code_words;
			generator.generateCodeWords(builder, &code_words);
			auto dict_filter = new Dict::Filter(stream, 0x3, 0x4, 0x6);
			dict_filter->addCodeWords(code_words.getCodeWords(), code_words.num1_, code_words.num2_, code_words.num3_);
			return dict_filter;
		}
		return nullptr;
	case kFilterTypeX86:
		return new X86AdvancedFilter(stream);
	}
	return nullptr;
}

// Analyze and compress.
void Archive::compress(Stream* in) {
	Analyzer analyzer;
	auto start_a = clock();
	std::cout << "Analyzing" << std::endl;
	{
		ProgressThread thr(in, stream_);
		analyzer.analyze(in);
	}
	std::cout << std::endl;
	analyzer.dump();
	std::cout << "Analyzing took " << clockToSeconds(clock() - start_a) << "s" << std::endl << std::endl;

	// Compress blocks.
	uint64_t total_in = 0;
	for (size_t p_idx = 0; p_idx < static_cast<size_t>(Detector::kProfileCount); ++p_idx) {
		auto profile = static_cast<Detector::Profile>(p_idx);
		// Compress each stream type.
		std::vector<FileSegmentStream::FileSegments> segments;
		uint64_t pos = 0;
		FileSegmentStream::FileSegments seg;
		seg.base_offset_ = 0;
		seg.stream_ = in;
		for (const auto& b : analyzer.getBlocks()) {
			const auto len = b.length();
			if (b.profile() == profile) {
				FileSegmentStream::SegmentRange range;
				range.offset_ = pos;
				range.length_ = len;
				seg.ranges_.push_back(range);
			}
			pos += len;
		}
		seg.calculateTotalSize();
		segments.push_back(seg);
		if (seg.total_size_ > 0) {
			auto start_pos = stream_->tell();
			seg.write(stream_);
			std::cout << "Overhead size " << stream_->tell() - start_pos << std::endl;
			auto start = clock();
			auto out_start = stream_->tell();
			Algorithm algo(options_, profile);
			algo.write(stream_);
			FileSegmentStream segstream(&segments, 0u);	
			std::cout << "Compressing " << Detector::profileToString(profile)
				<< " stream size=" << formatNumber(seg.total_size_) << "\t" << std::endl;
			std::unique_ptr<Filter> filter(algo.createFilter(&segstream, &analyzer));
			Stream* in_stream = &segstream;
			if (filter.get() != nullptr) in_stream = filter.get();
			std::unique_ptr<Compressor> comp(algo.createCompressor());
			{
				ProgressThread thr(&segstream, stream_, true, out_start);
				comp->compress(in_stream, stream_);
			}
			total_in += segstream.tell();
			std::cout << std::endl << "Compressed " << formatNumber(seg.total_size_) << " -> " << formatNumber(stream_->tell() - out_start)
				<< " in " << clockToSeconds(clock() - start) << "s" << std::endl << std::endl;
		}
	}
}

// Decompress.
void Archive::decompress(Stream* out) {

}
