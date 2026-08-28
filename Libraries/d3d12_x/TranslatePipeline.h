#pragma once

#include "Gcn/GcnHlsl.h"

#include <cstdint>
#include <string>
#include <vector>

namespace GDKScarlett::D3D12X
{
	struct TranslateOptions
	{
		bool resourceFree = false;
		bool faithfulBranch = true;
		bool forceOrtho = false;
		bool matrixPos = false;

		const std::vector<std::string>* vsLinkOutputs = nullptr;
		const std::vector<VsInputElem>* vsInOverride = nullptr;
		bool vsNoInputs = false;
	};

	struct TranslateOutput
	{
		std::string key;
		std::string asmText;
		std::string hlsl;
		std::string target;
		bool ok = false;
		int instrs = 0;
		int samples = 0;
		std::vector<std::string> unhandled;
	};

	// stageHint: 0 = detect, 1 = VS, 2 = PS, 6 = CS.
	bool TranslateContainer(const uint8_t* blob, size_t len, int stageHint,
	                        const TranslateOptions& options, TranslateOutput& out, std::string& error);
}
