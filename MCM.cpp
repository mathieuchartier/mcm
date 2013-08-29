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

#include <stdio.h>
#include <string.h>
#include <string>
#include <ctime>
#include <sstream>
#include <fstream>

#include "Archive.hpp"
#include "CM.hpp"
#include "Filter.hpp"
#include "Huffman.hpp"
#include "LZ.hpp"
#include "File.hpp"

FilterCompressor<CM<6>, IdentityFilterFactory> comp;
//FilterCompressor<LZW<false>, IdentityFilterFactory> comp;
//FilterCompressor<Huffman, IdentityFilterFactory> comp;

std::string errstr(int err) {
#ifdef WIN32
	char buffer[1024];
	strerror_s(buffer, sizeof(buffer), err);
	return buffer;
#else
	return strerror(err);
#endif
}

class VerifyStream {
public:
	std::ifstream fin;
	size_t differences, total;

	VerifyStream(const std::string& file_name) {
		fin.open(file_name, std::ios_base::in | std::ios_base::binary);
		assert(fin.good());
		init();
	}

	void init() {
		differences = total = 0;
	}

	void write(int c) {
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

	forceinline size_t getTotal() const {
		return total;
	}

	void summary() {
		fin.get();
		if (!fin.eof()) {
			std::cerr << "ERROR: Output truncated at byte " << fin.tellg() << std::endl;
		} else {
			if (differences) {
				std::cerr << "ERROR: Total differences: " << differences << std::endl;
			} else {
				std::cout << "No differences found!" << std::endl;
			}
		}
	}
};

std::string trim_ext(std::string str) {
	std::streamsize start = 0, pos;
	if ((pos = str.find_last_of('\\')) != std::string::npos) {
		start = std::max(start, pos + 1);
	}
	if ((pos = str.find_last_of('/')) != std::string::npos) {
		start = std::max(start, pos + 1);
	}
	return str.substr(static_cast<size_t>(start));
}

int usage(const std::string& name) {
	std::cout
		<< "mcm file compressor v0." << comp.version << ", (c)2013 Google Inc" << std::endl
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

int main(int argc, char* argv[]) {
	bool decompress = false, test_mode = false, opt_mode = false, mem_mode = false;
	uint64_t block_size = MB * MB;
	size_t threads = 1;
	assert(argc >= 1);
	std::string in_file, out_file, program = trim_ext(argv[0]);
	// Parse options.
	int i = 1;
	for (;i < argc;++i) {
		std::string arg = argv[i];
		if (arg == "-d") decompress = true;
		else if (arg == "-test") test_mode = true;
		else if (arg == "-opt") opt_mode = true;
		else if (arg == "-1") comp.setMemUsage(1);
		else if (arg == "-2") comp.setMemUsage(2);
		else if (arg == "-3") comp.setMemUsage(3);
		else if (arg == "-4") comp.setMemUsage(4);
		else if (arg == "-5") comp.setMemUsage(5);
		else if (arg == "-6") comp.setMemUsage(6);
		else if (arg == "-7") comp.setMemUsage(7);
		else if (arg == "-8") comp.setMemUsage(8);
		else if (arg == "-9") comp.setMemUsage(9);
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
		} else if (arg == "-t") {
			if  (i + 1 >= argc) {
				return usage(program);
			}
			std::istringstream iss(argv[++i]);
			iss >> threads;
			if (threads == 0 || !iss.good()) {
				return usage(program);
			}
		} else if (arg == "-mem") mem_mode = true;
		else if (arg.length() && arg[0] == '-') {
			std::cerr << "Unknown option " << arg << std::endl;
			return 1;
		} else {
			break;
		}
	}
	
	// Read in file and outfile.
	if (i < argc) {
		in_file = argv[i++];
	}

	if (i < argc) {
		out_file = argv[i++];
	} else {
		if (decompress) {
			out_file = in_file + ".decomp";
		} else {
			out_file = in_file + ".mcm";
		}
	}

	if (in_file.empty() || out_file.empty()) {
		std::cerr << "Error, input or output files missing" << std::endl;
		usage(program);
		return 5;
	}

	int err = 0;
	BufferedFileStream<4 * KB> fin, fout;
	if (err = fin.open(in_file, std::ios_base::binary | std::ios_base::in)) {
		std::cerr << "Error opening: " << in_file << " (" << errstr(err) << ")" << std::endl;
		return 1;
	}
	if (err = fout.open(out_file, std::ios_base::binary | std::ios_base::out)) {
		std::cerr << "Error opening: " << out_file << " (" << errstr(err) << ")" << std::endl;
		return 2;
	}

	clock_t start = clock();
	if (decompress) {
		std::cout << "DeCompressing" << std::endl;
		bool result = comp.DeCompress(fout, fin);
		if (!result) {
			std::cerr << "DeCompression failed, file not compressed by this version or correct version" << std::endl;
		} else {
			auto time = clock() - start;
			std::cout << "DeCompression took " << time << "MS" << std::endl;
			std::cout << "Rate: " << double(time) * (1000000000.0 / double(CLOCKS_PER_SEC)) / double(fout.getTotal()) << " ns/B" << std::endl;
		}
	} else {
		std::cout << "Compressing to " << out_file << std::endl;
		auto size = comp.Compress(fout, fin);
#if 0
		srand(static_cast<unsigned int>(time(NULL)));
		if (opt_mode) {
			size_t* opt_var = &comp.opt_var; //comp.match_model.opt_var;
			size_t best_var = *opt_var;
			//for (;*opt_var < 32 * 32;++*opt_var) {
			//for (;;) {
			for (;;comp.opt_var = rand32()) {
				fout.restart();
				fin.restart();
				auto new_size = comp.Compress(fout, fin);
				if (new_size < size) {
					std::cout << "(" << *opt_var << "): " << size << " -> " << new_size << std::endl;
					size = new_size;
					best_var = *opt_var;
				}
				std::cout << "best(" << std::hex << best_var << ")=" << std::dec << size << std::endl;
			}
		}
#endif	
		clock_t time = clock() - start;
		std::cout << "Compression took " << time << " MS" << std::endl;
		std::cout << "Rate: " << double(time) * (1000000000.0 / double(CLOCKS_PER_SEC)) / double(fin.getTotal()) << " ns/B" << std::endl;
		std::cout << "Size: " << fout.getTotal() << " bytes @ " << double(fout.getTotal()) * 8.0 / double(fin.getTotal()) << " bpc" << std::endl;

		if (test_mode) {
			fout.close();
			fin.close();

			if (err = fin.open(out_file, std::ios_base::in | std::ios_base::binary)) {
				std::cerr << "Error opening: " << out_file << " (" << errstr(err) << ")" << std::endl;
				return 1;
			}
			
			std::cout << "Decompresing & verifying file" << std::endl;
			VerifyStream verifyStream(in_file);
			start = clock();
			comp.DeCompress(verifyStream, fin);
			verifyStream.summary();
			time = clock() - start;
			std::cout << "DeCompression took " << time << " MS" << std::endl;

			fin.close();
		}
	}

	fout.close();
	fin.close();
	return 0;
}
