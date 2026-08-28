#include "GcnHlslInternal.h"

#include <cstdlib>
#include <sstream>

namespace GDKScarlett::D3D12X
{
	std::string Trim(const std::string& text)
	{
		size_t first = text.find_first_not_of(" \t\r\n");
		if (first == std::string::npos)
		{
			return "";
		}
		size_t last = text.find_last_not_of(" \t\r\n");
		return text.substr(first, last - first + 1);
	}

	std::string BaseMnemonic(const std::string& mnemonic)
	{
		for (const char* suffix : { "_e32", "_e64" })
		{
			size_t at = mnemonic.rfind(suffix);
			if (at != std::string::npos && at == mnemonic.size() - 4)
			{
				return mnemonic.substr(0, at);
			}
		}
		return mnemonic;
	}

	// Handles the range forms "v[N:M]"/"s[N:M]" as well as "vN"/"sN"; atoi(op+1)
	// returns 0 for the bracket form and mis-bases every multi-VGPR result.
	int RegisterBase(const std::string& op)
	{
		if (op.size() < 2)
		{
			return 0;
		}
		const char* cursor = op.c_str() + 1;
		if (*cursor == '[')
		{
			++cursor;
		}
		return atoi(cursor);
	}

	// `exp target vsrc0, ...` puts the target and first source in one
	// space-separated field, and flags like done/compr/vm follow the last operand.
	bool LooksLikeOperand(const std::string& token)
	{
		if (token.empty() || token.find(':') != std::string::npos)
		{
			return false;
		}
		char lead = token[0];
		if ((lead == 'v' || lead == 's') && token.size() > 1 &&
		    (isdigit((unsigned char)token[1]) || token[1] == '['))
		{
			return true;
		}
		if (token.rfind("0x", 0) == 0)
		{
			return true;
		}
		if (isdigit((unsigned char)lead) || lead == '-')
		{
			return true;
		}
		if (token.rfind("attr", 0) == 0 || token.rfind("pos", 0) == 0 ||
		    token.rfind("param", 0) == 0 || token.rfind("mrt", 0) == 0)
		{
			return true;
		}
		if (token == "off" || token == "vcc" || token == "vcc_lo" || token == "exec" ||
		    token == "exec_lo" || token == "m0" || token == "scc")
		{
			return true;
		}
		return false;
	}

	bool ParseLine(const std::string& line, AsmInstruction& out)
	{
		std::string text = Trim(line);
		if (text.empty() || text[0] == '.' || text.back() == ':')
		{
			return false;
		}
		out = AsmInstruction{};

		size_t encoding = text.find("; encoding:");
		if (encoding != std::string::npos)
		{
			std::string tail = text.substr(encoding);
			int bytes = 0;
			for (size_t i = 0; i + 1 < tail.size(); ++i)
			{
				if (tail[i] == '0' && (tail[i + 1] == 'x' || tail[i + 1] == 'X'))
				{
					if (bytes < 4)
					{
						uint32_t byteValue = (uint32_t)strtoul(tail.c_str() + i, nullptr, 16);
						out.enc0 |= (byteValue & 0xFF) << (8 * bytes);
					}
					++bytes;
				}
			}
			if (bytes > 0)
			{
				out.width = bytes;
			}
			text = Trim(text.substr(0, encoding));
		}
		if (text.empty())
		{
			return false;
		}
		out.raw = text;

		size_t space = text.find_first_of(" \t");
		if (space == std::string::npos)
		{
			out.mnemonic = text;
			return true;
		}
		out.mnemonic = text.substr(0, space);

		std::vector<std::string> fields;
		{
			std::stringstream stream(Trim(text.substr(space)));
			std::string field;
			while (std::getline(stream, field, ','))
			{
				fields.push_back(Trim(field));
			}
		}
		for (const std::string& field : fields)
		{
			std::stringstream stream(field);
			std::string token;
			bool first = true;
			while (stream >> token)
			{
				if (first || LooksLikeOperand(token))
				{
					out.ops.push_back(token);
					first = false;
					continue;
				}
				size_t colon = token.find(':');
				if (colon != std::string::npos)
				{
					out.mods[token.substr(0, colon)] = token.substr(colon + 1);
				}
				else
				{
					out.mods[token] = "";
				}
			}
		}
		return true;
	}
}
