#pragma once

#include "GcnHlsl.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace GDKScarlett::D3D12X
{
	struct AsmInstruction
	{
		std::string mnemonic;
		std::vector<std::string> ops;
		std::map<std::string, std::string> mods;
		std::string raw;
		int width = 4;
		uint32_t pc = 0;
		uint32_t enc0 = 0;    // first encoding dword, for the Xbox-custom re-decode
	};

	struct Context
	{
		std::set<int> vgprUsed, sgprUsed;
		std::map<int, int> attrMaxComp;
		std::map<int, std::pair<std::string, std::string>> packedPairs;
		std::string body;
		std::vector<std::string> unhandled;

		std::string currentPredicate;
		bool outClamp = false;
		const char* outMul = nullptr;

		// Predicates are hoisted to function scope: they are written inside a
		// reconstructed `if` and read after it closes.
		std::vector<std::string> predDecls;
		std::set<std::string> condVars;
		std::map<std::string, std::pair<std::string, std::string>> execSaves;
		int predCounter = 0;

		std::vector<std::string> textures;
		std::map<int, std::string> texSlots;
		std::set<int> sampSlots;

		// SGPR -> descriptor index from the shader's own fetches: s_load_dwordx8
		// means t(off/8), and samplers are 4 dwords, so s(off/4).
		std::map<unsigned, unsigned> descIndex;

		// Program-order slots. descIndex keeps only the last value per register, so
		// a register reused for two descriptors resolves wrongly without these.
		std::map<unsigned, unsigned> texSlotLive, sampSlotLive;
		int sampleCount = 0;

		std::map<unsigned, unsigned> cbufIndex;
		std::set<unsigned> cbufUsed;
		std::map<unsigned, int> cbufSize;
		std::set<int> texReserved;

		std::vector<VsInputElem> vsIn;
		std::map<unsigned, unsigned> vsAvail;
		std::map<unsigned, std::string> sgprInit;

		bool resourceFree = false;
		bool assumeBranchTaken = true;
		std::string vccExpr = "vcc";
		int indent = 0;

		bool isVertex = false;
		bool isCompute = false;
		unsigned ntX = 1, ntY = 1, ntZ = 1;
		std::map<int, std::string> uavSlots;
		std::set<int> uavBufSlots;
		std::set<int> bufSrvSlots;
		std::set<int> rawSrvSlots;
		std::set<int> rawUavSlots;

		bool needMulHi = false;
		bool usesLds = false;
		// Real groupshared plus atomics, required for ds_add; per-thread LDS stays
		// the default for the plain read/write class.
		bool ldsShared = false;

		bool vsOrthoPos = false;
		std::array<std::string, 4> posOut = { "0", "0", "0", "1" };
		bool hasPos = false;
		std::map<int, std::array<std::string, 4>> params;
		int maxParam = -1;

		bool mrtMulti = false;
		std::set<int> mrtUsed;

		bool remapActive = false;
		std::map<int, int> paramRemap;

		bool vsNoInputs = false;
		bool vsReadsVid = false, vsReadsIid = false;

		// fxc flushes a denormal-range asfloat literal to 0.0 before a later
		// asint/asuint can recover the bits, so hex-literal movs are tracked and
		// substituted directly into integer reads.
		std::map<int, uint32_t> vKnownLit, sKnownLit;
		std::set<std::string> predTrue;

		bool shadowDone = false;
	};

	std::string Trim(const std::string& text);
	std::string BaseMnemonic(const std::string& mnemonic);
	int RegisterBase(const std::string& op);
	bool LooksLikeOperand(const std::string& token);
	bool ParseLine(const std::string& line, AsmInstruction& out);

	std::string HexToFloatLit(uint32_t bits);

	inline const char* ComponentName(int component)
	{
		static const char* names[4] = { "x", "y", "z", "w" };
		return names[component & 3];
	}

	inline int ComponentIndex(char component)
	{
		switch (component)
		{
		case 'x': return 0;
		case 'y': return 1;
		case 'z': return 2;
		case 'w': return 3;
		}
		return 0;
	}

	inline bool IsNumeric(const std::string& text)
	{
		return !text.empty() && isdigit((unsigned char)text[0]);
	}

	inline std::string Pad(const Context& context)
	{
		return std::string(4 + context.indent * 4, ' ');
	}

	std::string SourceExpr(Context& context, const std::string& op);
	std::string SourceExprUint(Context& context, const std::string& op);
	std::string SourceExprInt(Context& context, const std::string& op);
	std::string DestName(Context& context, const std::string& op);
	std::string ShadowName(Context& context, const std::string& op);

	std::string ExtraCbufferDecls(const Context& context, const char* stage);
	int UserDataSize(const Context& context);
	std::string ConditionName(Context& context, const std::string& token);
	std::string MaskExpr(Context& context, const std::string& token);
	int BufferSlotFor(Context& context, const std::string& srsrc);
	std::string VgprShadowDecls(const Context& context);
	std::string SgprShadowDecl(int index, const std::string& seedExpr);
	std::string IntifyExpr(const std::string& expr);
	bool ParseRegisterWrite(const std::string& line, std::string& dst, std::string& rhs);

	void EmitRaw(Context& context, const std::string& line);
	void Emit(Context& context, const std::string& line);
	void EmitComment(Context& context, const std::string& comment);
	std::string ApplyOutputMods(Context& context, const std::string& rhs);
	void EmitAssign(Context& context, const std::string& dst, const std::string& rhs);
	void EmitAccum(Context& context, const std::string& dst, const std::string& op, const std::string& rhs);
	void EmitCondition(Context& context, const std::string& dstToken, const std::string& rhs);

	void Translate(Context& context, const AsmInstruction& AsmInstruction);
}
