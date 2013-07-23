#ifndef _WORD_MODEL_HPP_
#define _WORD_MODEL_HPP_

#include "UTF8.hpp"

class WordModel {
public:
	// Hashes.
	hash_t prev;
	hash_t h1, h2;

	// UTF decoder.
	UTF8Decoder<false> decoder;

	// Length of the model.
	size_t len;

	// Transform table.
	static const size_t transform_table_size = 256;
	size_t transform[transform_table_size];

	WordModel() { 
		size_t index = 0;
		for (auto& t : transform) t = transform_table_size;
		for (size_t i = 'a'; i <= 'z'; ++i) {
			transform[i] = index++;
		}
		for (size_t i = 'A'; i <= 'Z'; ++i) {
			transform[i] = transform[(byte)lower_case((char)i)];
		}
			
		// Word model transform.
		// transform['_'] = index++;
		transform['À'] = transform['à'] = index++;
		transform['Á'] = transform['á'] = index++;
		transform['Â'] = transform['â'] = index++;
		transform['Ã'] = transform['ã'] = index++;
		transform['Ä'] = transform['ä'] = index++;
		transform['Å'] = transform['å'] = index++;
		transform['Æ'] = transform['æ'] = index++;

		transform['Ç'] = transform['ç'] = index++;

		transform['È'] = transform['è'] = index++;
		transform['É'] = transform['é'] = index++;
		transform['Ê'] = transform['ê'] = index++;
		transform['Ë'] = transform['ë'] = index++;

		transform['Ì'] = transform['ì'] = index++;
		transform['Í'] = transform['í'] = index++;
		transform['Î'] = transform['î'] = index++;
		transform['Ï'] = transform['ï'] = index++;

		transform['È'] = transform['è'] = index++;
		transform['É'] = transform['é'] = index++;
		transform['Ê'] = transform['ê'] = index++;
		transform['Ë'] = transform['ë'] = index++;

		transform['Ð'] = index++;
		transform['ð'] = index++;
		transform['Ñ'] = transform['ñ'] = index++;

		transform['Ò'] = transform['ò'] = index++;
		transform['Ó'] = transform['ó'] = index++;
		transform['Ô'] = transform['ô'] = index++;
		transform['Ö'] = transform['ö'] = index++;
		transform['Õ'] = transform['õ'] = index++;

		transform['Ò'] = transform['ò'] = index++;
		transform['Ó'] = transform['ó'] = index++;
		transform['Ô'] = transform['ô'] = index++;
		transform['Ö'] = transform['ö'] = index++;
			
		transform['Ù'] = transform['ù'] = index++;
		transform['Ú'] = transform['ú'] = index++;
		transform['Û'] = transform['û'] = index++;
		transform['Ü'] = transform['ü'] = index++;
	}

	void init() {
		len = 0;
		prev = 0;
		reset();
		decoder.init();
	}

	forceinline void reset() {
		h1 = 0x9FD33A52;
		h2 = 0x21724712;
		len = 0;
	}

	forceinline size_t getHash() const {
		return h1 + h2;
	}

	forceinline hash_t getPrevHash() const {
		return prev;
	}

	forceinline size_t getLength() const {
		return len;
	}

	void update(char c) {
		size_t cur = transform[(size_t)(byte)c];
		if (cur != transform_table_size || (cur >= 128 && cur != transform_table_size)) {
			h1 = (h1 + cur) * 54;
			h2 = h1 >> 7;
			++len;
		} else {
			prev = rotate_left(getHash(), 13);
			reset();
		}
	}

	static forceinline hash_t hashFunc(size_t c, hash_t h) {
		h += c;
		h += rotate_left(h, 10);
		return h ^ (h >> 6);
	}

	void updateUTF(char c) {
		decoder.update(c);
		size_t cur = decoder.getAcc();
		if (decoder.done()) {
			if (cur < 256) cur = transform[cur];
			if (LIKELY(cur != transform_table_size)) {
				h1 = (h1 + cur) * 54;
				h2 = h1 >> 7;
				++len;
			} else {
				if (len) {
					prev = rotate_left(getHash(), 13);
					reset();
				}
				return;
			}
		} else {
			h2 = hashFunc(cur, h2);
		}
	}
};

#endif