#include "DxbcUtil.h"

#include <cstring>

namespace GDKScarlett::D3D12X
{
	bool FindDxilPart(const uint8_t* data, size_t size,
	                  const uint8_t*& part, uint32_t& partSize, bool& isNobc)
	{
		part = nullptr;
		partSize = 0;
		isNobc = false;
		if (!data || size < 0x20 || memcmp(data, "DXBC", 4) != 0)
		{
			return false;
		}
		const uint8_t* end = data + size;
		uint32_t partCount = *reinterpret_cast<const uint32_t*>(data + 0x1C);
		if (partCount > 64)
		{
			return false;
		}
		const uint32_t* offsets = reinterpret_cast<const uint32_t*>(data + 0x20);
		if (reinterpret_cast<const uint8_t*>(offsets + partCount) > end)
		{
			return false;
		}
		for (uint32_t i = 0; i < partCount; ++i)
		{
			if (data + offsets[i] + 8 > end)
			{
				continue;
			}
			const uint8_t* partPtr = data + offsets[i];
			if (memcmp(partPtr, "DXIL", 4) != 0)
			{
				continue;
			}
			uint32_t declaredSize = *reinterpret_cast<const uint32_t*>(partPtr + 4);
			const uint8_t* programHeader = partPtr + 8;
			if (programHeader + 24 > end)
			{
				return false;
			}
			uint32_t bitcodeOffset = *reinterpret_cast<const uint32_t*>(programHeader + 16);
			const uint8_t* bitcode = programHeader + 8 + bitcodeOffset;
			if (bitcode + 4 <= end && memcmp(bitcode, "NOBC", 4) == 0)
			{
				isNobc = true;
			}
			size_t available = static_cast<size_t>(end - programHeader);
			part = programHeader;
			partSize = declaredSize < available ? declaredSize : static_cast<uint32_t>(available);
			return true;
		}
		return false;
	}

	bool PsvInfo(const uint8_t* data, size_t size, uint32_t& shaderKind, uint32_t numThreads[3])
	{
		shaderKind = ~0u;
		numThreads[0] = numThreads[1] = numThreads[2] = 1;
		if (!data || size < 0x20 || memcmp(data, "DXBC", 4) != 0)
		{
			return false;
		}
		uint32_t partCount = *reinterpret_cast<const uint32_t*>(data + 0x1C);
		if (partCount > 64)
		{
			return false;
		}
		const uint32_t* offsets = reinterpret_cast<const uint32_t*>(data + 0x20);
		const uint8_t* end = data + size;
		if (reinterpret_cast<const uint8_t*>(offsets + partCount) > end)
		{
			return false;
		}
		for (uint32_t i = 0; i < partCount; ++i)
		{
			const uint8_t* partPtr = data + offsets[i];
			if (partPtr + 8 > end || memcmp(partPtr, "PSV0", 4) != 0)
			{
				continue;
			}
			uint32_t declaredSize = *reinterpret_cast<const uint32_t*>(partPtr + 4);
			const uint8_t* runtimeInfo = partPtr + 8 + 4;
			uint32_t runtimeInfoSize = *reinterpret_cast<const uint32_t*>(partPtr + 8);
			if (runtimeInfoSize < 0x1C || partPtr + 12 + runtimeInfoSize > end || declaredSize < 4 + runtimeInfoSize)
			{
				return false;
			}
			shaderKind = *reinterpret_cast<const uint32_t*>(runtimeInfo + 0x18);
			if (runtimeInfoSize >= 0x30 && shaderKind == 5)
			{
				numThreads[0] = *reinterpret_cast<const uint32_t*>(runtimeInfo + 0x24);
				numThreads[1] = *reinterpret_cast<const uint32_t*>(runtimeInfo + 0x28);
				numThreads[2] = *reinterpret_cast<const uint32_t*>(runtimeInfo + 0x2C);
				if (!numThreads[0] || numThreads[0] > 1024) numThreads[0] = 1;
				if (!numThreads[1] || numThreads[1] > 1024) numThreads[1] = 1;
				if (!numThreads[2] || numThreads[2] > 1024) numThreads[2] = 1;
			}
			return true;
		}
		return false;
	}

