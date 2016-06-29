#ifndef BASE64_H
#define BASE64_H

#define base64_encode_size(x) (x * 4 / 3) + 5

int base64encode(
		const void* data_buf,
		size_t dataLength,
		char* result,
		size_t resultSize
);

#endif // BASE64_H
