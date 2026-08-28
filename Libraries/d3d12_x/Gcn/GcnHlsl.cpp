#include "GcnHlslInternal.h"

#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>

namespace GDKScarlett::D3D12X
{
	// SM5 has no umulhi and no 64-bit integers, so 32x32 -> high 32 goes through
	// a 16-bit split.
	static const char* kMulHi =
		"uint xc_mulhi(uint a, uint b) {\n"
		"    uint alo = a & 0xFFFFu, ahi = a >> 16, blo = b & 0xFFFFu, bhi = b >> 16;\n"
		"    uint t  = ahi * blo + ((alo * blo) >> 16);\n"
		"    uint t2 = alo * bhi + (t & 0xFFFFu);\n"
		"    return ahi * bhi + (t >> 16) + (t2 >> 16);\n"
		"}\n";

	static bool IsForwardCbranch(const std::string& mnemonic)
	{
		return mnemonic == "s_cbranch_vccnz" || mnemonic == "s_cbranch_vccz";
	}

	// Declared at the registers the microcode addresses, recovered from its own
	// descriptor fetches rather than from order of appearance.
	static std::string ResourceDecls(const Context& context)
	{
		std::string decls;
		for (const auto& texture : context.texSlots)
		{
			decls += texture.second + " tex" + std::to_string(texture.first) +
			         " : register(t" + std::to_string(texture.first) + ");\n";
		}
		for (int slot : context.bufSrvSlots)
		{
			if (!context.texSlots.count(slot))
			{
				decls += "Buffer<float4> srvbuf" + std::to_string(slot) +
				         " : register(t" + std::to_string(slot) + ");\n";
			}
		}
		for (int slot : context.rawSrvSlots)
		{
			if (!context.texSlots.count(slot) && !context.bufSrvSlots.count(slot))
			{
				decls += "ByteAddressBuffer rawsrv" + std::to_string(slot) +
				         " : register(t" + std::to_string(slot) + ");\n";
			}
		}
		for (int slot : context.sampSlots)
		{
			decls += "SamplerState samp" + std::to_string(slot) +
			         " : register(s" + std::to_string(slot) + ");\n";
		}
		return decls;
	}

	static std::string VgprDecls(const Context& context, const char* dummyName)
	{
		std::string decls = "    float ";
		bool first = true;
		for (int index : context.vgprUsed)
		{
			if (!first)
			{
				decls += ", ";
			}
			decls += "v" + std::to_string(index) + "=0.0";
			first = false;
		}
		if (first)
		{
			decls += std::string(dummyName) + "=0.0";
		}
		return decls + ";\n";
	}

	// Prefer the real constant-buffer slot recovered from the shader's own
	// s_buffer_load; that is how the transform matrix and the PS tints reach the
	// shader. Everything else falls back to the flat user-data guess.
	static std::string SgprSeeds(const Context& context)
	{
		std::string decls;
		for (int index : context.sgprUsed)
		{
			static const char* swizzle = "xyzw";
			auto found = context.sgprInit.find((unsigned)index);
			std::string seed = (found != context.sgprInit.end())
			                       ? found->second
			                       : ("g_udata[" + std::to_string(index / 4) + "]." +
			                          std::string(1, swizzle[index % 4]));
			decls += "    float s" + std::to_string(index) + " = " + seed + ";\n";
			decls += SgprShadowDecl(index, seed);
		}
		return decls;
	}

	static std::string PredicateDecls(const Context& context)
	{
		std::string decls;
		for (const std::string& name : context.predDecls)
		{
			decls += "    bool " + name + " = false;\n";
		}
		return decls;
	}

