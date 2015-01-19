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
#include "CCM.hpp"
#include "CM.hpp"
#include "Dict.hpp"
#include "File.hpp"
#include "Huffman.hpp"
#include "LZ.hpp"
#include "Tests.hpp"
#include "TurboCM.hpp"
#include "X86Binary.hpp"

CompressorFactories* CompressorFactories::instance = nullptr;

typedef X86AdvancedFilter DefaultFilter;
//typedef IdentityFilter DefaultFilter;
//typedef Dict DefaultFilter;

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
			if (!differences) {
				std::cerr
					<< "Difference found at byte! " << total << " b1: "
					<< "ref: " << (int)ref << " new: " << (int)c << std::endl;
			}
			++differences;
		}
		++total;
	}

	forceinline uint32_t getTotal() const {
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

static int usage(const std::string& name) {
	std::cout
		<< "mcm file compressor v0." << CM<6>::version << ", (c)2015 Google Inc" << std::endl
		<< "Caution: Use only for testing!!" << std::endl
		<< "Usage: " << name << " [options] <infile> <outfile>" << std::endl
		<< "Options: -d for decompress" << std::endl
		<< "-1 ... -9 specifies ~32mb ... ~1500mb memory, " << std::endl
		<< "-test tests the file after compression is done" << std::endl
		<< "-b <mb> specifies block size in MB" << std::endl
		<< "-t <threads> the number of threads to use (decompression requires the same number of threads" << std::endl
		<< "Exaples:" << std::endl
		<< "Compress: " << name << " -9 enwik8 enwik8.mcm" << std::endl
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
		MODE_UNKNOWN,
		// Compress -> Decompress -> Verify.
		// (File or directory).
		MODE_TEST,
		// In memory test.
		MODE_MEM_TEST,
		// Single file test.
		MODE_SINGLE_TEST,
		// Add a single file.
		MODE_ADD,
		MODE_EXTRACT,
		MODE_EXTRACT_ALL,
		// Single hand mode.
		MODE_COMPRESS,
		MODE_DECOMPRESS,
	};
	Mode mode;
	bool opt_mode;
	Compressor* compressor;
	uint32_t mem_level;
	uint32_t threads;
	uint64_t block_size;
	FilePath archive_file;
	std::vector<FilePath> files;
	
	Options()
		: mode(MODE_UNKNOWN)
		, opt_mode(false)
		, compressor(nullptr)
		, mem_level(6)
		, threads(1)
		, block_size(kDefaultBlockSize) {
	}

	int parse(int argc, char* argv[]) {
		assert(argc >= 1);
		std::string program = trimExt(argv[0]);
		// Parse options.
		int i = 1;
		for (;i < argc;++i) {
			const std::string arg = argv[i];
			Mode parsed_mode = MODE_UNKNOWN;
			if (arg == "-test") parsed_mode = MODE_TEST;
			else if (arg == "-memtest") parsed_mode = MODE_MEM_TEST;
			else if (arg == "-stest") parsed_mode = MODE_SINGLE_TEST;
			else if (arg == "-c") parsed_mode = MODE_COMPRESS;
			else if (arg == "-d") parsed_mode = MODE_DECOMPRESS;
			else if (arg == "-a") parsed_mode = MODE_ADD;
			else if (arg == "-e") parsed_mode = MODE_EXTRACT;
			else if (arg == "-x") parsed_mode = MODE_EXTRACT_ALL;
			if (parsed_mode != MODE_UNKNOWN) {
				if (mode != MODE_UNKNOWN) {
					std::cerr << "Multiple commands specified";
					return 2;
				}
				mode = parsed_mode;
				switch (mode) {
				case MODE_ADD:
				case MODE_EXTRACT:
				case MODE_EXTRACT_ALL:
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
			} else if (arg == "-1") mem_level = 1;
			else if (arg == "-2") mem_level = 2;
			else if (arg == "-3") mem_level = 3;
			else if (arg == "-4") mem_level = 4;
			else if (arg == "-5") mem_level = 5;
			else if (arg == "-6") mem_level = 6;
			else if (arg == "-7") mem_level = 7;
			else if (arg == "-8") mem_level = 8;
			else if (arg == "-9") mem_level = 9;
			else if (arg == "-10") mem_level = 10;
			else if (arg == "-b") {
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
				if (mode == MODE_ADD || mode == MODE_EXTRACT) {
					// Read in files.
					files.push_back(FilePath(argv[i]));
				} else {
					// Done parsing.
					break;
				}
			}
		}
		const bool single_file_mode =
			mode == MODE_COMPRESS || mode == MODE_DECOMPRESS || mode == MODE_TEST || mode == MODE_MEM_TEST;
		if (single_file_mode) {
			std::string in_file, out_file;
			// Read in file and outfile.
			if (i < argc) {
				in_file = argv[i++];
			}
			if (i < argc) {
				out_file = argv[i++];
			} else {
				if (mode == MODE_DECOMPRESS) {
					out_file = in_file + ".decomp";
				} else {
					out_file = in_file + ".mcm";
				}
			}
			if (mode == MODE_MEM_TEST) {
				// No out file for memtest.
				files.push_back(FilePath(in_file));
			} else if (mode == MODE_COMPRESS || mode == MODE_TEST) {
				archive_file = FilePath(out_file);
				files.push_back(FilePath(in_file));
			} else {
				archive_file = FilePath(in_file);
				files.push_back(FilePath(out_file));
			}
		}
		return 0;
	}
};

int main(int argc, char* argv[]) {
	CompressorFactories::init();
	//runAllTests();
	Options options;
	auto ret = options.parse(argc, argv);
	if (ret) {
		std::cerr << "Failed to parse arguments";
		return ret;
	}
	Archive archive;
	switch (options.mode) {
	case Options::MODE_MEM_TEST: {
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
	case Options::MODE_SINGLE_TEST: {
		
	}
	case Options::MODE_TEST: {
		int err = 0;
		ReadFileStream fin;
		WriteFileStream fout;
		auto in_file = options.files.back().getName();
		auto out_file = options.archive_file.getName();
		if (err = fin.open(in_file, std::ios_base::in | std::ios_base::binary)) {
			std::cerr << "Error opening: " << in_file << " (" << errstr(err) << ")" << std::endl;
			return 1;
		}
		if (err = fout.open(out_file, std::ios_base::out | std::ios_base::binary)) {
			std::cerr << "Error opening: " << out_file << " (" << errstr(err) << ")" << std::endl;
			return 2;
		}

		clock_t start = clock();
		std::cout << "Compressing to " << out_file << " mem level=" << options.mem_level << std::endl;
		std::unique_ptr<Compressor> comp;
		comp.reset(new CM<5>);
		//comp.reset(new TurboCM<6>);
		//comp.reset(new CCM);
		comp->setMemUsage(options.mem_level);
		{
			ProgressReadStream rms(&fin, &fout);
			DefaultFilter f(&rms);
			comp->compress(&f, &fout);
		}
		clock_t time = clock() - start;
		std::cout << "Compression " << fin.getCount() << "->" << fout.getCount() << " took " << time << "ms" << std::endl;
		std::cout << "Rate: " << double(time) * (1000000000.0 / double(CLOCKS_PER_SEC)) / double(fin.getCount()) << " ns/B" << std::endl;
		std::cout << "Size: " << fout.getCount() << " bytes @ " << double(fout.getCount()) * 8.0 / double(fin.getCount()) << " bpc" << std::endl;

		fout.close();
		fin.close();

		if (err = fin.open(out_file, std::ios_base::in | std::ios_base::binary)) {
			std::cerr << "Error opening: " << out_file << " (" << errstr(err) << ")" << std::endl;
			return 1;
		}
			
		std::cout << "Decompresing & verifying file" << std::endl;		
		VerifyStream verifyStream(in_file);
		DefaultFilter f(&verifyStream);
		start = clock();
		comp->decompress(&fin, &f);
		f.flush();
		verifyStream.summary();
		time = clock() - start;
		std::cout << "DeCompression took " << time << " MS" << std::endl;

		fin.close();
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
	case Options::MODE_ADD: {
		// Add a single file.
		break;
	}
	case Options::MODE_COMPRESS: {
		// Single file mode, compress to a file with no name (the only file in the archive).
		break;
	}
	case Options::MODE_DECOMPRESS: {
		// Decompress the single file in the archive to the output out.
		break;
	}
	case Options::MODE_EXTRACT: {
		// Extract a single file from multi file archive .
		break;
	}
	case Options::MODE_EXTRACT_ALL: {
		// Extract all the files in the archive.
		break;
	}
	}

	/* 
	if (in_file.empty() || out_file.empty()) {
		std::cerr << "Error, input or output files missing" << std::endl;
		usage(program);
		return 5;
	}
	*/

#if 0
	
#endif
	return 0;
}
