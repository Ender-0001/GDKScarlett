#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace GDKScarlett::D3D12X
{
	// One row of a vertex shader's real input signature (ISG1). The fetch shader
	// writes attribute `reg`'s components into VGPRs [4 + 4*reg .. 7 + 4*reg].
	struct VsInputElem
	{
		unsigned reg;
		unsigned semIdx;
		unsigned comps;
	};

	struct Result
	{
		std::string hlsl;
		std::string target;
		std::vector<std::string> unhandled;
		int instrs = 0;
		int samples = 0;
		bool ok = false;
	};

	Result TranslateAsmToHlsl(const std::string& asmText, const char* stageTarget,
	                          bool resourceFree = false, bool assumeBranchTaken = true,
	                          bool isVertex = false, bool vsOrthoPos = false,
	                          const std::vector<std::pair<std::string, bool>>* psInputs = nullptr,
	                          const std::vector<std::string>* vsParamSem = nullptr,
	                          const std::vector<VsInputElem>* vsInputs = nullptr,
	                          const std::map<unsigned, std::string>* sgprInit = nullptr,
	                          const std::map<unsigned, unsigned>* descIndex = nullptr,
	                          bool isCompute = false,
	                          const unsigned* numThreads = nullptr,
	                          const std::vector<std::string>* vsOutOverride = nullptr,
	                          bool vsNoInputs = false);
}
