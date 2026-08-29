#include "Log.h"

#include <stdarg.h>
#include <stdio.h>

namespace GDKScarlett::Log
{
	static void DefaultSink(const char* line)
	{
		OutputDebugStringA(line);
	}

	static void (*GSink)(const char*) = DefaultSink;

	void SetSink(void (*sink)(const char* line))
	{
		GSink = sink;
	}

	void Write(const char* tag, const char* text)
	{
		if (!GSink)
		{
			return;
		}
		char line[640];
		_snprintf_s(line, sizeof(line), _TRUNCATE, "[%s] %s\n", tag, text ? text : "");
		GSink(line);
	}

	void WriteFormat(const char* tag, const char* format, ...)
	{
		char body[512];
		va_list args;
		va_start(args, format);
		_vsnprintf_s(body, sizeof(body), _TRUNCATE, format, args);
		va_end(args);
		Write(tag, body);
	}
}
