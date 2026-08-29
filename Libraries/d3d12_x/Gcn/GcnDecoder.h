#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace GDKScarlett::D3D12X
{
	enum class Encoding
	{
		Unknown,
		Sop1, Sop2, Sopc, Sopp, Sopk,   // scalar ALU / control
		Smrd, Smem,                     // scalar memory
		Vop1, Vop2, Vopc, Vop3, Vop3p,  // vector ALU
		Vintrp,                         // parameter interpolation
		Ds,                             // LDS/GDS
		Mubuf, Mtbuf, Mimg,             // vector memory: buffer / typed buffer / image
		Flat,
		Exp,
	};

	const char* EncodingName(Encoding encoding);

	struct Operand
	{
		enum class Kind { None, Vgpr, Sgpr, Special, InlineConst, Literal };
		Kind kind = Kind::None;
		int reg = 0;
		int count = 1;         // consecutive registers: v[1:4] -> 4
		uint32_t bits = 0;     // Literal: the 32-bit value
		float fval = 0.0f;     // InlineConst: the float it denotes
		bool neg = false;
		bool abs = false;
		std::string text;
	};

	struct Instruction
	{
		uint32_t pc = 0;               // byte offset from the program start
		Encoding encoding = Encoding::Unknown;
		std::string name;              // empty when the opcode is unrecognized
		std::vector<uint32_t> words;
		std::vector<Operand> ops;
		std::vector<std::pair<std::string, std::string>> mods;
		bool clamp = false;
		unsigned omod = 0;             // 0=none, 1=*2, 2=*4, 3=/2
	};

	std::string FormatInstruction(const Instruction& instruction);

	struct Program
	{
		uint64_t address = 0;
		std::vector<Instruction> instructions;
		bool terminated = false;       // reached s_endpgm cleanly
		uint32_t unknownCount = 0;
	};

	bool DecodeProgram(const uint32_t* words, size_t wordCount, Program& out, std::string& error,
	                   uint64_t baseAddress = 0);

	extern volatile LONG GScanAttempts;
	extern volatile LONG64 GScanInstructions;

	bool LocateProgram(const uint32_t* words, size_t wordCount,
	                   size_t& startWord, Program& out, std::string& error);
}