	Result TranslateAsmToHlsl(const std::string& asmText, const char* stageTarget,
	                          bool resourceFree, bool assumeBranchTaken,
	                          bool isVertex, bool vsOrthoPos,
	                          const std::vector<std::pair<std::string, bool>>* psInputs,
	                          const std::vector<std::string>* vsParamSem,
	                          const std::vector<VsInputElem>* vsInputs,
	                          const std::map<unsigned, std::string>* sgprInit,
	                          const std::map<unsigned, unsigned>* descIndex,
	                          bool isCompute,
	                          const unsigned* numThreads,
	                          const std::vector<std::string>* vsOutOverride,
	                          bool vsNoInputs)
	{
		Result result;
		result.target = stageTarget ? stageTarget : "ps_5_1";

		std::vector<AsmInstruction> instructions;
		{
			std::stringstream stream(asmText);
			std::string line;
			while (std::getline(stream, line))
			{
				AsmInstruction AsmInstruction;
				if (ParseLine(line, AsmInstruction))
				{
					instructions.push_back(AsmInstruction);
				}
			}
		}
		result.instrs = (int)instructions.size();

		{
			uint32_t pc = 0;
			for (AsmInstruction& AsmInstruction : instructions)
			{
				AsmInstruction.pc = pc;
				pc += (uint32_t)AsmInstruction.width;
			}
		}
		std::map<uint32_t, size_t> pcIndex;
		for (size_t i = 0; i < instructions.size(); ++i)
		{
			pcIndex[instructions[i].pc] = i;
		}

		Context context;
		context.resourceFree = resourceFree;
		context.assumeBranchTaken = assumeBranchTaken;
		context.isVertex = isVertex;
		context.vsOrthoPos = vsOrthoPos;
		context.isCompute = isCompute;
		context.vsNoInputs = isVertex && vsNoInputs;
		if (isVertex && vsOutOverride)
		{
			context.remapActive = true;
			// Export param N carries OSG1 semantic vsParamSem[N]; find it in the
			// PS-dictated row list, case-insensitively as D3D semantics are.
			if (vsParamSem)
			{
				for (int n = 0; n < (int)vsParamSem->size(); ++n)
				{
					for (int i = 0; i < (int)vsOutOverride->size(); ++i)
					{
						if (_stricmp((*vsParamSem)[n].c_str(), (*vsOutOverride)[i].c_str()) == 0)
						{
							context.paramRemap[n] = i;
							break;
						}
					}
				}
			}
			else
			{
				for (int n = 0; n < (int)vsOutOverride->size(); ++n)
				{
					context.paramRemap[n] = n;
				}
			}
		}
		if (isCompute && numThreads)
		{
			context.ntX = numThreads[0];
			context.ntY = numThreads[1];
			context.ntZ = numThreads[2];
		}
		if (vsInputs)
		{
			context.vsIn = *vsInputs;
			for (const VsInputElem& element : context.vsIn)
			{
				context.vsAvail[element.semIdx] = (element.comps ? (element.comps > 4 ? 4 : element.comps) : 4);
			}
		}
		if (sgprInit)
		{
			context.sgprInit = *sgprInit;
		}
		if (descIndex)
		{
			context.descIndex = *descIndex;
		}
		// Seed strings may reference g_udata<K>[N]: collect the Ks to declare and
		// the Ns to size by.
		for (const auto& seed : context.sgprInit)
		{
			const std::string& expr = seed.second;
			if (expr.rfind("g_udata", 0) != 0)
			{
				continue;
			}
			unsigned cbuffer = 0;
			if (expr.size() > 7 && isdigit((unsigned char)expr[7]))
			{
				cbuffer = (unsigned)atoi(expr.c_str() + 7);
				if (!cbuffer || cbuffer >= 16)
				{
					continue;
				}
				context.cbufUsed.insert(cbuffer);
			}
			size_t bracket = expr.find('[');
			if (bracket != std::string::npos)
			{
				int index = atoi(expr.c_str() + bracket + 1);
				int& maxSlot = context.cbufSize[cbuffer];
				if (index >= maxSlot)
				{
					maxSlot = index + 1;
				}
			}
		}

		// Reserve texture slots before emission: a buffer load must not claim a
		// t-register an image op needs later, and either can appear first.
		bool sysWritten[4] = { false, false, false, false };
		std::map<unsigned, unsigned> preTex;
		for (const AsmInstruction& AsmInstruction : instructions)
		{
			std::string mnemonic = BaseMnemonic(AsmInstruction.mnemonic);
			if (mnemonic == "s_load_dwordx8" && AsmInstruction.ops.size() >= 3 &&
			    AsmInstruction.ops[2].rfind("0x", 0) == 0)
			{
				preTex[(unsigned)RegisterBase(AsmInstruction.ops[0])] =
					(unsigned)strtoul(AsmInstruction.ops[2].c_str(), nullptr, 16) / 8;
			}
			if (mnemonic.rfind("image_", 0) == 0 && AsmInstruction.ops.size() > 2 &&
			    AsmInstruction.ops[2][0] == 's')
			{
				unsigned base = (unsigned)RegisterBase(AsmInstruction.ops[2]);
				auto live = preTex.find(base);
				if (live != preTex.end())
				{
					context.texReserved.insert((int)live->second);
				}
				else
				{
					auto found = context.descIndex.find(base);
					if (found != context.descIndex.end())
					{
						context.texReserved.insert((int)found->second);
					}
				}
			}
			// Flag v0 (VertexID) / v3 (InstanceID) reads that happen before any
			// write. Fetch attributes land at v4+ so they never mask these.
			if (isVertex)
			{
				auto markRegisters = [&](const std::string& op, bool isDst)
				{
					int first = -1, last = -1;
					if (op.size() >= 2 && op[0] == 'v' && isdigit((unsigned char)op[1]))
					{
						first = last = atoi(op.c_str() + 1);
					}
					else if (op.rfind("v[", 0) == 0)
					{
						first = atoi(op.c_str() + 2);
						size_t colon = op.find(':');
						last = (colon != std::string::npos) ? atoi(op.c_str() + colon + 1) : first;
					}
					if (first < 0)
					{
						return;
					}
					for (int reg = first; reg <= last && reg <= 63; ++reg)
					{
						if (isDst)
						{
							if (reg < 4)
							{
								sysWritten[reg] = true;
							}
						}
						else if (reg < 4 && !sysWritten[reg])
						{
							if (reg == 0)
							{
								context.vsReadsVid = true;
							}
							if (reg == 3)
							{
								context.vsReadsIid = true;
							}
						}
					}
				};
				bool dstFirst = mnemonic.rfind("v_", 0) == 0 || mnemonic.rfind("buffer_load", 0) == 0 ||
				                mnemonic.rfind("tbuffer_load", 0) == 0 || mnemonic.rfind("ds_read", 0) == 0 ||
				                mnemonic.rfind("image_load", 0) == 0 || mnemonic.rfind("image_sample", 0) == 0 ||
				                mnemonic.rfind("image_gather", 0) == 0;
				for (size_t index = (dstFirst ? 1 : 0); index < AsmInstruction.ops.size(); ++index)
				{
					markRegisters(AsmInstruction.ops[index], false);
				}
				if (dstFirst && !AsmInstruction.ops.empty())
				{
					markRegisters(AsmInstruction.ops[0], true);
				}
			}
			if (isCompute && mnemonic.rfind("ds_add", 0) == 0)
			{
				context.ldsShared = true;
			}
			if (!isVertex && !isCompute && (mnemonic == "exp" || mnemonic == "export") &&
			    !AsmInstruction.ops.empty())
			{
				const std::string& target = AsmInstruction.ops[0];
				if (target.rfind("mrt", 0) == 0 && target != "mrtz" && target.size() > 3 &&
				    isdigit((unsigned char)target[3]))
				{
					context.mrtUsed.insert(atoi(target.c_str() + 3));
				}
			}
		}
		context.mrtMulti = context.mrtUsed.size() > 1;
		if (context.vsReadsVid)
		{
			context.vgprUsed.insert(0);
		}
		if (context.vsReadsIid)
		{
			context.vgprUsed.insert(3);
		}

		// A forward s_cbranch to target T guards instructions [branch+width, T):
		// they run only when the branch is NOT taken, so open an `if` at the branch
		// and close it on reaching T. Backward and unrecognized targets fall through.
		std::vector<uint32_t> closeAt;
		for (size_t i = 0; i < instructions.size(); ++i)
		{
			AsmInstruction& AsmInstruction = instructions[i];
			while (!closeAt.empty() && closeAt.back() == AsmInstruction.pc)
			{
				closeAt.pop_back();
				if (context.indent > 0)
				{
					context.indent--;
				}
				Emit(context, "}");
			}
			std::string mnemonic = BaseMnemonic(AsmInstruction.mnemonic);
			if (IsForwardCbranch(mnemonic) && !AsmInstruction.ops.empty())
			{
				long simm = atol(AsmInstruction.ops[0].c_str());
				uint32_t target = AsmInstruction.pc + (uint32_t)AsmInstruction.width + (uint32_t)(simm * 4);
				if (simm > 0 && pcIndex.count(target))
				{
					std::string condition;
					if (context.assumeBranchTaken)
					{
						condition = "false";
					}
					else if (mnemonic == "s_cbranch_vccnz")
					{
						condition = "!(" + context.vccExpr + ")";
					}
					else
					{
						condition = context.vccExpr;
					}
					EmitComment(context, AsmInstruction.raw +
					                         (context.assumeBranchTaken
					                              ? "  (forward branch: assuming TAKEN, block skipped)"
					                              : "  (forward branch: reconstructed, block runs when not taken)"));
					Emit(context, "if (" + condition + ") {");
					context.indent++;
					closeAt.push_back(target);
					continue;
				}
			}
			Translate(context, AsmInstruction);
		}
		while (!closeAt.empty())
		{
			closeAt.pop_back();
			if (context.indent > 0)
			{
				context.indent--;
			}
			Emit(context, "}");
		}

		result.samples = context.sampleCount;
		{
			std::set<std::string> distinct(context.unhandled.begin(), context.unhandled.end());
			result.unhandled.assign(distinct.begin(), distinct.end());
		}
		result.ok = result.unhandled.empty();

		std::string hlsl;
		if (context.isCompute)
		{
			hlsl += "// Auto-generated by gcn_hlsl (CS, mechanical GCN -> HLSL).\n\n";
			for (const auto& uav : context.uavSlots)
			{
				hlsl += uav.second + " uav" + std::to_string(uav.first) +
				        " : register(u" + std::to_string(uav.first) + ");\n";
			}
			for (int slot : context.uavBufSlots)
			{
				if (!context.uavSlots.count(slot))
				{
					hlsl += "RWBuffer<float4> buf" + std::to_string(slot) +
					        " : register(u" + std::to_string(slot) + ");\n";
				}
			}
			for (int slot : context.rawUavSlots)
			{
				if (!context.uavSlots.count(slot) && !context.uavBufSlots.count(slot))
				{
					hlsl += "RWByteAddressBuffer rawuav" + std::to_string(slot) +
					        " : register(u" + std::to_string(slot) + ");\n";
				}
			}
			hlsl += ResourceDecls(context);
			hlsl += "cbuffer CSUserData : register(b0) { float4 g_udata[" +
			        std::to_string(UserDataSize(context)) + "]; };\n" + ExtraCbufferDecls(context, "CS") + "\n";
			// Per-thread LDS is deliberately not groupshared: fxc rejects writes it
			// cannot prove disjoint (X3695). 1024 elements, not the hardware's 4096,
			// because an indexable temp this size hits fxc's temp cap.
			if (context.usesLds)
			{
				hlsl += context.ldsShared ? "groupshared uint xc_lds[1024];\n"
				                          : "static uint xc_lds[1024];\n";
			}
			if (context.needMulHi)
			{
				hlsl += kMulHi;
			}
			if (context.usesLds || context.needMulHi)
			{
				hlsl += "\n";
			}
			hlsl += "[numthreads(" + std::to_string(context.ntX) + "," + std::to_string(context.ntY) + "," +
			        std::to_string(context.ntZ) + ")]\n";
			hlsl += "void main(uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID) {\n";
			if (context.ldsShared)
			{
				// Strided by flat thread id so the zero-init is provably disjoint.
				unsigned threads = context.ntX * context.ntY * context.ntZ;
				if (!threads)
				{
					threads = 1;
				}
				hlsl += "    uint xc_shx = 0;\n";
				hlsl += "    { uint xf = gtid.x + gtid.y*" + std::to_string(context.ntX) +
				        "u + gtid.z*" + std::to_string(context.ntX * context.ntY) + "u;\n";
				hlsl += "      for (uint xi = xf; xi < 1024u; xi += " + std::to_string(threads) +
				        "u) xc_lds[xi] = 0u; }\n";
				hlsl += "    GroupMemoryBarrierWithGroupSync();\n";
			}
			hlsl += VgprDecls(context, "_vd");
			hlsl += VgprShadowDecls(context);
			hlsl += SgprSeeds(context);
			// Not a plain asfloat(gtid.x): fxc folds asuint(asfloat(<int expr>)) to
			// literal zero, which made every compute shader run as thread 0 of group
			// 0. A dynamically-indexed temp breaks that fold and forces real codegen.
			{
				bool anySeed = false;
				for (int k = 0; k < 3; ++k)
				{
					if (context.vgprUsed.count(k) || context.sgprUsed.count(7 + k))
					{
						anySeed = true;
					}
				}
				if (anySeed)
				{
					hlsl += "    uint xc_sv[2];\n";
				}
				for (int k = 0; k < 3; ++k)
				{
					const char* component = k == 0 ? "x" : k == 1 ? "y" : "z";
					if (context.vgprUsed.count(k))
					{
						hlsl += std::string("    xc_sv[gtid.") + component + " & 1u] = gtid." + component +
						        "; v" + std::to_string(k) + " = asfloat(xc_sv[gtid." + component + " & 1u]); v" +
						        std::to_string(k) + "i = gtid." + component + ";\n";
					}
					if (context.sgprUsed.count(7 + k))
					{
						hlsl += std::string("    xc_sv[gid.") + component + " & 1u] = gid." + component +
						        "; s" + std::to_string(7 + k) + " = asfloat(xc_sv[gid." + component + " & 1u]); s" +
						        std::to_string(7 + k) + "i = gid." + component + ";\n";
					}
				}
			}
			hlsl += "    bool vcc = false;\n";
			hlsl += PredicateDecls(context);
			hlsl += "\n";
			hlsl += context.body;
			hlsl += "}\n";
			result.hlsl = hlsl;
			result.target = "cs_5_1";
			return result;
		}

		if (context.isVertex)
		{
			hlsl += "// Auto-generated by gcn_hlsl (VS, mechanical GCN -> HLSL).\n";
			hlsl += "// Vertex input = FSlateVertex; SGPRs seeded from a user-data cbuffer so\n";
			hlsl += "// live projection constants flow in (in-shader writes still override).\n\n";
			if (!context.vsNoInputs)
			{
				hlsl += "struct VSIn {\n";
				if (!context.vsIn.empty())
				{
					static const char* kTypes[5] = { "float4", "float", "float2", "float3", "float4" };
					for (const VsInputElem& element : context.vsIn)
					{
						unsigned comps = element.comps ? (element.comps > 4 ? 4 : element.comps) : 4;
						hlsl += std::string("    ") + kTypes[comps] + " attr" + std::to_string(element.semIdx) +
						        " : ATTRIBUTE" + std::to_string(element.semIdx) + ";\n";
					}
				}
				else
				{
					hlsl += "    float4 attr0 : ATTRIBUTE0;\n    float2 attr1 : ATTRIBUTE1;\n"
					        "    float2 attr2 : ATTRIBUTE2;\n    float4 attr3 : ATTRIBUTE3;\n"
					        "    float4 attr4 : ATTRIBUTE4;\n    uint2  attr5 : ATTRIBUTE5;\n";
				}
				hlsl += "};\n\n";
			}
			hlsl += ResourceDecls(context);
			if (!context.texSlots.empty() || !context.bufSrvSlots.empty() || !context.rawSrvSlots.empty())
			{
				hlsl += "\n";
			}
			hlsl += "cbuffer VSUserData : register(b0) { float4 g_udata[" +
			        std::to_string(UserDataSize(context)) + "]; };\n" + ExtraCbufferDecls(context, "VS") + "\n";
			if (context.needMulHi)
			{
				hlsl += kMulHi + std::string("\n");
			}
			hlsl += "struct VSOut {\n    float4 pos : SV_Position;\n";
			// Declare a row for every OSG1 semantic, not just the exported params: an
			// export inside a skipped branch would otherwise leave a row undeclared
			// and desktop rejects the PSO on the VS->PS linkage. Unexported rows stay
			// zero from the (VSOut)0 init.
			int paramCount = context.maxParam + 1;
			if (context.remapActive && vsOutOverride)
			{
				paramCount = (int)vsOutOverride->size();
				for (int n = 0; n < paramCount; ++n)
				{
					hlsl += "    float4 param" + std::to_string(n) + " : " + (*vsOutOverride)[n] + ";\n";
				}
			}
			else
			{
				if (vsParamSem && (int)vsParamSem->size() > paramCount)
				{
					paramCount = (int)vsParamSem->size();
				}
				for (int n = 0; n < paramCount; ++n)
				{
					std::string semantic = (vsParamSem && n < (int)vsParamSem->size())
					                           ? (*vsParamSem)[n]
					                           : ("TEXCOORD" + std::to_string(n));
					hlsl += "    float4 param" + std::to_string(n) + " : " + semantic + ";\n";
				}
			}
			hlsl += "};\n\n";
			{
				std::string args = context.vsNoInputs ? "" : "VSIn vin";
				if (context.vsReadsVid)
				{
					if (!args.empty())
					{
						args += ", ";
					}
					args += "uint xc_vid : SV_VertexID";
				}
				if (context.vsReadsIid)
				{
					if (!args.empty())
					{
						args += ", ";
					}
					args += "uint xc_iid : SV_InstanceID";
				}
				hlsl += "VSOut main(" + args + ") {\n";
			}
			hlsl += VgprDecls(context, "_vd");
			hlsl += VgprShadowDecls(context);
			hlsl += SgprSeeds(context);
			hlsl += "    bool vcc = false;\n    VSOut o = (VSOut)0;\n";
			hlsl += PredicateDecls(context);
			// Same fxc bitcast fold as the CS launch IDs, so route through a temp.
			if (context.vsReadsVid || context.vsReadsIid)
			{
				hlsl += "    uint xc_sv[2];\n";
				if (context.vsReadsVid)
				{
					hlsl += "    xc_sv[xc_vid & 1u] = xc_vid; v0 = asfloat(xc_sv[xc_vid & 1u]); v0i = xc_vid;\n";
				}
				if (context.vsReadsIid)
				{
					hlsl += "    xc_sv[xc_iid & 1u] = xc_iid; v3 = asfloat(xc_sv[xc_iid & 1u]); v3i = xc_iid;\n";
				}
			}
			hlsl += "\n";
			hlsl += context.body;
			if (context.vsOrthoPos && !context.vsNoInputs)
			{
				// FSlateVertex carries position in ATTRIBUTE2, but a smaller signature
				// may not declare it at all, so fall back to the first 2-component
				// attribute, which is the position slot in the slimmer quad layouts.
				std::string positionAttr = "attr2";
				if (!context.vsAvail.empty() && context.vsAvail.find(2) == context.vsAvail.end())
				{
					positionAttr.clear();
					for (const auto& available : context.vsAvail)
					{
						if (available.second >= 2)
						{
							positionAttr = "attr" + std::to_string(available.first);
							break;
						}
					}
				}
				if (!positionAttr.empty())
				{
					hlsl += "\n    // DIAGNOSTIC: position via a Slate pixel->NDC ortho on the position\n";
					hlsl += "    // attribute, bypassing the b0 matrix (user-data offsets unresolved).\n";
					hlsl += "    o.pos = float4(vin." + positionAttr + ".x * (2.0/1920.0) - 1.0, 1.0 - vin." +
					        positionAttr + ".y * (2.0/1080.0), 0.0, 1.0);\n";
				}
			}
			hlsl += "    return o;\n}\n";
			result.hlsl = hlsl;
			return result;
		}

		hlsl += "// Auto-generated by gcn_hlsl (mechanical GCN -> HLSL).\n\n";
		hlsl += "struct PSIn {\n    float4 pos : SV_Position;\n";
		std::map<int, std::string> attrFieldSem;
		if (psInputs)
		{
			// Declare every non-SV row, read or not, so the input registers line up
			// with the VS outputs; microcode attrs map to the read rows in order.
			int attr = 0;
			for (size_t i = 0; i < psInputs->size(); ++i)
			{
				hlsl += "    float4 in_row" + std::to_string(i) + "_v : " + (*psInputs)[i].first + ";\n";
				if ((*psInputs)[i].second)
				{
					attrFieldSem[attr++] = "in_row" + std::to_string(i) + "_v";
				}
			}
		}
		else
		{
			int semantic = 0;
			for (const auto& attribute : context.attrMaxComp)
			{
				int comps = attribute.second + 1;
				const char* type = comps == 1 ? "float" : comps == 2 ? "float2" : comps == 3 ? "float3" : "float4";
				hlsl += "    " + std::string(type) + " in_attr" + std::to_string(attribute.first) +
				        "_v : TEXCOORD" + std::to_string(semantic++) + ";\n";
			}
		}
		hlsl += "};\n\n";
		hlsl += ResourceDecls(context);
		if (!context.texSlots.empty() || !context.bufSrvSlots.empty() || !context.rawSrvSlots.empty())
		{
			hlsl += "\n";
		}
		if (!context.resourceFree || !context.sgprInit.empty())
		{
			hlsl += "cbuffer PSUserData : register(b0) { float4 g_udata[" +
			        std::to_string(UserDataSize(context)) + "]; };\n" + ExtraCbufferDecls(context, "PS") + "\n";
		}
		if (context.needMulHi)
		{
			hlsl += kMulHi + std::string("\n");
		}

		if (context.mrtMulti)
		{
			hlsl += "struct PSOut {\n";
			for (int target : context.mrtUsed)
			{
				hlsl += "    float4 o" + std::to_string(target) + " : SV_Target" + std::to_string(target) + ";\n";
			}
			hlsl += "};\n\n";
			hlsl += "PSOut main(PSIn input) {\n";
		}
		else
		{
			hlsl += "float4 main(PSIn input) : SV_Target {\n";
		}
		for (const auto& attribute : context.attrMaxComp)
		{
			if (psInputs)
			{
				auto field = attrFieldSem.find(attribute.first);
				std::string source = (field != attrFieldSem.end())
				                         ? ("input." + field->second)
				                         : "float4(0,0,0,0)";
				hlsl += "    float4 in_attr" + std::to_string(attribute.first) + " = " + source + ";\n";
			}
			else
			{
				int comps = attribute.second + 1;
				const char* swizzle = comps == 1 ? "x" : comps == 2 ? "xy" : comps == 3 ? "xyz" : "xyzw";
				hlsl += "    float4 in_attr" + std::to_string(attribute.first) + " = float4(0,0,0,0); in_attr" +
				        std::to_string(attribute.first) + "." + swizzle + " = input.in_attr" +
				        std::to_string(attribute.first) + "_v;\n";
			}
		}
		hlsl += VgprDecls(context, "_vdummy");
		hlsl += VgprShadowDecls(context);
		if (context.resourceFree && context.sgprInit.empty())
		{
			// Nothing is known about what the shader multiplies by. Zero is the
			// historical default and is exactly why UI panels came out black.
			hlsl += "    float ";
			{
				bool first = true;
				for (int index : context.sgprUsed)
				{
					if (!first)
					{
						hlsl += ", ";
					}
					hlsl += "s" + std::to_string(index) + "=0.0";
					first = false;
				}
				if (first)
				{
					hlsl += "_sdummy=0.0";
				}
				hlsl += ";\n";
			}
			for (int index : context.sgprUsed)
			{
				hlsl += SgprShadowDecl(index, "0.0");
			}
		}
		else
		{
			hlsl += SgprSeeds(context);
			if (context.sgprUsed.empty())
			{
				hlsl += "    float _sdummy=0.0;\n";
			}
		}
		hlsl += "    bool vcc = false;\n";
		hlsl += PredicateDecls(context);
		if (context.mrtMulti)
		{
			for (int target : context.mrtUsed)
			{
				hlsl += "    float4 out" + std::to_string(target) + " = float4(0,0,0,1);\n";
			}
			hlsl += "\n";
			hlsl += context.body;
			hlsl += "    PSOut _po;\n";
			for (int target : context.mrtUsed)
			{
				hlsl += "    _po.o" + std::to_string(target) + " = out" + std::to_string(target) + ";\n";
			}
			hlsl += "    return _po;\n}\n";
		}
		else
		{
			hlsl += "    float4 out_color = float4(0,0,0,1);\n\n";
			hlsl += context.body;
			hlsl += "    return out_color;\n}\n";
		}

		result.hlsl = hlsl;
		return result;
	}
}
