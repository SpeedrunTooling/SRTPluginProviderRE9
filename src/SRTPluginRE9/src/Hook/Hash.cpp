#include "Hash.h"
#include "sha-256.h"

namespace SRTPluginRE9::Hash
{
	bool FileExists(const char *filePath)
	{
		FILE *file;
		return !fopen_s(&file, filePath, "rb") && !fclose(file);
	}

	const std::expected<const size_t, std::string> GetFileSizeT(const char *filePath)
	{
		FILE *file;

		if (fopen_s(&file, filePath, "rb"))
			return std::unexpected("Unable to open file"); // Unable to open!

		if (_fseeki64(file, 0LL, SEEK_END) < 0LL)
		{
			fclose(file);
			return std::unexpected("Unable to seek file"); // Unable to seek!
		}

		size_t size = _ftelli64(file);
		fclose(file);
		return size;
	}

	const std::expected<const std::array<uint8_t, 32>, std::string> GetFileHashSHA256(const char *filePath)
	{
		constexpr const int32_t bufferSize = SIZE_OF_SHA_256_CHUNK * SIZE_OF_SHA_256_CHUNK;

		FILE *file;
		if (fopen_s(&file, filePath, "rb"))
			return std::unexpected("Unable to open file for reading");

		std::array<uint8_t, 32> hash{};
		struct Sha_256 sha_256;
		sha_256_init(&sha_256, hash.data());

		auto fileSize = GetFileSizeT(filePath);
		if (!fileSize.has_value())
			return std::unexpected(fileSize.error());

		std::array<uint8_t, bufferSize> fileData{};
		size_t totalBytesRead = 0;
		size_t bytesRead = 0;
		int error = 0;
		int eof = 0;
		do
		{
			bytesRead = fread(fileData.data(), sizeof(uint8_t), bufferSize, file);
			sha_256_write(&sha_256, fileData.data(), bytesRead);
			totalBytesRead += bytesRead;
			error = ferror(file);
			eof = feof(file);
			clearerr(file);
		} while ((totalBytesRead < fileSize.value()) && !error && !eof);
		fclose(file);
		sha_256_close(&sha_256);

		if (error)
			return std::unexpected("File error occurred after operation");

		return hash;
	}

	const std::string ArrayToHexString(const std::array<uint8_t, 32> &array)
	{
		size_t arraySize = array.size();
		size_t bufferSize = (arraySize * 6) - 2 + 1;

		std::string toString(bufferSize, 0);
		for (size_t i = 0; i < arraySize; ++i)
		{
			if (i < arraySize - 1)
				snprintf(toString.data() + (i * 6), 7, "0x%02X, ", array[i]);
			else
				snprintf(toString.data() + (i * 6), 5, "0x%02X", array[i]);
		}

		return toString;
	}
}
