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

#include <chrono>
#include <ctime>
#include <fstream>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sstream>
#include <thread>

#include "Archive.hpp"
#include "CM.hpp"
#include "DeltaFilter.hpp"
#include "Dict.hpp"
#include "File.hpp"
#include "Huffman.hpp"
#include "LZ.hpp"
#include "Tests.hpp"
#include "TurboCM.hpp"
#include "X86Binary.hpp"

CompressorFactories* CompressorFactories::instance = nullptr;

//typedef X86BinaryFilter DefaultFilter;
typedef X86AdvancedFilter DefaultFilter;
//typedef FixedDeltaFilter<2, 2> DefaultFilter;
//typedef SimpleDict DefaultFilter;
//typedef IdentityFilter DefaultFilter;

class ArchiveHeader {
public:
	static const size_t kVersion = 82;

	char magic[3]; // MCM
	uint16_t version;
	uint8_t mem_usage;
	uint8_t algorithm;

	ArchiveHeader() {
		magic[0] = 'M';
		magic[1] = 'C';
		magic[2] = 'M';
		version = kVersion;
		mem_usage = 8;
	}

	template <typename TIn>
	void read(TIn& sin) {
		for (auto& c : magic) {
			c = sin.get();
		}
		version = sin.get();
		version = (version << 8) | sin.get();
		mem_usage = (byte)sin.get();
		algorithm = (byte)sin.get();
	}

	template <typename TOut>
	void write(TOut& sout) {
		for (auto& c : magic) {
			sout.put(c);
		}
		sout.put(version >> 8);
		sout.put(version & 0xFF);
		sout.put(mem_usage);
		sout.put(algorithm);
	}

	bool isValid() const {
		// Check magic && version.
		if (magic[0] != 'M' ||
			magic[1] != 'C' ||
			magic[2] != 'M' ||
			version != kVersion) {
			return false;
		}
		return true;
	}

	Compressor* createCompressor() {
		//return new Store;
		switch ((Compressor::Type)algorithm) {
		case Compressor::kTypeCMTurbo:
			return new CM<kCMTypeTurbo>(mem_usage);
		case Compressor::kTypeCMFast:
			return new CM<kCMTypeFast>(mem_usage);
		case Compressor::kTypeCMMid:
			return new CM<kCMTypeMid>(mem_usage);
		case Compressor::kTypeCMHigh:
			return new CM<kCMTypeHigh>(mem_usage);
		case Compressor::kTypeCMMax:
			return new CM<kCMTypeMax>(mem_usage);
		default:
			return new Store;
		}
		return nullptr;
	}
};

class VerifyStream : public WriteStream {
public:
	std::ifstream fin;
	uint32_t differences, total;

	VerifyStream(const std::string& file_name) {
		fin.open(file_name, std::ios_base::in | std::ios_base::binary);
		assert(fin.good());
		init();
	}

	void init() {
		differences = total = 0;
	}

	void put(int c) {
		auto ref = fin.eof() ? 256 : fin.get();
		bool diff = ref != c;
		if (diff) {
			if (differences == 0) {
				std::cerr << "Difference found at byte! " << total << " b1: " << "ref: " << (int)ref << " new: " << (int)c << std::endl;
			}
			++differences;
		}
		++total;
	}

	virtual uint64_t tell() const {
		return total;
	}

	void summary() {
		fin.get();
		if (!fin.eof()) {
			std::cerr << "ERROR: Output truncated at byte " << fin.tellg() << " differences=" << differences << std::endl;
		} else {
			if (differences) {
				std::cerr << "ERROR: differences=" << differences << std::endl;
			} else {
				std::cout << "No differences found!" << std::endl;
			}
		}
	}
};

std::string trimExt(std::string str) {
	std::streamsize start = 0, pos;
	if ((pos = str.find_last_of('\\')) != std::string::npos) {
		start = std::max(start, pos + 1);
	}
	if ((pos = str.find_last_of('/')) != std::string::npos) {
		start = std::max(start, pos + 1);
	}
	return str.substr(static_cast<uint32_t>(start));
}

static void printHeader() {
	std::cout << "mcm file compressor v0." << ArchiveHeader::kVersion << ", by Mathieu Chartier (c)2015 Google Inc" << std::endl;
}

