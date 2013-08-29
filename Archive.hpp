#ifndef ARCHIVE_HPP_
#define ARCHIVE_HPP_

#include "CM.hpp"
#include "Compressor.hpp"

class Archive {
	// TODO:
	class FileHeader {
	public:
		std::string name;
		int attributes;
	};

	// Blocks are independently decodable.
	class Block {
	public:
		// Compression algorithm.
		byte algorithm;

		// Memory level.
		byte mem;

		// Padding.
		byte pad1, pad2;
		
		// Compressed size of the block (not including header!).
		size_t csize;
	};

	// Compressor factories.
	std::vector<Compressor::Factory*> compressor_factories;
public:
	Archive() {
		// Add CM compressor.
		compressor_factories.push_back(new Compressor::FactoryOf<Store>());
	}

	virtual ~Archive() {
		deleteValues(compressor_factories);
	}

	Compressor* make_compressor(size_t type) {
		assert(type < compressor_factories.size());
		auto* factory = compressor_factories[type];
		assert(factory != nullptr);
		return factory->create();
	}

	template <typename TStream>
	Archive(const TStream& in) {
		
	}
};

#endif
