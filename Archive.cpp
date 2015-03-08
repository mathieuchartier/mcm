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

bool Archive::Header::isSameVersion() {
	return major_version_ == kCurMajorVersion && minor_version_ == kCurMinorVersion;
}

Archive::Algorithm::Algorithm(uint8_t mem_usage, uint8_t algorithm, bool lzp_enabled)
	: mem_usage_(mem_usage), algorithm_(algorithm), lzp_enabled_(lzp_enabled) {
}

Compressor* Archive::Algorithm::createCompressor() {
	switch (static_cast<Compressor::Type>(algorithm_)) {
	case Compressor::kTypeCMTurbo:
		return new CM<kCMTypeTurbo>(mem_usage_, lzp_enabled_);
		//return new TurboCM<6>(mem_usage);
		//return new CMRolz;
	case Compressor::kTypeCMFast:
		return new CM<kCMTypeFast>(mem_usage_, lzp_enabled_);
	case Compressor::kTypeCMMid:
		return new CM<kCMTypeMid>(mem_usage_, lzp_enabled_);
	case Compressor::kTypeCMHigh:
		return new CM<kCMTypeHigh>(mem_usage_, lzp_enabled_);
	case Compressor::kTypeCMMax:
		return new CM<kCMTypeMax>(mem_usage_, lzp_enabled_);
	default:
		return new Store;
	}
	return nullptr;
}

void Archive::Algorithm::read(Stream* stream) {
	mem_usage_ = static_cast<uint8_t>(stream->get());
	algorithm_ = static_cast<uint8_t>(stream->get());
	lzp_enabled_ = stream->get() != 0 ? true : false;
}

void Archive::Algorithm::write(Stream* stream) {
	stream->put(mem_usage_);
	stream->put(algorithm_);
	stream->put(lzp_enabled_);
}

Archive::FBlockHeader::Type Archive::FBlockHeader::getType() const {
	return static_cast<Type>(data_ & kTypeMask);
}

void Archive::FBlockHeader::setType(Type type) {
	setData(type, getLength());
}

uint64_t Archive::FBlockHeader::getLength() const {
	return data_ >> kTypeBits;
}

void Archive::FBlockHeader::setLength(uint64_t length) {
	setData(getType(), length);
}

void Archive::FBlockHeader::setData(Type type, uint64_t next_block) {
	assert((next_block << kTypeBits) >= next_block);
	data_ = static_cast<uint64_t>(type) | (next_block << kTypeBits);
}

Archive::ScopedBlockHeader::ScopedBlockHeader(FBlockHeader::Type type, Archive* archive) {
	header_ = archive->newBlockHeader(type);
	// Make sure we are at the end of the header.
	header_->write();
	// Let the caller write their data.
	data_start_ = archive->getFile().tell();
}

Archive::ScopedBlockHeader::~ScopedBlockHeader() {
	const uint64_t data_end_ = header_->getArchive()->getFile().tell();
	header_->getHeader().setLength(data_end_ - data_start_);
	header_->update();
}

Archive::BlockHeader* Archive::ScopedBlockHeader::getHeader() {
	return header_;
}

Archive::BlockHeader::BlockHeader(Archive* archive) : archive_(archive), data_offset_(0) {	
}

Archive* Archive::BlockHeader::getArchive() {
	return archive_;
}

Archive::BlockHeader::~BlockHeader() {
}

Archive::FBlockHeader& Archive::BlockHeader::getHeader() {
	setDirty(true);
	return header_;
}

const Archive::FBlockHeader& Archive::BlockHeader::getHeader() const {
	return header_;
}

void Archive::BlockHeader::write() {

}

void Archive::BlockHeader::read() {

}

void Archive::BlockHeader::setDataOffset(uint64_t offset) {
	data_offset_ = offset;
}

uint64_t Archive::BlockHeader::getDataOffset() const {
	return data_offset_;
}

Archive::FFileHeader::FFileHeader() : attributes_(0) {
}

Archive::FFileHeader::FFileHeader(const FilePath& file_path)
	: name_(file_path.getName())
	, attributes_(0) {
}

const std::string& Archive::FFileHeader::getName() const {
	return name_;
}

int Archive::FFileHeader::getAttributes() {
	return attributes_;
}

Archive::FSegment::FSegment() : id_(0), offset_(0), length_(0) {
}

uint64_t Archive::FSegment::getOffset() const {
	return offset_;
}

void Archive::FSegment::setOffset(uint64_t offset) {
	offset_ = offset;
}

uint64_t Archive::FSegment::getLength() const {
	return length_;
}

void Archive::FSegment::setLength(uint64_t length) {
	length_ = length;
}

uint32_t Archive::FSegment::getId() const {
	return id_;
}

void Archive::FSegment::setId(uint32_t id) {
	id_ = id;
}

void Archive::FSegment::write(uint64_t pos, File* file) {
	// file->awrite(pos, this, sizeof(*this));
}

void Archive::FSegment::read(uint64_t pos, File* file) {
	// file->aread(pos, this, sizeof(*this));
}

Archive::FSegment& Archive::FileSegment::getFSegment() {
	return fsegment_;
}

void Archive::FileSegment::setName(const std::string& name) {
	name_ = name;
}

const std::string& Archive::FileSegment::getName() const {
	return name_;
}

Archive::Archive() {
}

Archive::~Archive() {
}

File& Archive::getFile() {
	return file_;
}