static int usage(const std::string& name) {
	printHeader();
	std::cout
		<< "Caution: Use only for testing!!" << std::endl
		<< "Usage: " << name << " [options] <infile> <outfile>" << std::endl
		<< "Options: -d for decompress" << std::endl
		<< "-1 ... -9 specifies ~32mb ... ~1500mb memory, " << std::endl
		<< "-10 -11 for 3GB, ~5.5GB (only supported on 64 bits)" << std::endl
		<< "modes: -turbo -fast -mid -high -max (default -high) specifies speed" << std::endl
		<< "-test tests the file after compression is done" << std::endl
		// << "-b <mb> specifies block size in MB" << std::endl
		// << "-t <threads> the number of threads to use (decompression requires the same number of threads" << std::endl
		<< "Exaples:" << std::endl
		<< "Compress: " << name << " -c -9 -high enwik8 enwik8.mcm" << std::endl
		<< "Decompress: " << name << " -d enwik8.mcm enwik8.ref" << std::endl;
	return 0;
}

void openArchive(ReadStream* stream) {
	Archive archive;
	// archive.open(stream);
}

// Compress a single file.
void compressSingleFile(ReadFileStream* fin, WriteFileStream* fout, uint32_t blocks) {
	// Split the file into a list of blocks.
	File& file = fin->getFile();
	// Seek to end and 
	file.seek(0, SEEK_END);
	uint64_t length = file.tell();
	file.seek(0, SEEK_SET);
	// Calculate block size.
	uint64_t block_size = (length + blocks - 1) / blocks;
	
	Archive archive;
}

// Compress a block to a temporary file. Thread safe.
void compressBlock() {
	WriteFileStream out_file;
	std::string temp_name;
	// This is racy.
	for (;;) {
		std::ostringstream ss;
		ss << "__TMP" + rand();
		temp_name = ss.str();
		if (!fileExists(temp_name.c_str())) {
			break;
		}
	}
	// Compress each file block into the chunk.

}

class Options {
public:
	// Block size of 0 -> file size / #threads.
	static const uint64_t kDefaultBlockSize = 0;
	enum Mode {
		kModeUnknown,
		// Compress -> Decompress -> Verify.
		// (File or directory).
		kModeTest,
		// Compress infinite times with different opt vars.
		kModeOpt,
		// In memory test.
		kModeMemTest,
		// Single file test.
		kModeSingleTest,
		// Add a single file.
		kModeAdd,
		kModeExtract,
		kModeExtractAll,
		// Single hand mode.
		kModeCompress,
		kModeDecompress,
	};
	enum CompLevel {
		kCompLevelTurbo,
		kCompLevelFast,
		kCompLevelMid,
		kCompLevelHigh,
		kCompLevelMax,
	};
	Mode mode;
	bool opt_mode;
	bool no_filter;
	Compressor* compressor;
	uint32_t mem_level;
	CompLevel comp_level;
	uint32_t threads;
	uint64_t block_size;
	FilePath archive_file;
	std::vector<FilePath> files;

	Options()
		: mode(kModeUnknown)
		, opt_mode(false)
		, no_filter(false)
		, compressor(nullptr)
		, mem_level(6)
		, comp_level(kCompLevelHigh)
		, threads(1)
		, block_size(kDefaultBlockSize) {
	}

	Compressor::Type compressorType() {
		switch (comp_level) {
		case kCompLevelTurbo: return Compressor::kTypeCMTurbo;
		case kCompLevelFast: return Compressor::kTypeCMFast;
		case kCompLevelMid: return Compressor::kTypeCMMid;
		case kCompLevelHigh: return Compressor::kTypeCMHigh;
		case kCompLevelMax: return Compressor::kTypeCMMax;
		}
		return Compressor::kTypeStore;
	}

