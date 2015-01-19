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

#ifndef ARCHIVE_HPP_
#define ARCHIVE_HPP_

#include <thread>

#include "CM.hpp"
#include "Compressor.hpp"
#include "File.hpp"

// File headers are stored in a list of blocks spread out through data.
class Archive {
public:
	// Header of the actual archive file.
	class FHeader {
	public:
		FHeader();
		virtual ~FHeader();
		uint32_t getVersion() const;
		bool isValid();
		void write(File* file);
		void read(File* file);

	private:
		static const uint32_t header_size = 4;
		char magic[header_size];
		uint32_t version;
		uint64_t free_list_;

		static const char* getReferenceHeader();

		friend class Archive;
	};

	// Archive consists of a list of these.
	class FBlockHeader {
		static const uint32_t kTypeBits = 8;
		static const uint64_t kTypeMask = (1 << kTypeBits) - 1;
	public:
		enum Type { 
			kTypeFileList,  // Compressed file names.
		};
		Type getType() const;
		void setType(Type type);
		uint64_t getLength() const;
		void setLength(uint64_t next_block);

	private:
		uint64_t data_;
		void setData(Type type, uint64_t length);
	};

	class BlockHeader : public FileMirror {
	public:
		BlockHeader(Archive* archive);
		virtual ~BlockHeader();
		FBlockHeader& getHeader();
		const FBlockHeader& getHeader() const;
		void write();
		void read();
		void setDataOffset(uint64_t offset);
		uint64_t getDataOffset() const;
		Archive* getArchive();

	private:
		Archive* archive_;
		FBlockHeader header_;
		uint64_t data_offset_;
	};

	class FileListBlock {
	public:
		FileListBlock(BlockHeader* block_header) {}
		void addFilePath(const FilePath& path) {}

	private:
		BlockHeader* block_header_;
		std::vector<std::string> files_;
	};

	class ScopedBlockHeader {
	public:
		explicit ScopedBlockHeader(FBlockHeader::Type type, Archive* archive);
		~ScopedBlockHeader();
		BlockHeader* getHeader();

	private:
		BlockHeader* header_;
		uint64_t data_start_;
	};

	class FListHeader {
	public:
		enum BlockType {
			kBlockTypeFile,
			kBlockTypeSegment,
		};
		FListHeader(BlockType type, uint64_t count);
		void setNextBlock(uint64_t next_block);

	private:	
		BlockType type_;
		uint64_t count_;
		uint64_t next_block_;
	};

	// File header is part of a file block.
	class FFileHeader {
	public:
		FFileHeader();
		FFileHeader(const FilePath& file_path);
		const std::string& getName() const;
		int getAttributes();

	public:
		// Filename
		std::string name_;
		// Attributes.
		int attributes_;
	};

	class FSegment {
	public:
		FSegment();
		uint64_t getOffset() const;
		void setOffset(uint64_t offset);
		uint64_t getLength() const;
		void setLength(uint64_t length);
		uint32_t getId() const;
		void setId(uint32_t id);
		void write(uint64_t pos, File* file);
		void read(uint64_t pos, File* file);

	private:
		// File ID (which file the segment belongs to).
		// Note: support at most 4B files.
		uint32_t id_;
		// Offset into the file. 
		uint64_t offset_;
		// Length of the segment.
		uint64_t length_;
	};

	class FileSegment {
	public:
		FSegment& getFSegment();
		void setName(const std::string& name);
		const std::string& getName() const;
		typedef std::vector<FileSegment> Vector;

	private:
		std::string name_;
		FSegment fsegment_;
	};

	// A solidly compressed block.
	class SolidBlock {
	public:
		SolidBlock() 
			: offset(0)
			, in_file(nullptr)
			, compressor(nullptr) {
		}

		class Header {
		public:
			// Compression algorithm.
			byte algorithm;
			// Memory level.
			byte mem;
			// Padding.
			byte pad1, pad2;
			// Compressed size of the block (not including header!).
			uint64_t csize;
		};

		Header& getHeader() {
			return header;
		}

	private:
		Header header;
		uint64_t offset;
		ReadStream* in_file;
		Compressor* compressor;
	};

	class FileSegmentReadStream : public ReadStream {
	public:
		FileSegmentReadStream(Archive* archive, FileSegment::Vector* block)
			: archive_(archive), block_(block), file_index_(0), remain_(0), offset_(0), cached_file_(nullptr) {
		}

