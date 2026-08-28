#include "TranslatePipeline.h"

#include "DxbcUtil.h"
#include "ShaderHash.h"

#include <map>

namespace GDKScarlett::D3D12X
{
	bool TranslateContainer(const uint8_t* blob, size_t len, int stageHint,
	                        const TranslateOptions& options, TranslateOutput& out, std::string& error)
	{
		const uint8_t* microcode = nullptr;
		size_t microcodeBytes = 0;
		Program program;
		if (!LocateMicrocode(blob, len, microcode, microcodeBytes, program, error))
		{
			return false;
		}
		out.key = HexKey(Fnv1a(microcode, microcodeBytes));

		for (const Instruction& instruction : program.instructions)
		{
			out.asmText += "\t" + FormatInstruction(instruction) + "\n";
		}

		uint32_t shaderKind = ~0u;
		uint32_t numThreads[3] = { 1, 1, 1 };
		PsvInfo(blob, len, shaderKind, numThreads);
		bool isCS = (stageHint == 6) || (stageHint == 0 && shaderKind == 5);
		bool isVS = !isCS && ((stageHint == 1) || (stageHint == 0 && shaderKind == 1) ||
		            (stageHint == 0 && shaderKind == ~0u &&
		             (out.asmText.find("exp pos") != std::string::npos ||
		              out.asmText.find("s_swappc") != std::string::npos)));

		// SMRD fields: op [26:22] sdst [21:15] sbase [14:9] imm [8] offset [7:0].
		std::map<unsigned, unsigned> descIndex;
		std::map<unsigned, std::string> sgprInit;
		std::map<unsigned, unsigned> vsharpIndex;
		bool litAttributable = false;
		bool hasSbufLoad = false;
		for (const Instruction& instruction : program.instructions)
		{
			if (instruction.encoding != Encoding::Smrd || instruction.words.empty())
			{
				continue;
			}
			uint32_t word = instruction.words[0];
			unsigned op = (word >> 22) & 0x1F;
			unsigned sdst = (word >> 15) & 0x7F;
			unsigned sbase = ((word >> 9) & 0x3F) * 2;
			unsigned imm = (word >> 8) & 1;
			unsigned off = word & 0xFF;
			if (op >= 8 && op <= 12)
			{
				hasSbufLoad = true;
			}
			if (!imm)
			{
				if (off == 0xFF && op >= 8 && op <= 12 && vsharpIndex.count(sbase))
				{
					litAttributable = true;
				}
				continue;
			}
			if (op == 3)
			{
				descIndex[sdst] = off / 8;
			}
			else if (op == 2)
			{
				descIndex[sdst] = off / 4;
				vsharpIndex[sdst] = off / 8;
			}
			unsigned count = 0;
			switch (op)
			{
			case 8:  count = 1;  break;
			case 9:  count = 2;  break;
			case 10: count = 4;  break;
			case 11: count = 8;  break;
			case 12: count = 16; break;
			default: continue;
			}
			unsigned cbufferIndex = 0;
			auto found = vsharpIndex.find(sbase);
			if (found != vsharpIndex.end() && found->second < 16)
			{
				cbufferIndex = found->second;
			}
			std::string arrayName = cbufferIndex ? "g_udata" + std::to_string(cbufferIndex) : "g_udata";
			static const char* swizzle = "xyzw";
			for (unsigned i = 0; i < count; ++i)
			{
				unsigned slot = off + i;
				if (sdst + i > 101 || slot / 4 >= 1024)
				{
					continue;
				}
				sgprInit[sdst + i] = arrayName + "[" + std::to_string(slot / 4) + "]." +
				                     std::string(1, swizzle[slot % 4]);
			}
		}

		bool vsOrtho = isVS && (options.forceOrtho ||
		               (sgprInit.empty() && hasSbufLoad && !litAttributable && !options.matrixPos));

		std::vector<std::pair<std::string, bool>> psIn;
		std::vector<std::string> vsSem;
		std::vector<VsInputElem> vsIn;
		bool havePsIn = !isVS && !isCS && PsInputLayout(blob, len, psIn);
		bool haveVsSem = isVS && VsParamSemantics(blob, len, vsSem);
		bool haveVsIn = isVS && VsInputLayout(blob, len, vsIn) && !vsIn.empty();
		if (isVS && !haveVsIn && options.vsInOverride && !options.vsInOverride->empty() && !options.vsNoInputs)
		{
			vsIn = *options.vsInOverride;
			haveVsIn = true;
		}

		Result result = TranslateAsmToHlsl(out.asmText,
		                                   isCS ? "cs_5_1" : isVS ? "vs_5_1" : "ps_5_1",
		                                   options.resourceFree, !options.faithfulBranch, isVS, vsOrtho,
		                                   havePsIn ? &psIn : nullptr,
		                                   haveVsSem ? &vsSem : nullptr,
		                                   (haveVsIn && !options.vsNoInputs) ? &vsIn : nullptr,
		                                   sgprInit.empty() ? nullptr : &sgprInit,
		                                   descIndex.empty() ? nullptr : &descIndex,
		                                   isCS, numThreads,
		                                   isVS ? options.vsLinkOutputs : nullptr,
		                                   isVS && options.vsNoInputs);
		out.hlsl = result.hlsl;
		out.target = result.target;
		out.ok = result.ok;
		out.instrs = result.instrs;
		out.samples = result.samples;
		out.unhandled = result.unhandled;
		return true;
	}
}
