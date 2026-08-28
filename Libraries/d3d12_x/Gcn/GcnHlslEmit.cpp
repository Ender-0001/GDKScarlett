#include "GcnHlslInternal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace GDKScarlett::D3D12X
{
	std::string HexToFloatLit(uint32_t bits)
	{
		char buffer[64];
		snprintf(buffer, sizeof(buffer), "asfloat(0x%08xu)", bits);
		return buffer;
	}

	std::string SourceExpr(Context& context, const std::string& op)
	{
		if (op.empty())
		{
			return "0.0";
		}
		// VOP3 source modifiers: "-x", "|x|", "-|x|".
		if (op[0] == '-' && op.size() > 1 && (op[1] == 'v' || op[1] == 's' || op[1] == '|'))
		{
			return "(-" + SourceExpr(context, op.substr(1)) + ")";
		}
		if (op[0] == '|' && op.size() > 2 && op.back() == '|')
		{
			return "abs(" + SourceExpr(context, op.substr(1, op.size() - 2)) + ")";
		}
		if (op[0] == 'v' && op.size() > 1 && isdigit((unsigned char)op[1]))
		{
			int index = atoi(op.c_str() + 1);
			context.vgprUsed.insert(index);
			return "v" + std::to_string(index);
		}
		if (op[0] == 's' && op.size() > 1 && isdigit((unsigned char)op[1]))
		{
			int index = atoi(op.c_str() + 1);
			context.sgprUsed.insert(index);
			return "s" + std::to_string(index);
		}
		if (op.rfind("0x", 0) == 0)
		{
			return HexToFloatLit((uint32_t)strtoul(op.c_str(), nullptr, 16));
		}
		if (op == "vcc" || op == "vcc_lo")
		{
			return "(vcc?1.0:0.0)";
		}
		if (op == "exec" || op == "exec_lo")
		{
			return "1.0";
		}
		if (op.find('.') != std::string::npos)
		{
			return op;
		}
		if (op == "0")
		{
			return "0.0";
		}
		if (IsNumeric(op) || (op[0] == '-' && op.size() > 1))
		{
			return op + ".0";
		}
		return "0.0";
	}

	std::string DestName(Context& context, const std::string& op)
	{
		if (op[0] == 'v')
		{
			int index = atoi(op.c_str() + 1);
			context.vgprUsed.insert(index);
			return "v" + std::to_string(index);
		}
		if (op[0] == 's')
		{
			int index = atoi(op.c_str() + 1);
			context.sgprUsed.insert(index);
			return "s" + std::to_string(index);
		}
		return op;
	}

	// Integer reads come from the register's shadow, never from asuint(vN): a
	// register holding a small integer is a denormal in the float model, and
	// denormals read back as zero.
	std::string ShadowName(Context& context, const std::string& op)
	{
		if (op.size() > 1 && (op[0] == 'v' || op[0] == 's') && isdigit((unsigned char)op[1]))
		{
			int index = atoi(op.c_str() + 1);
			if (op[0] == 'v')
			{
				context.vgprUsed.insert(index);
				return "v" + std::to_string(index) + "i";
			}
			context.sgprUsed.insert(index);
			return "s" + std::to_string(index) + "i";
		}
		return std::string();
	}

	// A literal operand stays a literal: SourceExpr renders "7" as 7.0, and
	// asuint(7.0) is 0x40E00000, so the shift amount would be 0 instead of 7.
	std::string SourceExprUint(Context& context, const std::string& op)
	{
		if (op.rfind("0x", 0) == 0)
		{
			return op + "u";
		}
		if (IsNumeric(op) && op.find('.') == std::string::npos)
		{
			return op + "u";
		}
		if (op.size() > 1 && op[0] == '-' && isdigit((unsigned char)op[1]) &&
		    op.find('.') == std::string::npos)
		{
			return "(uint)(" + op + ")";
		}
		std::string shadow = ShadowName(context, op);
		if (!shadow.empty())
		{
			return shadow;
		}
		return "asuint(" + SourceExpr(context, op) + ")";
	}

	std::string SourceExprInt(Context& context, const std::string& op)
	{
		if (op.rfind("0x", 0) == 0)
		{
			return "(int)" + op + "u";
		}
		if ((IsNumeric(op) || (op.size() > 1 && op[0] == '-' && isdigit((unsigned char)op[1]))) &&
		    op.find('.') == std::string::npos)
		{
			return op;
		}
		std::string shadow = ShadowName(context, op);
		if (!shadow.empty())
		{
			return "(int)" + shadow;
		}
		return "asint(" + SourceExpr(context, op) + ")";
	}

	std::string ExtraCbufferDecls(const Context& context, const char* stage)
	{
		std::string decls;
		for (unsigned index : context.cbufUsed)
		{
			int count = 64;
			auto found = context.cbufSize.find(index);
			if (found != context.cbufSize.end() && found->second > count)
			{
				count = found->second;
			}
			if (count > 1024)
			{
				count = 1024;
			}
			decls += "cbuffer " + std::string(stage) + "UserData" + std::to_string(index) +
			         " : register(b" + std::to_string(index) + ") { float4 g_udata" +
			         std::to_string(index) + "[" + std::to_string(count) + "]; };\n";
		}
		return decls;
	}

	// Sized to what the shader actually indexes: declaring past the real CBV
	// blacked out every draw.
	int UserDataSize(const Context& context)
	{
		int need = context.sgprInit.empty() ? 8 : 64;
		if (context.sgprInit.empty())
		{
			for (int index : context.sgprUsed)
			{
				int slot = index / 4 + 1;
				if (slot > need && slot <= 64)
				{
					need = slot;
				}
			}
		}
		auto found = context.cbufSize.find(0u);
		if (found != context.cbufSize.end() && found->second > need)
		{
			need = found->second;
		}
		return need > 1024 ? 1024 : need;
	}

	// VOP3 compares write an explicit SGPR pair, so collapsing every condition
	// onto one global `vcc` breaks whenever two are live at once.
	std::string ConditionName(Context& context, const std::string& token)
	{
		if (token.empty() || token == "vcc" || token == "vcc_lo")
		{
			return "vcc";
		}
		if (token == "scc")
		{
			if (!context.condVars.count("scc"))
			{
				context.condVars.insert("scc");
				context.predDecls.push_back("scc");
			}
			return "scc";
		}
		if (token == "-1")
		{
			return "true";
		}
		if (token == "0")
		{
			return "false";
		}
		if (token[0] != 's')
		{
			return "vcc";
		}
		std::string name = "cond" + std::to_string(RegisterBase(token));
		if (!context.condVars.count(name))
		{
			context.condVars.insert(name);
			context.predDecls.push_back(name);
		}
		return name;
	}

	// A mask read in a value position: exec is the active predicate, a register
	// holding a saved exec is the predicate it captured.
	std::string MaskExpr(Context& context, const std::string& token)
	{
		if (token == "exec" || token == "exec_lo")
		{
			return context.currentPredicate.empty() ? "true" : context.currentPredicate;
		}
		auto saved = context.execSaves.find(token);
		if (saved != context.execSaves.end())
		{
			return saved->second.first;
		}
		return ConditionName(context, token);
	}

	int BufferSlotFor(Context& context, const std::string& srsrc)
	{
		auto live = context.cbufIndex.find((unsigned)RegisterBase(srsrc));
		if (live != context.cbufIndex.end())
		{
			return (int)live->second;
		}
		auto found = context.descIndex.find((unsigned)RegisterBase(srsrc));
		if (found != context.descIndex.end())
		{
			return (int)found->second / 2;   // off/4 -> off/8
		}
		return -1;
	}

	std::string VgprShadowDecls(const Context& context)
	{
		if (context.vgprUsed.empty())
		{
			return std::string();
		}
		std::string decls = "    uint ";
		bool first = true;
		for (int index : context.vgprUsed)
		{
			if (!first)
			{
				decls += ", ";
			}
			decls += "v" + std::to_string(index) + "i=0u";
			first = false;
		}
		return decls + ";\n";
	}

	// asuint of the seed expression, not of the float local: a constant like
	// SOAStride=256 is itself a denormal bit pattern.
	std::string SgprShadowDecl(int index, const std::string& seedExpr)
	{
		return "    uint s" + std::to_string(index) + "i = asuint(" + seedExpr + ");\n";
	}

	// `asfloat(X)` unwraps to X, so the bitcast round-trip this mechanism exists
	// to avoid is never re-created.
	std::string IntifyExpr(const std::string& expr)
	{
		if (expr.rfind("asfloat(", 0) == 0 && !expr.empty() && expr.back() == ')')
		{
			int depth = 0;
			bool wraps = true;
			for (size_t i = 7; i < expr.size(); ++i)
			{
				if (expr[i] == '(')
				{
					++depth;
				}
				else if (expr[i] == ')')
				{
					if (--depth == 0 && i != expr.size() - 1)
					{
						wraps = false;
						break;
					}
				}
			}
			if (wraps)
			{
				return "(uint)(" + expr.substr(8, expr.size() - 9) + ")";
			}
		}
		return "asuint(" + expr + ")";
	}

	bool ParseRegisterWrite(const std::string& line, std::string& dst, std::string& rhs)
	{
		if (line.size() < 5 || (line[0] != 'v' && line[0] != 's') || !isdigit((unsigned char)line[1]))
		{
			return false;
		}
		size_t at = 1;
		while (at < line.size() && isdigit((unsigned char)line[at]))
		{
			++at;
		}
		if (at + 3 >= line.size() || line[at] != ' ' || line[at + 1] != '=' || line[at + 2] != ' ')
		{
			return false;
		}
		if (line.back() != ';')
		{
			return false;
		}
		dst = line.substr(0, at);
		rhs = line.substr(at + 3, line.size() - (at + 3) - 1);
		return true;
	}

	void EmitRaw(Context& context, const std::string& line)
	{
		context.body += Pad(context) + line + "\n";
	}

	void Emit(Context& context, const std::string& line)
	{
		std::string text = line;

		// Replace asint(vN)/asuint(vN) with the tracked literal bits.
		auto substitute = [&text](char registerClass, int index, uint32_t bits)
		{
			char from[2][24], to[2][24];
			_snprintf_s(from[0], sizeof(from[0]), _TRUNCATE, "asuint(%c%d)", registerClass, index);
			_snprintf_s(to[0], sizeof(to[0]), _TRUNCATE, "0x%08xu", bits);
			_snprintf_s(from[1], sizeof(from[1]), _TRUNCATE, "asint(%c%d)", registerClass, index);
			_snprintf_s(to[1], sizeof(to[1]), _TRUNCATE, "asint(0x%08xu)", bits);
			for (int k = 0; k < 2; ++k)
			{
				size_t at = 0;
				while ((at = text.find(from[k], at)) != std::string::npos)
				{
					size_t end = at + strlen(from[k]);
					text = text.substr(0, at) + to[k] + text.substr(end);
					at += strlen(to[k]);
				}
			}
		};
		for (const auto& known : context.vKnownLit)
		{
			substitute('v', known.first, known.second);
		}
		for (const auto& known : context.sKnownLit)
		{
			substitute('s', known.first, known.second);
		}

		// Any write invalidates the tracked literal; a recording mov re-adds it.
		if (text.size() > 3 && (text[0] == 'v' || text[0] == 's') && isdigit((unsigned char)text[1]))
		{
			size_t at = 1;
			while (at < text.size() && isdigit((unsigned char)text[at]))
			{
				++at;
			}
			if (at + 2 < text.size() && text[at] == ' ' && text[at + 1] == '=')
			{
				int index = atoi(text.c_str() + 1);
				if (text[0] == 'v')
				{
					context.vKnownLit.erase(index);
				}
				else
				{
					context.sKnownLit.erase(index);
				}
			}
		}

		context.body += Pad(context) + text + "\n";

		// Mirror register writes made through raw Emit into the integer shadow.
		if (!context.shadowDone)
		{
			std::string dst, rhs;
			if (ParseRegisterWrite(text, dst, rhs))
			{
				context.body += Pad(context) + dst + "i = " + IntifyExpr(rhs) + ";\n";
			}
		}
		context.shadowDone = false;
	}

	void EmitComment(Context& context, const std::string& comment)
	{
		context.body += Pad(context) + "// " + comment + "\n";
	}

	// The hardware scales by omod first, then clamps.
	std::string ApplyOutputMods(Context& context, const std::string& rhs)
	{
		std::string result = rhs;
		if (context.outMul)
		{
			result = "(" + result + ") * " + context.outMul;
		}
		if (context.outClamp)
		{
			result = "saturate(" + result + ")";
		}
		return result;
	}

	void EmitAssign(Context& context, const std::string& dst, const std::string& rhs)
	{
		std::string value = ApplyOutputMods(context, rhs);
		const bool isRegister = dst.size() > 1 && (dst[0] == 'v' || dst[0] == 's') &&
		                        isdigit((unsigned char)dst[1]);
		context.shadowDone = isRegister;
		if (context.currentPredicate.empty())
		{
			Emit(context, dst + " = " + value + ";");
		}
		else
		{
			Emit(context, dst + " = (" + context.currentPredicate + ") ? (" + value + ") : " + dst + ";");
		}
		if (!isRegister)
		{
			return;
		}
		// The shadow takes the same source expression; reading back the float
		// register is the denormal-flush hazard.
		std::string shadow = IntifyExpr(value);
		if (context.currentPredicate.empty())
		{
			EmitRaw(context, dst + "i = " + shadow + ";");
		}
		else
		{
			EmitRaw(context, dst + "i = (" + context.currentPredicate + ") ? (" + shadow + ") : " + dst + "i;");
		}
	}

	void EmitAccum(Context& context, const std::string& dst, const std::string& op, const std::string& rhs)
	{
		if (context.currentPredicate.empty())
		{
			Emit(context, dst + " " + op + "= " + rhs + ";");
		}
		else
		{
			Emit(context, dst + " = (" + context.currentPredicate + ") ? (" + dst + " " + op + " " + rhs + ") : " + dst + ";");
		}
	}

	// Every write inside an exec-guarded region must be gated on its predicate:
	// the hardware skips the whole block via s_cbranch_execz.
	void EmitCondition(Context& context, const std::string& dstToken, const std::string& rhs)
	{
		std::string dst = ConditionName(context, dstToken);
		context.execSaves.erase(dstToken);
		if (context.currentPredicate.empty())
		{
			Emit(context, dst + " = " + rhs + ";");
		}
		else
		{
			Emit(context, dst + " = (" + context.currentPredicate + ") ? (" + rhs + ") : " + dst + ";");
		}
	}
}
