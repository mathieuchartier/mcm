#ifndef STREAM_HPP_
#define STREAM_HPP_

#include "Util.hpp"

class ReadStream {
public:
    virtual int get() = 0;
    virtual size_t read(byte* buf, size_t n) {
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
    virtual ~ReadStream() {}
};

class WriteStream {
public:
    virtual void put(int c) = 0;
    virtual void write(const byte* buf, size_t n) {
		for (;n; --n) {
			put(*(buf++));
		}
	}
    virtual ~WriteStream() {}
};

template <const size_t buffer_size>
class BufferedStreamReader {
public:
	Closure* buffer_event;
	ReadStream* stream;
	size_t buffer_count, buffer_pos;
	byte buffer[buffer_size];
	
	BufferedStreamReader(ReadStream* stream) {
		init(stream);
	}

	virtual ~BufferedStreamReader() {

	}

	void setEvent(Closure* event) {
		buffer_event = event;
	}

	void init(ReadStream* new_stream) {
		buffer_event = nullptr;
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
			buffer_event->run();
		}
		return buffer[buffer_pos++];
	}
};


template <const size_t buffer_size>
class BufferedStreamWriter {
public:
	Closure* flush_event;
	WriteStream* stream;
	size_t buffer_pos;
	byte buffer[buffer_size];

	BufferedStreamWriter(WriteStream* stream) {
		init(stream);
	}
	
	virtual ~BufferedStreamWriter() {
		assert(!buffer_pos);
	}

	void setEvent(Closure* event) {
		flush_event = event;
	}

	Closure* getEvent() {
		return flush_event;
	}
		
	void init(WriteStream* new_stream) {
		stream = new_stream;
		buffer_pos = 0;
		flush_event = nullptr;
	}

	void flush() {
		stream->write(buffer, buffer_pos);
		buffer_pos = 0;
		if(flush_event != nullptr) {
			flush_event->run();
		}
	}

	forceinline void put(byte c) {
		if (UNLIKELY(buffer_pos > buffer_size)) {
			flush();
		}
		buffer[buffer_pos] = c;
	}
};

#endif
