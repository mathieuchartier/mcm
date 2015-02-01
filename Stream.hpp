#ifndef STREAM_HPP_
#define STREAM_HPP_

#include <cstring>

#include "Util.hpp"

class Stream {
public:
	virtual int get() = 0;
	virtual void put(int c) = 0;
    virtual size_t read(uint8_t* buf, size_t n) {
		size_t count;
		for (count = 0; count < n; ++count) {
			auto c = get();
			if (c == EOF) {
				break;
			}
			buf[count] = c;
		}
		return count;
	}
	virtual void write(const uint8_t* buf, size_t n) {
		for (;n; --n) {
			put(*(buf++));
		}
	}
	virtual uint64_t tell() const {
		unimplementedError(__FUNCTION__);
		return 0;
	}
	virtual void seek(uint64_t pos) {
		unimplementedError(__FUNCTION__);
	}
    virtual ~Stream() {
	}
};

class WriteStream : public Stream {
public:
	virtual int get() {
		unimplementedError(__FUNCTION__);
		return 0;
	}
    virtual void put(int c) = 0;
	virtual ~WriteStream() {}
};

class VoidWriteStream : public WriteStream {
public:
	virtual ~VoidWriteStream() {}
    virtual void write(const byte*, uint32_t) {
	}
	virtual void put(int) {
	}
};

class ReadStream : public Stream {
public:
    virtual int get() = 0;
	virtual void put(int c) {
		unimplementedError(__FUNCTION__);
	}
    virtual ~ReadStream() {}
};

class ReadMemoryStream : public ReadStream {
public:
	ReadMemoryStream(const std::vector<byte>* buffer)
		: buffer_(buffer->data())
		, pos_(buffer->data())
		, limit_(buffer->data() + buffer->size()) {
	}
	ReadMemoryStream(const byte* buffer, const byte* limit) : buffer_(buffer), pos_(buffer), limit_(limit) {
	}
	virtual int get() {
		if (pos_ >= limit_) {
			return EOF;
		}
		return *pos_++;
	}
    virtual size_t read(byte* buf, size_t n) {
		const size_t remain = limit_ - pos_;
		const size_t read_count = std::min(remain, n);
		std::copy(pos_, pos_ + read_count, buf);
		pos_ += read_count;
		return read_count;
	}
	virtual uint64_t tell() const {
		return pos_ - buffer_;
	}

private:
	const byte* const buffer_;
	const byte* pos_;
	const byte* const limit_;
};

class WriteMemoryStream : public WriteStream {
public:
	explicit WriteMemoryStream(byte* buffer) : buffer_(buffer), pos_(buffer) {
	}
	virtual void put(int c) {
		*pos_++ = static_cast<byte>(static_cast<unsigned int>(c));
	}
	virtual void write(const byte* data, uint32_t count) {
		memcpy(pos_, data, count);
		pos_ += count;
	}
	virtual uint64_t tell() const {
		return pos_ - buffer_;
	}

private:
	byte* buffer_;
	byte* pos_;
};

class WriteVectorStream : public WriteStream {
public:
	explicit WriteVectorStream(std::vector<byte>* buffer) : buffer_(buffer) {
	}
	virtual void put(int c) {
		buffer_->push_back(c);
	}
	virtual void write(const byte* data, uint32_t count) {
		buffer_->insert(buffer_->end(), data, data + count);
	}
	virtual uint64_t tell() const {
		return buffer_->size();
	}

private:
	std::vector<byte>* const buffer_;
};

template <typename T>
class OStreamWrapper : public std::ostream {
	class StreamBuf : public std::streambuf {
	public:
	};
public:
};

template <const uint32_t buffer_size>
class BufferedStreamReader {
public:
	Stream* stream;
	size_t buffer_count, buffer_pos;
	byte buffer[buffer_size];

	BufferedStreamReader(Stream* stream) {
		init(stream);
	}

	virtual ~BufferedStreamReader() {
	}

	void init(Stream* new_stream) {
		stream = new_stream;
		buffer_pos = 0;
		buffer_count = 0;
	}

	forceinline int get() {
		if (UNLIKELY(buffer_pos >= buffer_count)) {
			buffer_pos = 0;
			buffer_count = stream->read(buffer, buffer_size);
			if (UNLIKELY(!buffer_count)) {
				return EOF;
			}
		}
		return buffer[buffer_pos++];
	}

	void put(int c) {
		unimplementedError(__FUNCTION__);
	}
};

template <const uint32_t kBufferSize>
class BufferedStreamWriter {
public:
	BufferedStreamWriter(Stream* stream) {
		init(stream);
	}
	virtual ~BufferedStreamWriter() {
		flush();
		assert(ptr_ == buffer_);
	}
	void init(Stream* new_stream) {
		stream_ = new_stream;
		ptr_ = buffer_;
	}
	void flush() {
		stream_->write(buffer_, ptr_ - buffer_);
		ptr_ = buffer_;
	}
	forceinline void put(uint8_t c) {
		if (UNLIKELY(ptr_ >= end())) {
			flush();
		}
		*(ptr_++) = c;
	}
	int get() {
		unimplementedError(__FUNCTION__);
		return 0;
	}

private:
	const uint8_t* end() const { 
		return &buffer_[kBufferSize];
	}
	Stream* stream_;
	uint8_t buffer_[kBufferSize];
	uint8_t* ptr_;
};

template <const bool kLazy = true>
class MemoryBitStream {
	byte* no_alias data_;
	uint32_t buffer_;
	uint32_t bits_;
	static const uint32_t kBitsPerSizeT = sizeof(uint32_t) * kBitsPerByte;
public:
	forceinline MemoryBitStream(byte* data) : data_(data), buffer_(0), bits_(0) {
	}

	byte* getData() {
		return data_;
	}

	forceinline void tryReadByte() {
		if (bits_ <= kBitsPerSizeT - kBitsPerByte) {
			readByte();
		}
	}

	forceinline void readByte() {
		buffer_ = (buffer_ << kBitsPerByte) | *data_++;
		bits_ += kBitsPerByte;
	}

	forceinline uint32_t readBits(uint32_t bits) {
		if (kLazy) {
			while (bits_ < bits) {
				readByte();
			}
		} else {
			// This might be slower
			tryReadByte();
			tryReadByte();
			tryReadByte();
		}
		bits_ -= bits;
		uint32_t ret = buffer_ >> bits_;
		buffer_ -= ret << bits_;
		return ret;
	}

	forceinline void flushByte() {
		bits_ -= kBitsPerByte;
		uint32_t byte = buffer_ >> bits_;
		buffer_ -= byte << bits_;
		*data_++ = byte;
	}

	void flush() {
		while (bits_ > kBitsPerByte) {
			flushByte();
		}
		*data_++ = buffer_;
	}

	forceinline void writeBits(uint32_t data, uint32_t bits) {
		bits_ += bits;
		buffer_  = (buffer_ << bits) | data;
		while (bits_ >= kBitsPerByte) {
			flushByte();
		}
	}
};

#endif
