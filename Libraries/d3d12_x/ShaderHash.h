#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace GDKScarlett::D3D12X
{
	constexpr int CacheVersion = 0;

	inline uint64_t Fnv1a(const void* data, size_t size)
	{
		const uint8_t* bytes = static_cast<const uint8_t*>(data);
		uint64_t hash = 1469598103934665603ULL;
		for (size_t i = 0; i < size; ++i)
		{
			hash ^= bytes[i];
			hash *= 1099511628211ULL;
		}
		return hash;
	}

	inline std::string HexKey(uint64_t hash)
	{
		static const char* digits = "0123456789abcdef";
		char buffer[17];
		for (int i = 15; i >= 0; --i)
		{
			buffer[i] = digits[hash & 0xF];
			hash >>= 4;
		}
		buffer[16] = 0;
		return std::string(buffer);
	}

	inline std::string CacheFileName(const std::string& key)
	{
		return key + ".v" + std::to_string(CacheVersion) + ".cso";
	}
}
