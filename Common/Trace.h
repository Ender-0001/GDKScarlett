#pragma once
#include "Log.h"

#ifndef GDKS_TRACE_TAG
#define GDKS_TRACE_TAG "GDKScarlett"
#endif

#define TRACE() ::GDKScarlett::Log::Write(GDKS_TRACE_TAG, __func__)
#define LOGF(...) ::GDKScarlett::Log::WriteFormat(GDKS_TRACE_TAG, __VA_ARGS__)

#define TRACE_N(n)                                                                         \
	do                                                                                     \
	{                                                                                      \
		static volatile long traceCounter = 0;                                             \
		const long traceCount = InterlockedIncrement(&traceCounter);                       \
		if (traceCount <= (n))                                                             \
		{                                                                                  \
			::GDKScarlett::Log::WriteFormat(GDKS_TRACE_TAG, "%s [%ld]", __func__, traceCount); \
		}                                                                                  \
	} while (0)

#define TRACE_ONCE() TRACE_N(1)

#define TRACE_SAMPLED(first, every)                                                        \
	do                                                                                     \
	{                                                                                      \
		static volatile long traceCounter = 0;                                             \
		const long traceCount = InterlockedIncrement(&traceCounter);                       \
		if (traceCount <= (first) || (traceCount % (every)) == 0)                          \
		{                                                                                  \
			::GDKScarlett::Log::WriteFormat(GDKS_TRACE_TAG, "%s [%ld]", __func__, traceCount); \
		}                                                                                  \
	} while (0)