	int parse(int argc, char* argv[]) {
		assert(argc >= 1);
		std::string program = trimExt(argv[0]);
		// Parse options.
		int i = 1;
		for (;i < argc;++i) {
			const std::string arg = argv[i];
			Mode parsed_mode = kModeUnknown;
			if (arg == "-test") parsed_mode = kModeSingleTest; // kModeTest;
			else if (arg == "-memtest") parsed_mode = kModeMemTest;
			else if (arg == "-opt") parsed_mode = kModeOpt;
			else if (arg == "-stest") parsed_mode = kModeSingleTest;
			else if (arg == "-c") parsed_mode = kModeCompress;
			else if (arg == "-d") parsed_mode = kModeDecompress;
			else if (arg == "-a") parsed_mode = kModeAdd;
			else if (arg == "-e") parsed_mode = kModeExtract;
			else if (arg == "-x") parsed_mode = kModeExtractAll;
			if (parsed_mode != kModeUnknown) {
				if (mode != kModeUnknown) {
					std::cerr << "Multiple commands specified";
					return 2;
				}
				mode = parsed_mode;
				switch (mode) {
				case kModeAdd:
				case kModeExtract:
				case kModeExtractAll:
					{
						if (++i >= argc) {
							std::cerr << "Expected archive";
							return 3;
						}
						// Archive is next.
						archive_file = FilePath(argv[i]);
						break;
					}
				}
			} else if (arg == "-opt") {
				opt_mode = true;
			}
			else if (arg == "-nofilter") no_filter = true;
			else if (arg == "-turbo") comp_level = kCompLevelTurbo;
			else if (arg == "-fast") comp_level = kCompLevelFast;
			else if (arg == "-mid") comp_level = kCompLevelMid;
			else if (arg == "-high") comp_level = kCompLevelHigh;
			else if (arg == "-max") comp_level = kCompLevelMax;
			else if (arg == "-1") mem_level = 1;
			else if (arg == "-2") mem_level = 2;
			else if (arg == "-3") mem_level = 3;
			else if (arg == "-4") mem_level = 4;
			else if (arg == "-5") mem_level = 5;
			else if (arg == "-6") mem_level = 6;
			else if (arg == "-7") mem_level = 7;
			else if (arg == "-8") mem_level = 8;
			else if (arg == "-9") mem_level = 9;
			else if (arg == "-10") {
				if (sizeof(void*) == 8) {
					mem_level = 10;
				} else {
					std::cerr << arg << " only supported with 64 bit";
				}
			} else if (arg == "-11") {
				if (sizeof(void*) == 8) {
					mem_level = 11;
				} else {
					std::cerr << arg << " only supported with 64 bit";
				}
			} else if (arg == "-b") {
				if  (i + 1 >= argc) {
					return usage(program);
				}
				std::istringstream iss(argv[++i]);
				iss >> block_size;
				block_size *= MB;
				if (!iss.good()) {
					return usage(program);
				}
			} else if (!arg.empty() && arg[0] == '-') {
				std::cerr << "Unknown option " << arg << std::endl;
				return 4;
			} else {
				if (mode == kModeAdd || mode == kModeExtract) {
					// Read in files.
					files.push_back(FilePath(argv[i]));
				} else {
					// Done parsing.
					break;
				}
			}
		}
		const bool single_file_mode =
			mode == kModeCompress || mode == kModeDecompress || mode == kModeSingleTest ||
			mode == kModeMemTest || mode == kModeOpt;
		if (single_file_mode) {
			std::string in_file, out_file;
			// Read in file and outfile.
			if (i < argc) {
				in_file = argv[i++];
			}
			if (i < argc) {
				out_file = argv[i++];
			} else {
				if (mode == kModeDecompress) {
					out_file = in_file + ".decomp";
				} else {
					out_file = in_file + ".mcm";
				}
			}
			if (mode == kModeMemTest) {
				// No out file for memtest.
				files.push_back(FilePath(in_file));
			} else if (mode == kModeCompress || mode == kModeSingleTest || mode == kModeOpt) {
				archive_file = FilePath(out_file);
				files.push_back(FilePath(in_file));
			} else {
				archive_file = FilePath(in_file);
				files.push_back(FilePath(out_file));
			}
		}
		if (archive_file.isEmpty() || files.empty()) {
			std::cerr << "Error, input or output files missing" << std::endl;
			usage(program);
			return 5;
		}
		return 0;
	}
};

void decompress(Stream* in, Stream* out) {
	ArchiveHeader header;
	header.read(*in);
	if (!header.isValid()) {
		std::cerr << "Invalid archive or invalid version";
		return;
	}
	DefaultFilter f(out);
	auto start = clock();
	std::unique_ptr<Compressor> comp(header.createCompressor());
	comp->decompress(in, &f);
	f.flush();
	auto time = clock() - start;
	std::cout << std::endl << "DeCompression took " << clockToSeconds(time) << "s" << std::endl;
}