void Archive::addNewFileBlock(const std::vector<FilePath>& files) {
	ScopedLock mu(lock_);
	ScopedBlockHeader block(FBlockHeader::kTypeFileList, this);
	// File list.
	auto* file_list = new FileListBlock(block.getHeader());
	// Write the file headers.
	for (const auto& file : files) {
		file_list->addFilePath(file);
		std::string name = file.getName();
		// +1 for null char.
		// getFile().write(&name[0], name.length() + 1);
	}
}

FileManager& Archive::getFileManager() {
	return file_manager_;
}

void Archive::open(const FilePath& file_path, bool overwrite, std::ios_base::open_mode mode) {
	file_.open(file_path.getName(), std::ios_base::binary | mode);
	if (!file_.length() || overwrite) {
		// If the file is empty we need to write out the archive headder before we do anything.
		// file_.write(reinterpret_cast<void*>(&header_), sizeof(header_));
	}
}

Archive::Job* Archive::startCompressionJob() {
	return nullptr;
}

/*
void Archive::compressBlock(OffsetFileWriteStream* out_stream, uint32_t method, ReadStream* stream, uint32_t) {
	assert(out_stream != nullptr);
	Compressor* compressor = CompressorFactories::makeCompressor(method);
	assert(compressor != nullptr);
	compressor->compress(stream, out_stream);
#if 0
	FBlock::Header header;
	header.algorithm = method;
	header.csize = 0;
	header.mem = mem;
	header.pad1 = header.pad2 = 0;
	uint64_t pos = out_stream->tell();
	out_stream->write(reinterpret_cast<byte*>(&header), sizeof(header));
	uint64_t start_pos = out_stream->tell();
	compressor->compress(stream, out_stream);
	// Update csize to how many bytes we wrote.
	header.csize = out_stream->tell() - start_pos;
	// Update the block header and rewrite.
	out_stream->seek(pos);
	out_stream->write(reinterpret_cast<byte*>(&header), sizeof(header));
#endif
}
*/

	/*
void Archive::compressFiles(OffsetFileWriteStream* out_stream, uint32_t method, FileSegment::Vector* block, uint32_t mem) {
	// Start by writing out the header.
	FListHeader header(FListHeader::kBlockTypeSegment, static_cast<uint64_t>(block->size()));
	out_stream->write(reinterpret_cast<byte*>(&header), sizeof(header));
	// Write the segments.
	for (auto& file_segment : *block) {
		auto& fseg = file_segment.getFSegment();
		fseg.setId(resolveFilename(file_segment.getName()));
		out_stream->write(reinterpret_cast<byte*>(&fseg), sizeof(fseg));
	}
	// Compress the actual file block.
	FileSegmentReadStream in_stream(this, block);
	compressBlock(out_stream, method, &in_stream, mem);
}

void Archive::compressFiles(uint32_t method, FileSegment::Vector* block, uint32_t mem) {
	ScopedLock mu(lock_);
	OffsetFileWriteStream stream(&getFile());
	compressFiles(&stream, 0, block, 4);
}

std::vector<Archive::FileSegment::Vector> Archive::splitFiles(const std::vector<FilePath>& files, uint32_t blocks, uint64_t min_block_size) {
	assert(blocks > 0);
	std::vector<Archive::FileSegment::Vector> ret;
	uint64_t total_length = 0;
	// File lengths;
	std::vector<uint64_t> lengths;
	// Calculate offsets.
	for (auto& file_path : files) {
		const std::string name = file_path.getName();
		uint64_t length = getFileLength(name);
		lengths.push_back(length);
		total_length += length;
	}
	uint32_t min_total_blocks = static_cast<uint32_t>(total_length / min_block_size + 1);
	blocks = std::min(blocks, min_total_blocks);
	// Split the files into blocks.
	ret.resize(blocks);
	uint32_t file_index = 0;
	uint64_t file_offset = 0;
	uint64_t avg_block_size = (total_length + blocks - 1) / blocks;
	for (Archive::FileSegment::Vector& seg_vec : ret) {
		uint64_t block_size = std::min(avg_block_size, total_length);
		while (block_size > 0) {
			uint64_t file_length = lengths[file_index];
			Archive::FileSegment fseg;
			fseg.setName(files[file_index].getName());
			const uint64_t file_remain = file_length - file_offset;
			uint64_t bytes = std::min(block_size, file_remain);
			fseg.getFSegment().setOffset(file_offset);
			fseg.getFSegment().setLength(bytes);
			file_offset += bytes;
			if (file_offset >= file_length) {
				++file_index;
				file_offset = 0;
			}
			block_size -= bytes;
			seg_vec.push_back(fseg);
		}
	}
	return ret;
}
*/

Archive::FListHeader::FListHeader(BlockType type, uint64_t count)
	: type_(type), count_(count), next_block_(0) {
}

void Archive::FListHeader::setNextBlock(uint64_t next_block) {
	next_block_ = next_block;
}

uint32_t Archive::resolveFilename(const std::string& file_name) {
	return 0; // This needs to get fixed.
}

Archive::BlockHeader* Archive::newBlockHeader(FBlockHeader::Type type) {
	BlockHeader* new_header = new BlockHeader(this);
	new_header->getHeader().setType(type);
	new_header->getHeader().setLength(0);
	// Try to add it at the end.
	file_.seek(0, SEEK_END);
	uint64_t offset = file_.tell();
	// Set offset.
	new_header->setOffset(offset);
	new_header->write();
	new_header->setDataOffset(file_.tell());
	return new_header;
}

void Archive::Dump(std::ostream& os) {
	for (const auto* block : block_headers_) {
		// TODO: Dump.
	}
}