	bool LocateMicrocode(const uint8_t* data, size_t size,
	                     const uint8_t*& progPtr, size_t& progBytes,
	                     Program& prog, std::string& error)
	{
		progPtr = nullptr;
		progBytes = 0;
		const uint8_t* part = nullptr;
		uint32_t partSize = 0;
		bool isNobc = false;
		if (!FindDxilPart(data, size, part, partSize, isNobc))
		{
			error = "no DXIL part";
			return false;
		}
		size_t startWord = 0;
		if (!LocateProgram(reinterpret_cast<const uint32_t*>(part), partSize / sizeof(uint32_t),
		                   startWord, prog, error))
		{
			return false;
		}
		if (prog.instructions.empty())
		{
			error = "empty program";
			return false;
		}
		const Instruction& last = prog.instructions.back();
		progBytes = last.pc + last.words.size() * 4;
		progPtr = part + startWord * sizeof(uint32_t);
		return true;
	}

	bool ParseSignature(const uint8_t* data, size_t size, const char* fourcc, std::vector<SigElem>& out)
	{
		out.clear();
		if (!data || size < 0x20 || memcmp(data, "DXBC", 4) != 0)
		{
			return false;
		}
		uint32_t partCount = *reinterpret_cast<const uint32_t*>(data + 0x1C);
		if (partCount > 64)
		{
			return false;
		}
		const uint32_t* offsets = reinterpret_cast<const uint32_t*>(data + 0x20);
		const uint8_t* end = data + size;
		if (reinterpret_cast<const uint8_t*>(offsets + partCount) > end)
		{
			return false;
		}
		for (uint32_t i = 0; i < partCount; ++i)
		{
			const uint8_t* partPtr = data + offsets[i];
			if (partPtr + 8 > end || memcmp(partPtr, fourcc, 4) != 0)
			{
				continue;
			}
			uint32_t declaredSize = *reinterpret_cast<const uint32_t*>(partPtr + 4);
			const uint8_t* chunkData = partPtr + 8;
			if (chunkData + 8 > end || chunkData + declaredSize > end)
			{
				return false;
			}
			uint32_t elementCount = *reinterpret_cast<const uint32_t*>(chunkData);
			uint32_t elementOffset = *reinterpret_cast<const uint32_t*>(chunkData + 4);
			if (elementCount > 64)
			{
				return false;
			}
			const uint8_t* element = chunkData + elementOffset;
			for (uint32_t k = 0; k < elementCount; ++k, element += 32)
			{
				if (element + 32 > chunkData + declaredSize)
				{
					return false;
				}
				const uint32_t* words = reinterpret_cast<const uint32_t*>(element);
				SigElem elem;
				uint32_t nameOffset = words[1];
				elem.semIdx = words[2];
				elem.sysval = words[3];
				elem.reg = words[5];
				elem.mask = words[6] & 0xFF;
				elem.rwMask = (words[6] >> 8) & 0xFF;
				const uint8_t* namePtr = chunkData + nameOffset;
				while (namePtr < chunkData + declaredSize && *namePtr)
				{
					elem.name += (char)*namePtr++;
				}
				out.push_back(std::move(elem));
			}
			return true;
		}
		return false;
	}

	bool PsInputLayout(const uint8_t* dxbc, size_t size, std::vector<std::pair<std::string, bool>>& out)
	{
		std::vector<SigElem> signature;
		if (!ParseSignature(dxbc, size, "ISG1", signature))
		{
			return false;
		}
		out.clear();
		for (const SigElem& elem : signature)
		{
			if (elem.sysval == 0)
			{
				out.push_back({ elem.name + std::to_string(elem.semIdx), elem.rwMask != 0 });
			}
		}
		return true;
	}

	bool VsParamSemantics(const uint8_t* dxbc, size_t size, std::vector<std::string>& out)
	{
		std::vector<SigElem> signature;
		if (!ParseSignature(dxbc, size, "OSG1", signature))
		{
			return false;
		}
		out.clear();
		for (const SigElem& elem : signature)
		{
			if (elem.sysval == 0)
			{
				out.push_back(elem.name + std::to_string(elem.semIdx));
			}
		}
		return true;
	}

	bool VsInputLayout(const uint8_t* dxbc, size_t size, std::vector<VsInputElem>& out)
	{
		std::vector<SigElem> signature;
		if (!ParseSignature(dxbc, size, "ISG1", signature))
		{
			return false;
		}
		out.clear();
		for (const SigElem& elem : signature)
		{
			if (elem.sysval != 0)
			{
				continue;
			}
			unsigned componentCount = 0;
			for (unsigned bit = 0; bit < 4; ++bit)
			{
				if (elem.mask & (1u << bit))
				{
					componentCount = bit + 1;
				}
			}
			out.push_back(VsInputElem{ elem.reg, elem.semIdx, componentCount ? componentCount : 4 });
		}
		return true;
	}
}