int main(int argc, char* argv[]) {
	CompressorFactories::init();
	// runAllTests();
	Options options;
	auto ret = options.parse(argc, argv);
	if (ret) {
		std::cerr << "Failed to parse arguments";
		return ret;
	}
	Archive archive;
	switch (options.mode) {
	case Options::kModeMemTest: {
		const uint32_t iterations = kIsDebugBuild ? 1 : 1;
		// Read in the whole file.
		size_t length = 0;
		uint64_t long_length = 0;
		std::vector<uint64_t> lengths;
		for (const auto& file : options.files) {
			lengths.push_back(getFileLength(file.getName()));
			long_length += lengths.back();
		}
		length = static_cast<uint32_t>(long_length);
		check(length < 300 * MB);
		auto in_buffer = new byte[length];
		// Read in the files.
		uint32_t index = 0;
		uint64_t read_pos = 0;
		for (const auto& file : options.files) {
			File f;
			f.open(file.getName(), std::ios_base::in | std::ios_base::binary);
			size_t count = f.read(in_buffer + read_pos, static_cast<size_t>(lengths[index]));
			check(count == lengths[index]);
			index++;
		}
		// Create the memory compressor.
		auto* compressor = new LZFast;
		//auto* compressor = new LZ4;
		//auto* compressor = new MemCopyCompressor;
		auto out_buffer = new byte[compressor->getMaxExpansion(length)];
		uint32_t comp_start = clock();
		uint32_t comp_size;
		static const bool opt_mode = false;
		if (opt_mode) {
			uint32_t best_size = 0xFFFFFFFF;
			uint32_t best_opt = 0;
			for (uint32_t opt = 0; ; ++opt) {
				compressor->setOpt(opt);
				comp_size = compressor->compressBytes(in_buffer, out_buffer, length);
				std::cout << "Opt " << opt << " / " << best_opt << " =  " << comp_size << "/" << best_size << std::endl;
				if (comp_size < best_size) {
					best_opt = opt;
					best_size = comp_size;
				}
			}
		} else {
			for (uint32_t i = 0; i < iterations; ++i) {
				comp_size = compressor->compressBytes(in_buffer, out_buffer, length);
			}
		}

		uint32_t comp_end = clock();
		std::cout << "Compression " << length << " -> " << comp_size << " = " << float(double(length) / double(comp_size)) << " rate: "
			<< prettySize(static_cast<uint64_t>(long_length * iterations / clockToSeconds(comp_end - comp_start))) << "/s" << std::endl;
		memset(in_buffer, 0, length);
		uint32_t decomp_start = clock();
		static const uint32_t decomp_iterations = kIsDebugBuild ? 1 : iterations * 5;
		for (uint32_t i = 0; i < decomp_iterations; ++i) {
			compressor->decompressBytes(out_buffer, in_buffer, length);
		}
		uint32_t decomp_end = clock();
		std::cout << "Decompression took: " << decomp_end - comp_end << " rate: "
			<< prettySize(static_cast<uint64_t>(long_length * decomp_iterations / clockToSeconds(decomp_end - decomp_start))) << "/s" << std::endl;
		index = 0;
		for (const auto& file : options.files) {
			File f;
			f.open(file.getName(), std::ios_base::in | std::ios_base::binary);
			uint32_t count = static_cast<uint32_t>(f.read(out_buffer, static_cast<uint32_t>(lengths[index])));
			check(count == lengths[index]);
			for (uint32_t i = 0; i < count; ++i) {
				if (out_buffer[i] != in_buffer[i]) {
					std::cerr << "File" << file.getName() << " doesn't match at byte " << i << std::endl;
					check(false);
				}
			}
			index++;
		}
		std::cout << "Decompression verified" << std::endl;
		break;
	}
	case Options::kModeSingleTest: {
		
	}
	case Options::kModeOpt:
	case Options::kModeCompress:
	case Options::kModeTest: {
		printHeader();

		int err = 0;
		ReadFileStream fin;
		WriteFileStream fout;
		auto in_file = options.files.back().getName();
		auto out_file = options.archive_file.getName();

		ArchiveHeader header;
		header.mem_usage = options.mem_level;
		header.algorithm = options.compressorType();

		if (err = fin.open(in_file, std::ios_base::in | std::ios_base::binary)) {
			std::cerr << "Error opening: " << in_file << " (" << errstr(err) << ")" << std::endl;
			return 1;
		}

		if (options.mode == Options::kModeOpt) {
			std::cout << "Optimizing " << in_file << std::endl;
			uint64_t best_size = std::numeric_limits<uint64_t>::max();
			size_t best_var = 0;
			for (size_t opt_var = 0; ; ++opt_var) {
				const clock_t start = clock();
				fin.seek(0);
				VoidWriteStream fout;
				header.write(fout);
				std::unique_ptr<Compressor> comp(header.createCompressor());
				if (!comp->setOpt(opt_var)) {
					continue;
				}
				{
					ProgressStream rms(&fin, &fout);
					DefaultFilter f(&rms);
					f.setOpt(opt_var);
					comp->compress(&f, &fout);
					f.dumpInfo();
				}
				clock_t time = clock() - start;
				const auto size = fout.tell();
				if (size < best_size) {
					best_size = size;
					best_var = opt_var;
				}
				std::cout << "opt_var=" << opt_var << " best=" << best_var << "(" << best_size << ") "
					<< fin.getCount() << "->" << size << " took " << clockToSeconds(time) << "s" << std::endl;
			}
		} else {
			const clock_t start = clock();
			if (err = fout.open(out_file, std::ios_base::out | std::ios_base::binary)) {
				std::cerr << "Error opening: " << out_file << " (" << errstr(err) << ")" << std::endl;
				return 2;
			}

			std::cout << "Compressing to " << out_file << " mem level=" << options.mem_level << std::endl;

			header.write(fout);

			std::unique_ptr<Compressor> comp(header.createCompressor());
			{
				ProgressStream rms(&fin, &fout);
				DefaultFilter f(&rms);
				comp->compress(&f, &fout);
				f.dumpInfo();
			}
			clock_t time = clock() - start;
			std::cout << "Compression " << fin.getCount() << "->" << fout.getCount() << " took " << clockToSeconds(time) << "s" << std::endl;
			std::cout << "Rate: " << double(time) * (1000000000.0 / double(CLOCKS_PER_SEC)) / double(fin.getCount()) << " ns/B" << std::endl;
			std::cout << "Size: " << fout.getCount() << " bytes @ " << double(fout.getCount()) * 8.0 / double(fin.getCount()) << " bpc" << std::endl;

			fout.close();
			fin.close();
			comp.reset(nullptr);

			if (options.mode == Options::kModeSingleTest) {
				if (err = fin.open(out_file, std::ios_base::in | std::ios_base::binary)) {
					std::cerr << "Error opening: " << out_file << " (" << errstr(err) << ")" << std::endl;
					return 1;
				}
			
				std::cout << "Decompresing & verifying file" << std::endl;		
				VerifyStream verifyStream(in_file);
				ProgressStream rms(&fin, &verifyStream, false);
				decompress(&fin, &rms);
				verifyStream.summary();
				fin.close();
			}
		}
		break;
#if 0
		// Note: Using overwrite, this is dangerous.
		archive.open(options.archive_file, true, std::ios_base::in | std::ios_base::out);
		// Add the files to the archive so we can compress the segments.
		archive.addNewFileBlock(options.files);
		assert(options.files.size() == 1U);
		// Split the files into as many blocks as we have threads.
		auto files = Archive::splitFiles(options.files, options.threads, 64 * KB);
		MultiCompressionJob multi_job;
		std::vector<Archive::Job*> jobs(files.size());
		// Compress the files in the main thread.
		archive.compressFiles(0, &files[0], 4);
		break;
#endif
	}
	case Options::kModeAdd: {
		// Add a single file.
		break;
	}
	case Options::kModeDecompress: {
		auto in_file = options.archive_file.getName();
		auto out_file = options.files.back().getName();
		ReadFileStream fin;
		WriteFileStream fout;
		int err = 0;
		if (err = fin.open(in_file, std::ios_base::in | std::ios_base::binary)) {
			std::cerr << "Error opening: " << out_file << " (" << errstr(err) << ")" << std::endl;
			return 1;
		}
		if (err = fout.open(out_file, std::ios_base::out | std::ios_base::binary)) {
			std::cerr << "Error opening: " << out_file << " (" << errstr(err) << ")" << std::endl;
			return 2;
		}
		printHeader();
		std::cout << "Decompresing file " << in_file << std::endl;
		ProgressStream rms(&fin, &fout, false);
		decompress(&fin, &rms);
		fin.close();
		fout.close();
		// Decompress the single file in the archive to the output out.
		break;
	}
	case Options::kModeExtract: {
		// Extract a single file from multi file archive .
		break;
	}
	case Options::kModeExtractAll: {
		// Extract all the files in the archive.
		break;
	}
	}

#if 0
	
#endif
	return 0;
}