		void closeFile() {
			if (cached_file_ != nullptr) {
				archive_->getFileManager().close_file(cached_file_);
				cached_file_ = nullptr;
			}
		}

		bool openNextFile() {
			if (file_index_ >= block_->size()) {
				return false;
			}
			return openFile(file_index_++);
		}

		bool openFile(uint32_t index) {
			assert(block_ != nullptr);
			auto& file_manager = archive_->getFileManager();
			// Close the currently opened file.
			closeFile();
			// Open the next file.
			cached_file_ = file_manager.open(block_->at(index).getName(), std::ios_base::in | std::ios_base::binary);
			if (cached_file_ == nullptr) {
				return false;
			}
			File* file = cached_file_->getFile();
			file_read_stream_.setFile(file);
			remain_ = file->length();
			offset_ = 0;
			return true;
		}

		virtual int get() {
			while (!remain_) {
				if (!openNextFile()) {
					// No more files.
					return EOF;
				}
			}
			--remain_;
			offset_++;
			return file_read_stream_.get();
		}

		virtual size_t read(byte* buf, size_t n) {
			size_t total_read = 0;
			while (n) {
				while (!remain_) {
					if (!openNextFile()) {
						goto END_OF_FILE; // No more files.
					}
				}
				size_t read_count = file_read_stream_.read(buf, std::min(static_cast<size_t>(remain_), n));
				if (!read_count) {
					break;
				}
				buf += read_count;
				n -= read_count;
				remain_ -= read_count;
				total_read += read_count;
				offset_ += read_count;
			}
			END_OF_FILE:
			return total_read;
		}

		virtual uint64_t tell() const {
			return offset_;
		}

		virtual void seek(uint64_t pos) {
			for (file_index_ = 0; file_index_ < block_->size(); ++file_index_) {
				auto& file = block_->at(file_index_);
				auto& fseg = file.getFSegment();
				if (pos < fseg.getLength()) {
					openFile(file_index_);
					assert(cached_file_ != nullptr);
					file_read_stream_.seek(fseg.getOffset() + pos);
					break;
				}
				pos -= fseg.getLength();
			}
			offset_ = pos;
		}

	private:
		Archive* archive_;
		FileSegment::Vector* block_;
		uint32_t file_index_;
		uint64_t remain_;
		uint64_t offset_;
		OffsetFileReadStream file_read_stream_;
		FileManager::CachedFile* cached_file_;
	};

	class Job : CompressionJob {
	public:
		void join() {
			thread_->join();
			delete thread_;
		}
		virtual void updateProgress() {
		}

		virtual bool isDone() const {
			return progress_ >= limit_;
		}

	private:
		uint64_t progress_;
		uint64_t limit_;
		Stream* stream_;
		std::thread* thread_;
	};

public:
	Archive();
	virtual ~Archive();
	File& getFile();
	// Resolve file id.
	uint32_t resolveFilename(const std::string& file_name);
	// Add a new file block.
	void addNewFileBlock(const std::vector<FilePath>& files);
	// Simply way of splitting files.
	static std::vector<FileSegment::Vector> splitFiles(const std::vector<FilePath>& files, uint32_t blocks, uint64_t min_block_size);
	FileManager& getFileManager();
	void open(const FilePath& file_path, bool overwrite, std::ios_base::open_mode mode = 0);
	Job* startCompressionJob();
	// OffsetFileReadStream* compressFiles(uint32_t method, OffsetFileWriteStream* out_stream, FileHeaderBlock& block, uint32_t mem);
	static void compressBlock(OffsetFileWriteStream* out_stream, uint32_t method, ReadStream* stream, uint32_t mem);
	// Compress some files.
	void compressFiles(OffsetFileWriteStream* out_stream, uint32_t method, FileSegment::Vector* block, uint32_t mem);
	void compressFiles(uint32_t method, FileSegment::Vector* block, uint32_t mem);
	void Dump(std::ostream& os);

private:
	// Main file.
	File file_;
	// The header.
	FHeader header_;
	// File manager.
	FileManager file_manager_;
	// Mutex
	std::mutex lock_;
	// New block header.
	std::vector<BlockHeader*> block_headers_;

	// Cached version of the blocks. Requires archive lock.
	BlockHeader* newBlockHeader(FBlockHeader::Type type);
};

#endif
