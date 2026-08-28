#pragma once

#include "Gcn/GcnDecoder.h"
#include "Gcn/GcnHlsl.h"

#include <cstdint>
#include <string>
#include <vector>

namespace GDKScarlett::D3D12X
{
	struct SigElem
	{
		std::string name;
		uint32_t semIdx = 0;
		uint32_t sysval = 0;
		uint32_t reg = 0;
		uint32_t mask = 0;
		uint32_t rwMask = 0;
	};

	bool FindDxilPart(const uint8_t* data, size_t size,
	                  const uint8_t*& part, uint32_t& partSize, bool& isNobc);

	bool PsvInfo(const uint8_t* data, size_t size, uint32_t& shaderKind, uint32_t numThreads[3]);

	bool LocateMicrocode(const uint8_t* data, size_t size,
	                     const uint8_t*& progPtr, size_t& progBytes,
	                     Program& prog, std::string& error);

	bool ParseSignature(const uint8_t* data, size_t size, const char* fourcc, std::vector<SigElem>& out);

	bool PsInputLayout(const uint8_t* dxbc, size_t size, std::vector<std::pair<std::string, bool>>& out);

	bool VsParamSemantics(const uint8_t* dxbc, size_t size, std::vector<std::string>& out);

	bool VsInputLayout(const uint8_t* dxbc, size_t size, std::vector<VsInputElem>& out);
}
