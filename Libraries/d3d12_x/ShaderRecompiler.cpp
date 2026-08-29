#define GDKS_TRACE_TAG "d3d12_x"
#include "Common.h"

#include "ShaderRecompiler.h"

#include "DxbcUtil.h"
#include "Gcn/GcnDecoder.h"
#include "ShaderHash.h"
#include "TranslatePipeline.h"

#include <windows.h>
#include <d3dcompiler.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <map>
#include <set>
#include <vector>
#include <string>

namespace GDKScarlett::D3D12X
{
	static const std::string& LocalStateDir()
	{
		static std::string dir = []
		{
			char localAppData[MAX_PATH];
			DWORD length = GetEnvironmentVariableA("LOCALAPPDATA", localAppData, sizeof(localAppData));
			if (length > 0 && length < sizeof(localAppData))
			{
				return std::string(localAppData) + "\\GDKScarlett";
			}
			return std::string(".");
		}();
		return dir;
	}

	static void EnsureDirectory(const std::string& path)
	{
		std::string partial;
		partial.reserve(path.size());
		for (size_t i = 0; i < path.size(); ++i)
		{
			partial += path[i];
			if (path[i] == '\\' && i > 2)
			{
				CreateDirectoryA(partial.c_str(), nullptr);
			}
		}
		CreateDirectoryA(path.c_str(), nullptr);
	}

	static const std::string& CacheDir()
	{
		static std::string dir = []
		{
			char override[MAX_PATH];
			DWORD length = GetEnvironmentVariableA("GDKS_SHADERCACHE", override, sizeof(override));
			std::string resolved = (length > 0 && length < sizeof(override))
			                           ? std::string(override)
			                           : LocalStateDir() + "\\ShaderCache";
			EnsureDirectory(resolved);
			return resolved;
		}();
		return dir;
	}

	static const char* StageName(UINT stageType)
	{
		switch (stageType)
		{
		case 1: return "VS";
		case 2: return "PS";
		case 3: return "DS";
		case 4: return "HS";
		case 5: return "GS";
		case 6: return "CS";
		default: return "??";
		}
	}

	static volatile LONG64 GLocateTicks = 0, GCacheIoTicks = 0, GCompileTicks = 0;
	static volatile LONG64 GTranslateTicks = 0, GAliasIoTicks = 0, GLinkFixTicks = 0;
	static volatile LONG64 GMaxLocateTicks = 0, GMaxCompileTicks = 0, GMaxTranslateTicks = 0;
	static volatile LONG GLocateCalls = 0, GLocateMemoHits = 0, GCacheIoCalls = 0, GCompileCalls = 0;
	static volatile LONG GTranslateCalls = 0, GAliasIoCalls = 0, GLinkFixCalls = 0;
	static volatile LONG GAliasHits = 0;

	static inline LONG64 Ticks()
	{
		LARGE_INTEGER t;
		QueryPerformanceCounter(&t);
		return t.QuadPart;
	}

	static void NoteMax(volatile LONG64* slot, LONG64 value)
	{
		for (;;)
		{
			LONG64 seen = *slot;
			if (value <= seen || InterlockedCompareExchange64(slot, value, seen) == seen)
			{
				break;
			}
		}
	}

	void GetRecompilerTimings(RecompilerTimings* out)
	{
		if (!out)
		{
			return;
		}
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		double toMs = freq.QuadPart ? 1000.0 / (double)freq.QuadPart : 0.0;
		out->locateMs = GLocateTicks * toMs;
		out->cacheIoMs = GCacheIoTicks * toMs;
		out->compileMs = GCompileTicks * toMs;
		out->locateCalls = GLocateCalls;
		out->locateMemoHits = GLocateMemoHits;
		out->cacheIoCalls = GCacheIoCalls;
		out->compileCalls = GCompileCalls;
		out->aliasHits = GAliasHits;
		out->scanAttempts = GScanAttempts;
		out->scanInstructions = GScanInstructions;
		out->translateMs = GTranslateTicks * toMs;
		out->aliasIoMs = GAliasIoTicks * toMs;
		out->linkFixMs = GLinkFixTicks * toMs;
		out->maxLocateMs = GMaxLocateTicks * toMs;
		out->maxCompileMs = GMaxCompileTicks * toMs;
		out->maxTranslateMs = GMaxTranslateTicks * toMs;
		out->translateCalls = GTranslateCalls;
		out->aliasIoCalls = GAliasIoCalls;
		out->linkFixCalls = GLinkFixCalls;
	}

	static volatile LONG GStageHit[8] = {}, GStageLive[8] = {}, GStagePlaceholder[8] = {},
	                     GStageUndecodable[8] = {};
	static volatile LONG GTranslateFail = 0, GFxcFail = 0;

	static std::map<std::string, LONG> GBlockingOpcodes;
	static std::map<std::string, LONG> GSeenOpcodes;
	static std::set<std::string> GCountedKeys;
	static SRWLOCK GOpcodeLock = SRWLOCK_INIT;

	static bool BenignOpcode(const std::string& opcode)
	{
		return opcode.rfind("s_load_dword", 0) == 0 || opcode == "s_nop";
	}

	static void NoteUnhandled(const std::string& key, const std::vector<std::string>& unhandled,
	                          bool blocked)
	{
		AcquireSRWLockExclusive(&GOpcodeLock);
		if (GCountedKeys.insert(key).second)
		{
			std::set<std::string> distinct;
			for (const std::string& opcode : unhandled)
			{
				if (!BenignOpcode(opcode))
				{
					distinct.insert(opcode);
				}
			}
			for (const std::string& opcode : distinct)
			{
				GSeenOpcodes[opcode] += 1;
				if (blocked)
				{
					GBlockingOpcodes[opcode] += 1;
				}
			}
		}
		ReleaseSRWLockExclusive(&GOpcodeLock);
	}

	static void AppendTop(std::string& line, const std::map<std::string, LONG>& counts, size_t limit)
	{
		std::vector<std::pair<LONG, std::string>> sorted;
		sorted.reserve(counts.size());
		for (const auto& entry : counts)
		{
			sorted.push_back({ entry.second, entry.first });
		}
		std::sort(sorted.begin(), sorted.end(),
		          [](const std::pair<LONG, std::string>& a, const std::pair<LONG, std::string>& b)
		          {
			          return a.first != b.first ? a.first > b.first : a.second < b.second;
		          });
		if (sorted.empty())
		{
			line += " (none)";
			return;
		}
		for (size_t i = 0; i < sorted.size() && i < limit; ++i)
		{
			line += " " + sorted[i].second + "=" + std::to_string(sorted[i].first);
		}
		if (sorted.size() > limit)
		{
			line += " (+" + std::to_string(sorted.size() - limit) + " more)";
		}
	}

	void LogRecompilerStats()
	{
		std::string stages;
		for (UINT s = 1; s <= 6; ++s)
		{
			if (!GStageHit[s] && !GStageLive[s] && !GStagePlaceholder[s] && !GStageUndecodable[s])
			{
				continue;
			}
			char part[128];
			wsprintfA(part, "%s%s hit=%ld live=%ld ph=%ld nodec=%ld", stages.empty() ? "" : " | ",
			          StageName(s), GStageHit[s], GStageLive[s], GStagePlaceholder[s],
			          GStageUndecodable[s]);
			stages += part;
		}
		LOGF("recompiler-stats: %s", stages.empty() ? "(no shaders seen)" : stages.c_str());

		AcquireSRWLockShared(&GOpcodeLock);
		std::map<std::string, LONG> blocking = GBlockingOpcodes;
		std::map<std::string, LONG> seen = GSeenOpcodes;
		size_t blockedShaders = GCountedKeys.size();
		ReleaseSRWLockShared(&GOpcodeLock);

		std::string line = "recompiler-stats: translateFail=" + std::to_string(GTranslateFail) +
		                   " fxcFail=" + std::to_string(GFxcFail) +
		                   " shadersWithUnhandled=" + std::to_string(blockedShaders) +
		                   " | BLOCKING:";
		AppendTop(line, blocking, 12);
		LOGF("%s", line.c_str());

		std::string line2 = "recompiler-stats: all unhandled (incl. non-blocking):";
		AppendTop(line2, seen, 12);
		LOGF("%s", line2.c_str());
	}

	static void WriteCacheFile(const std::string& path, const void* data, size_t size)
	{
		HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
		                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file != INVALID_HANDLE_VALUE)
		{
			DWORD written = 0;
			WriteFile(file, data, (DWORD)size, &written, nullptr);
			CloseHandle(file);
		}
	}

	static std::map<std::string, std::string> GAlias;
	static SRWLOCK GAliasLock = SRWLOCK_INIT;

	static std::string AliasFileName(const std::string& blobKey)
	{
		return blobKey + ".v" + std::to_string(CacheVersion) + ".alias";
	}

	static bool LookupAlias(const std::string& blobKey, std::string& microKey)
	{
		AcquireSRWLockShared(&GAliasLock);
		auto found = GAlias.find(blobKey);
		bool known = (found != GAlias.end());
		if (known)
		{
			microKey = found->second;
		}
		ReleaseSRWLockShared(&GAliasLock);
		if (known)
		{
			return !microKey.empty();
		}

		std::string value;
		std::string path = CacheDir() + "\\" + AliasFileName(blobKey);
		LONG64 ta = Ticks();
		HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
		                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file != INVALID_HANDLE_VALUE)
		{
			char buffer[17] = {};
			DWORD read = 0;
			if (ReadFile(file, buffer, 16, &read, nullptr) && read == 16)
			{
				value.assign(buffer, 16);
			}
			CloseHandle(file);
		}
		InterlockedAdd64(&GAliasIoTicks, Ticks() - ta);
		InterlockedIncrement(&GAliasIoCalls);

		AcquireSRWLockExclusive(&GAliasLock);
		GAlias.emplace(blobKey, value);
		ReleaseSRWLockExclusive(&GAliasLock);
		microKey = value;
		return !value.empty();
	}

	static void StoreAlias(const std::string& blobKey, const std::string& microKey)
	{
		if (microKey.size() != 16)
		{
			return;
		}
		bool isNew = false;
		AcquireSRWLockExclusive(&GAliasLock);
		auto found = GAlias.find(blobKey);
		if (found == GAlias.end() || found->second.empty())
		{
			GAlias[blobKey] = microKey;
			isNew = true;
		}
		ReleaseSRWLockExclusive(&GAliasLock);
		if (isNew)
		{
			WriteCacheFile(CacheDir() + "\\" + AliasFileName(blobKey), microKey.data(), 16);
		}
	}

	struct LocatedRec
	{
		bool ok;
		const uint8_t* prog;
		size_t progBytes;
		std::string key;
		size_t instrCount;
		size_t namedCount;
	};

	static std::map<std::pair<const void*, SIZE_T>, LocatedRec> GLocated;
	static SRWLOCK GLocatedLock = SRWLOCK_INIT;

	static bool LocateCached(const void* blob, SIZE_T length, LocatedRec& out)
	{
		if (!blob || !length)
		{
			return false;
		}
		auto id = std::make_pair(blob, length);
		AcquireSRWLockShared(&GLocatedLock);
		auto found = GLocated.find(id);
		bool known = (found != GLocated.end());
		if (known)
		{
			out = found->second;
		}
		ReleaseSRWLockShared(&GLocatedLock);
		if (known)
		{
			InterlockedIncrement(&GLocateMemoHits);
			return out.ok;
		}

		LocatedRec rec{};
		const uint8_t* prog = nullptr;
		size_t progBytes = 0;
		Program program;
		std::string error;
		LONG64 t0 = Ticks();
		rec.ok = LocateMicrocode(static_cast<const uint8_t*>(blob), length, prog, progBytes,
		                         program, error);
		LONG64 locateDelta = Ticks() - t0;
		InterlockedAdd64(&GLocateTicks, locateDelta);
		NoteMax(&GMaxLocateTicks, locateDelta);
		InterlockedIncrement(&GLocateCalls);
		if (rec.ok)
		{
			rec.prog = prog;
			rec.progBytes = progBytes;
			rec.key = HexKey(Fnv1a(prog, progBytes));
			rec.instrCount = program.instructions.size();
			rec.namedCount = program.instructions.size() - program.unknownCount;
		}
		else
		{
			rec.key = error;
		}

		AcquireSRWLockExclusive(&GLocatedLock);
		auto inserted = GLocated.emplace(id, rec);
		out = inserted.first->second;
		ReleaseSRWLockExclusive(&GLocatedLock);
		return out.ok;
	}

	static pD3DCompile LoadD3DCompile()
	{
		static pD3DCompile compile = []
		{
			HMODULE library = LoadLibraryA("d3dcompiler_47.dll");
			pD3DCompile entry = library ? (pD3DCompile)GetProcAddress(library, "D3DCompile") : nullptr;
			if (!entry)
			{
				LOGF("recompiler: d3dcompiler_47.dll unavailable - live compiler disabled");
			}
			return entry;
		}();
		return compile;
	}

	// The ID3DBlob is deliberately leaked so the pointer outlives every PSO.
	static bool CompileHlsl(const char* source, const char* target, D3D12_SHADER_BYTECODE* out)
	{
		pD3DCompile compile = LoadD3DCompile();
		if (!compile)
		{
			return false;
		}
		ID3DBlob* code = nullptr;
		ID3DBlob* errors = nullptr;
		HRESULT hr = compile(source, strlen(source), "gdks_recompiled", nullptr, nullptr,
		                     "main", target, 0, 0, &code, &errors);
		if (FAILED(hr))
		{
			LOGF("recompiler: HLSL %s compile failed 0x%08X: %s", target, (unsigned)hr,
			     errors ? (const char*)errors->GetBufferPointer() : "(no message)");
			if (errors)
			{
				errors->Release();
			}
			return false;
		}
		if (errors)
		{
			errors->Release();
		}
		out->pShaderBytecode = code->GetBufferPointer();
		out->BytecodeLength = code->GetBufferSize();
		return true;
	}

	// Placeholders valid against any root signature, input layout and RT format.
	// The VS must declare a superset of the interpolants a recompiled PS can
	// declare, or the VS<->PS link fails PSO creation.
	static bool EmitStageShader(UINT stageType, bool hasRenderTarget, D3D12_SHADER_BYTECODE* out)
	{
		switch (stageType)
		{
		case 1:
			return CompileHlsl(
				"struct VOut{float4 pos:SV_Position;float4 t0:TEXCOORD0;float4 t1:TEXCOORD1;"
				"float4 t2:TEXCOORD2;float4 t3:TEXCOORD3;float4 t4:TEXCOORD4;float4 t5:TEXCOORD5;"
				"float4 t6:TEXCOORD6;float4 t7:TEXCOORD7;};"
				"VOut main(uint vid:SV_VertexID){VOut o;uint i=vid%3;"
				"float2 p=float2(i==1?3.0:-1.0,i==2?3.0:-1.0);o.pos=float4(p,0,1);"
				"float4 uv=float4(p*0.5+0.5,0,1);"
				"o.t0=uv;o.t1=uv;o.t2=uv;o.t3=uv;o.t4=uv;o.t5=uv;o.t6=uv;o.t7=uv;return o;}",
				"vs_5_1", out);
		case 2:
			return hasRenderTarget
			           ? CompileHlsl("float4 main():SV_Target{return float4(1.0,0.0,1.0,1.0);}", "ps_5_1", out)
			           : CompileHlsl("void main(){}", "ps_5_1", out);
		case 5:
			return CompileHlsl(
				"struct V{float4 p:SV_Position;};[maxvertexcount(1)]"
				"void main(point V i[1],inout PointStream<V> s){s.Append(i[0]);}", "gs_5_1", out);
		case 6:
			return CompileHlsl("[numthreads(1,1,1)] void main(){}", "cs_5_1", out);
		default:
			return false;
		}
	}

	static bool StagePlaceholder(UINT stageType, bool hasRenderTarget, D3D12_SHADER_BYTECODE* out)
	{
		static D3D12_SHADER_BYTECODE cache[16] = {};
		static bool tried[16] = {};
		static SRWLOCK lock = SRWLOCK_INIT;
		if (stageType >= 8)
		{
			return false;
		}
		unsigned slot = stageType * 2u + (hasRenderTarget ? 1u : 0u);
		AcquireSRWLockExclusive(&lock);
		if (!tried[slot])
		{
			tried[slot] = true;
			EmitStageShader(stageType, hasRenderTarget, &cache[slot]);
		}
		D3D12_SHADER_BYTECODE bytecode = cache[slot];
		ReleaseSRWLockExclusive(&lock);
		if (!bytecode.pShaderBytecode)
		{
			return false;
		}
		*out = bytecode;
		return true;
	}

	static std::map<std::string, D3D12_SHADER_BYTECODE> GLoadedBlobs;
	static SRWLOCK GLoadedLock = SRWLOCK_INIT;

	static void StoreLoadedBlob(const std::string& key, void* buffer, size_t length)
	{
		AcquireSRWLockExclusive(&GLoadedLock);
		GLoadedBlobs[key] = { buffer, (SIZE_T)length };
		ReleaseSRWLockExclusive(&GLoadedLock);
	}

	static bool TryLoadCached(const std::string& key, D3D12_SHADER_BYTECODE* out)
	{
		AcquireSRWLockShared(&GLoadedLock);
		auto found = GLoadedBlobs.find(key);
		bool known = (found != GLoadedBlobs.end());
		D3D12_SHADER_BYTECODE bytecode = known ? found->second : D3D12_SHADER_BYTECODE{};
		ReleaseSRWLockShared(&GLoadedLock);
		if (known)
		{
			if (!bytecode.pShaderBytecode)
			{
				return false;
			}
			*out = bytecode;
			return true;
		}

		bytecode = D3D12_SHADER_BYTECODE{};
		std::string path = CacheDir() + "\\" + CacheFileName(key);
		LONG64 t0 = Ticks();
		HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
		                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		bool opened = (file != INVALID_HANDLE_VALUE);
		if (opened)
		{
			DWORD size = GetFileSize(file, nullptr);
			if (size != INVALID_FILE_SIZE && size > 0)
			{
				void* buffer = malloc(size);
				DWORD read = 0;
				if (buffer && ReadFile(file, buffer, size, &read, nullptr) && read == size)
				{
					bytecode.pShaderBytecode = buffer;
					bytecode.BytecodeLength = (SIZE_T)size;
				}
				else if (buffer)
				{
					free(buffer);
				}
			}
			CloseHandle(file);
		}
		InterlockedAdd64(&GCacheIoTicks, Ticks() - t0);
		InterlockedIncrement(&GCacheIoCalls);

		AcquireSRWLockExclusive(&GLoadedLock);
		auto inserted = GLoadedBlobs.emplace(key, bytecode);
		bool weLost = !inserted.second;
		D3D12_SHADER_BYTECODE winner = inserted.first->second;
		ReleaseSRWLockExclusive(&GLoadedLock);
		if (weLost)
		{
			if (bytecode.pShaderBytecode && bytecode.pShaderBytecode != winner.pShaderBytecode)
			{
				free(const_cast<void*>(bytecode.pShaderBytecode));
			}
			bytecode = winner;
		}

		static LONG loggedLookups = 0;
		if (InterlockedIncrement(&loggedLookups) <= 64)
		{
			LOGF("recompiler: cache lookup key=%s -> %s (%s, %u bytes)", key.c_str(),
			     bytecode.pShaderBytecode ? "HIT" : "miss",
			     opened ? "file opened" : "file NOT found", (unsigned)bytecode.BytecodeLength);
		}
		if (!bytecode.pShaderBytecode)
		{
			return false;
		}
		*out = bytecode;
		return true;
	}

	// Only substitute clean translations: a value-affecting unhandled opcode means
	// wrong pixels, and the placeholder is the safer degradation. s_load_* are
	// benign, being descriptor fetches the SMRD prepass already consumed.
	static bool TranslationIsClean(const TranslateOutput& output, std::string& offender)
	{
		for (const std::string& opcode : output.unhandled)
		{
			if (opcode.rfind("s_load_dword", 0) != 0 && opcode != "s_nop")
			{
				offender = "unhandled: " + opcode;
				return false;
			}
		}
		return true;
	}

	static bool LiveCompileShader(const uint8_t* blob, size_t length, UINT stageType, const std::string& key)
	{
		static std::set<std::string> failedKeys;
		static SRWLOCK failedLock = SRWLOCK_INIT;
		{
			AcquireSRWLockShared(&failedLock);
			bool alreadyFailed = failedKeys.count(key) != 0;
			ReleaseSRWLockShared(&failedLock);
			if (alreadyFailed)
			{
				return false;
			}
		}
		pD3DCompile compile = LoadD3DCompile();
		if (!compile)
		{
			return false;
		}

		TranslateOptions options;
		TranslateOutput translated;
		std::string error;
		LONG64 tt = Ticks();
		bool ok = TranslateContainer(blob, length,
		                             stageType == 1 ? 1 : stageType == 6 ? 6 : 2,
		                             options, translated, error);
		LONG64 translateDelta = Ticks() - tt;
		InterlockedAdd64(&GTranslateTicks, translateDelta);
		InterlockedIncrement(&GTranslateCalls);
		NoteMax(&GMaxTranslateTicks, translateDelta);
		bool translateBlocked = false;
		if (ok && !translated.ok && !TranslationIsClean(translated, error))
		{
			ok = false;
			translateBlocked = true;
		}
		if (!translated.unhandled.empty())
		{
			NoteUnhandled(key, translated.unhandled, translateBlocked);
		}
		if (!ok)
		{
			InterlockedIncrement(&GTranslateFail);
		}

		ID3DBlob* code = nullptr;
		ID3DBlob* errors = nullptr;
		HRESULT hr = E_FAIL;
		if (ok)
		{
			LONG64 tc = Ticks();
			hr = compile(translated.hlsl.data(), translated.hlsl.size(), key.c_str(), nullptr, nullptr,
			             "main", translated.target.c_str(), D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
			             &code, &errors);
			LONG64 compileDelta = Ticks() - tc;
			InterlockedAdd64(&GCompileTicks, compileDelta);
			NoteMax(&GMaxCompileTicks, compileDelta);
			InterlockedIncrement(&GCompileCalls);
		}
		if (!ok || FAILED(hr) || !code)
		{
			if (ok)
			{
				InterlockedIncrement(&GFxcFail);
			}
			static LONG loggedFailures = 0;
			if (InterlockedIncrement(&loggedFailures) <= 16)
			{
				LOGF("recompiler: LIVE compile FAILED key=%s hr=0x%08X translate=%s%s%s",
				     key.c_str(), (unsigned)hr, ok ? "ok" : error.c_str(),
				     (errors && errors->GetBufferSize()) ? " fxc: " : "",
				     (errors && errors->GetBufferSize()) ? (const char*)errors->GetBufferPointer() : "");
			}
			if (errors)
			{
				errors->Release();
			}
			if (code)
			{
				code->Release();
			}
			AcquireSRWLockExclusive(&failedLock);
			failedKeys.insert(key);
			ReleaseSRWLockExclusive(&failedLock);
			return false;
		}
		if (errors)
		{
			errors->Release();
		}

		size_t size = code->GetBufferSize();
		void* copy = malloc(size);
		if (!copy)
		{
			code->Release();
			return false;
		}
		memcpy(copy, code->GetBufferPointer(), size);
		code->Release();
		StoreLoadedBlob(key, copy, size);
		WriteCacheFile(CacheDir() + "\\" + CacheFileName(key), copy, size);

		if (!translated.ok)
		{
			static LONG loggedUnhandled = 0;
			if (InterlockedIncrement(&loggedUnhandled) <= 16)
			{
				std::string opcodes;
				for (const std::string& opcode : translated.unhandled)
				{
					opcodes += " ";
					opcodes += opcode;
				}
				LOGF("recompiler: LIVE key=%s has UNHANDLED opcodes:%s", key.c_str(), opcodes.c_str());
			}
		}
		return true;
	}

	// Keyed by the game's bytecode pointer and length, which are stable for the
	// life of a shader blob; the PSO path asks twice per shader.
	bool HasCacheEntry(const D3D12_SHADER_BYTECODE* input)
	{
		if (!input || !input->pShaderBytecode || !input->BytecodeLength)
		{
			return false;
		}
		static std::map<std::pair<const void*, SIZE_T>, bool> memo;
		static SRWLOCK memoLock = SRWLOCK_INIT;
		auto id = std::make_pair(input->pShaderBytecode, input->BytecodeLength);
		AcquireSRWLockShared(&memoLock);
		auto found = memo.find(id);
		bool known = (found != memo.end());
		bool substitutable = known ? found->second : false;
		ReleaseSRWLockShared(&memoLock);
		if (known)
		{
			return substitutable;
		}

		std::string microKey;
		if (LookupAlias(HexKey(Fnv1a(input->pShaderBytecode, input->BytecodeLength)), microKey))
		{
			InterlockedIncrement(&GAliasHits);
			substitutable = true;
		}
		else
		{
			LocatedRec located{};
			substitutable = LocateCached(input->pShaderBytecode, input->BytecodeLength, located);
		}

		AcquireSRWLockExclusive(&memoLock);
		memo[id] = substitutable;
		ReleaseSRWLockExclusive(&memoLock);
		return substitutable;
	}

	static unsigned FormatComps(DXGI_FORMAT format)
	{
		switch (format)
		{
		case DXGI_FORMAT_R32G32B32A32_TYPELESS: case DXGI_FORMAT_R32G32B32A32_FLOAT:
		case DXGI_FORMAT_R32G32B32A32_UINT:     case DXGI_FORMAT_R32G32B32A32_SINT:
		case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_UNORM:    case DXGI_FORMAT_R16G16B16A16_UINT:
		case DXGI_FORMAT_R16G16B16A16_SNORM:    case DXGI_FORMAT_R16G16B16A16_SINT:
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:     case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:   case DXGI_FORMAT_R8G8B8A8_UINT:
		case DXGI_FORMAT_R8G8B8A8_SNORM:        case DXGI_FORMAT_R8G8B8A8_SINT:
		case DXGI_FORMAT_B8G8R8A8_UNORM:        case DXGI_FORMAT_R10G10B10A2_UNORM:
		case DXGI_FORMAT_R10G10B10A2_UINT:      return 4;
		case DXGI_FORMAT_R32G32B32_TYPELESS:    case DXGI_FORMAT_R32G32B32_FLOAT:
		case DXGI_FORMAT_R32G32B32_UINT:        case DXGI_FORMAT_R32G32B32_SINT:
		case DXGI_FORMAT_R11G11B10_FLOAT:       return 3;
		case DXGI_FORMAT_R32G32_TYPELESS: case DXGI_FORMAT_R32G32_FLOAT:
		case DXGI_FORMAT_R32G32_UINT:     case DXGI_FORMAT_R32G32_SINT:
		case DXGI_FORMAT_R16G16_TYPELESS: case DXGI_FORMAT_R16G16_FLOAT:
		case DXGI_FORMAT_R16G16_UNORM:    case DXGI_FORMAT_R16G16_UINT:
		case DXGI_FORMAT_R16G16_SNORM:    case DXGI_FORMAT_R16G16_SINT:
		case DXGI_FORMAT_R8G8_TYPELESS:   case DXGI_FORMAT_R8G8_UNORM:
		case DXGI_FORMAT_R8G8_UINT:       case DXGI_FORMAT_R8G8_SNORM:
		case DXGI_FORMAT_R8G8_SINT:       return 2;
		case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_R32_UINT:     case DXGI_FORMAT_R32_SINT:
		case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_R16_FLOAT:
		case DXGI_FORMAT_R16_UNORM:    case DXGI_FORMAT_R16_UINT:
		case DXGI_FORMAT_R16_SNORM:    case DXGI_FORMAT_R16_SINT:
		case DXGI_FORMAT_R8_TYPELESS:  case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_R8_UINT:      case DXGI_FORMAT_R8_SNORM:
		case DXGI_FORMAT_R8_SINT:      return 1;
		default: return 4;
		}
	}

	bool TryLinkFixVs(const D3D12_SHADER_BYTECODE* vs, const D3D12_SHADER_BYTECODE* ps,
	                  const D3D12_INPUT_LAYOUT_DESC* inputLayout, D3D12_SHADER_BYTECODE* out)
	{
		struct LinkFixTimer
		{
			LONG64 start;
			LinkFixTimer() : start(Ticks()) {}
			~LinkFixTimer()
			{
				InterlockedAdd64(&GLinkFixTicks, Ticks() - start);
				InterlockedIncrement(&GLinkFixCalls);
			}
		} linkFixTimer;
		static LONG loggedBails = 0;
		auto bail = [&](const char* reason) -> bool
		{
			if (InterlockedIncrement(&loggedBails) <= 24)
			{
				LOGF("recompiler: LINKFIX bail: %s", reason);
			}
			return false;
		};
		if (out)
		{
			out->pShaderBytecode = nullptr;
			out->BytecodeLength = 0;
		}
		if (!vs || !vs->pShaderBytecode || !ps || !ps->pShaderBytecode || !out)
		{
			return bail("null in/out");
		}
		pD3DCompile compile = LoadD3DCompile();
		if (!compile)
		{
			return bail("no d3dcompiler");
		}

		LocatedRec vsLoc{}, psLoc{};
		if (!LocateCached(vs->pShaderBytecode, vs->BytecodeLength, vsLoc))
		{
			return bail("VS microcode not located");
		}
		const std::string& vsKey = vsLoc.key;
		if (!LocateCached(ps->pShaderBytecode, ps->BytecodeLength, psLoc))
		{
			return bail("PS microcode not located");
		}
		const std::string& psKey = psLoc.key;

		std::string layoutTag = "n";
		if (inputLayout && inputLayout->NumElements && inputLayout->pInputElementDescs)
		{
			layoutTag.clear();
			for (UINT i = 0; i < inputLayout->NumElements && i < 16; ++i)
			{
				layoutTag += std::to_string(inputLayout->pInputElementDescs[i].SemanticIndex);
			}
		}
		std::string pairKey = "linkfix_" + vsKey + "_" + psKey + "_" + layoutTag;

		{
			AcquireSRWLockShared(&GLoadedLock);
			auto found = GLoadedBlobs.find(pairKey);
			bool known = (found != GLoadedBlobs.end());
			D3D12_SHADER_BYTECODE bytecode = known ? found->second : D3D12_SHADER_BYTECODE{};
			ReleaseSRWLockShared(&GLoadedLock);
			if (known)
			{
				if (!bytecode.pShaderBytecode)
				{
					return false;
				}
				*out = bytecode;
				return true;
			}
		}

		std::string path = CacheDir() + "\\" + CacheFileName(pairKey);
		{
			HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
			                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file != INVALID_HANDLE_VALUE)
			{
				DWORD size = GetFileSize(file, nullptr);
				void* buffer = (size != INVALID_FILE_SIZE && size > 0) ? malloc(size) : nullptr;
				DWORD read = 0;
				bool ok = buffer && ReadFile(file, buffer, size, &read, nullptr) && read == size;
				CloseHandle(file);
				if (ok)
				{
					StoreLoadedBlob(pairKey, buffer, size);
					out->pShaderBytecode = buffer;
					out->BytecodeLength = size;
					return true;
				}
				if (buffer)
				{
					free(buffer);
				}
			}
		}

		std::vector<std::pair<std::string, bool>> psRows;
		if (!PsInputLayout((const uint8_t*)ps->pShaderBytecode, ps->BytecodeLength, psRows))
		{
			StoreLoadedBlob(pairKey, nullptr, 0);
			return bail("PS has no ISG1");
		}
		std::vector<std::string> rows;
		for (const auto& row : psRows)
		{
			rows.push_back(row.first);
		}

		std::vector<VsInputElem> layoutInputs;
		bool noInputs = !inputLayout || inputLayout->NumElements == 0 || !inputLayout->pInputElementDescs;
		if (!noInputs)
		{
			for (UINT i = 0; i < inputLayout->NumElements; ++i)
			{
				const D3D12_INPUT_ELEMENT_DESC& element = inputLayout->pInputElementDescs[i];
				// UE's GDK vertex factories use ATTRIBUTE<n> exclusively; anything else
				// is an unknown fetch convention.
				if (!element.SemanticName || _stricmp(element.SemanticName, "ATTRIBUTE") != 0)
				{
					StoreLoadedBlob(pairKey, nullptr, 0);
					return bail(element.SemanticName ? element.SemanticName : "null semantic");
				}
				layoutInputs.push_back(VsInputElem{ i, element.SemanticIndex, FormatComps(element.Format) });
			}
		}

		std::string error;
		TranslateOptions options;
		options.vsLinkOutputs = &rows;
		options.vsInOverride = layoutInputs.empty() ? nullptr : &layoutInputs;
		options.vsNoInputs = noInputs;
		TranslateOutput translated;
		LONG64 tt = Ticks();
		bool ok = TranslateContainer((const uint8_t*)vs->pShaderBytecode, vs->BytecodeLength,
		                             1, options, translated, error);
		LONG64 translateDelta = Ticks() - tt;
		InterlockedAdd64(&GTranslateTicks, translateDelta);
		InterlockedIncrement(&GTranslateCalls);
		NoteMax(&GMaxTranslateTicks, translateDelta);
		if (ok && !translated.ok && !TranslationIsClean(translated, error))
		{
			ok = false;
		}

		ID3DBlob* code = nullptr;
		ID3DBlob* errors = nullptr;
		HRESULT hr = E_FAIL;
		if (ok)
		{
			LONG64 tc = Ticks();
			hr = compile(translated.hlsl.data(), translated.hlsl.size(), pairKey.c_str(), nullptr, nullptr,
			             "main", "vs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
			LONG64 compileDelta = Ticks() - tc;
			InterlockedAdd64(&GCompileTicks, compileDelta);
			NoteMax(&GMaxCompileTicks, compileDelta);
			InterlockedIncrement(&GCompileCalls);
		}
		if (!ok || FAILED(hr) || !code)
		{
			static LONG loggedFailures = 0;
			if (InterlockedIncrement(&loggedFailures) <= 16)
			{
				LOGF("recompiler: LINKFIX FAILED %s hr=0x%08X translate=%s%s%s",
				     pairKey.c_str(), (unsigned)hr, ok ? "ok" : error.c_str(),
				     (errors && errors->GetBufferSize()) ? " fxc: " : "",
				     (errors && errors->GetBufferSize()) ? (const char*)errors->GetBufferPointer() : "");
			}
			if (errors)
			{
				errors->Release();
			}
			if (code)
			{
				code->Release();
			}
			StoreLoadedBlob(pairKey, nullptr, 0);
			return false;
		}
		if (errors)
		{
			errors->Release();
		}

		size_t size = code->GetBufferSize();
		void* copy = malloc(size);
		if (!copy)
		{
			code->Release();
			return false;
		}
		memcpy(copy, code->GetBufferPointer(), size);
		code->Release();
		StoreLoadedBlob(pairKey, copy, size);
		WriteCacheFile(path, copy, size);

		static LONG succeeded = 0;
		LONG count = InterlockedIncrement(&succeeded);
		if (count <= 16 || (count % 50) == 0)
		{
			LOGF("recompiler: LINKFIX ok %s (%zu bytes, %ld total)", pairKey.c_str(), size, count);
		}
		out->pShaderBytecode = copy;
		out->BytecodeLength = size;
		return true;
	}

	static thread_local char TPsoKeys[256];
	static thread_local int TPsoKeysLength;

	void BeginPsoKeyCapture()
	{
		TPsoKeys[0] = 0;
		TPsoKeysLength = 0;
	}

	const char* PsoKeyCapture()
	{
		return TPsoKeys;
	}

	static void NotePsoKey(const char* stage, const std::string& key)
	{
		if (TPsoKeysLength < 0 || TPsoKeysLength >= (int)sizeof(TPsoKeys) - 1)
		{
			return;
		}
		int written = _snprintf_s(TPsoKeys + TPsoKeysLength, sizeof(TPsoKeys) - TPsoKeysLength,
		                          _TRUNCATE, " %s:%s", stage, key.c_str());
		if (written > 0)
		{
			TPsoKeysLength += written;
		}
	}

	static bool IsSubstitutableStage(UINT stageType, bool hasRenderTarget)
	{
		return stageType == 1 || stageType == 6 || (stageType == 2 && hasRenderTarget);
	}

	bool TryRecompileToDxil(UINT stageType, bool hasRenderTarget,
	                        const D3D12_SHADER_BYTECODE* input, D3D12_SHADER_BYTECODE* output,
	                        bool allowCache)
	{
		if (output)
		{
			output->pShaderBytecode = nullptr;
			output->BytecodeLength = 0;
		}
		if (!input || !input->pShaderBytecode)
		{
			return false;
		}

		std::string blobKey = HexKey(Fnv1a(input->pShaderBytecode, input->BytecodeLength));
		std::string key;
		bool ok = LookupAlias(blobKey, key);
		if (ok)
		{
			InterlockedIncrement(&GAliasHits);
		}
		else
		{
			LocatedRec located{};
			ok = LocateCached(input->pShaderBytecode, input->BytecodeLength, located);
			if (ok)
			{
				key = located.key;
				StoreAlias(blobKey, key);
				static LONG loggedLocated = 0;
				if (InterlockedIncrement(&loggedLocated) <= 64)
				{
					LOGF("recompiler[%s hasRT=%d]: GCN microcode %zu bytes (%zu instrs, %u named) "
					     "key=%s blob=%s",
					     StageName(stageType), hasRenderTarget ? 1 : 0, located.progBytes,
					     located.instrCount, (unsigned)located.namedCount, key.c_str(),
					     blobKey.c_str());
				}
			}
			else
			{
				if (stageType < 8)
				{
					InterlockedIncrement(&GStageUndecodable[stageType]);
				}
				static LONG loggedUndecodable = 0;
				if (InterlockedIncrement(&loggedUndecodable) <= 8)
				{
					LOGF("recompiler[%s]: no decodable GCN stream (%s)", StageName(stageType),
					     located.key.c_str());
				}
			}
		}
		if (ok)
		{
			if (allowCache && IsSubstitutableStage(stageType, hasRenderTarget))
			{
				if (TryLoadCached(key, output))
				{
					if (stageType < 8)
					{
						InterlockedIncrement(&GStageHit[stageType]);
					}
					NotePsoKey(StageName(stageType), key);
					static LONG loggedSubstitutions = 0;
					if (InterlockedIncrement(&loggedSubstitutions) <= 8)
					{
						LOGF("recompiler[%s]: substituted cached recompiled shader (%zu bytes)",
						     StageName(stageType), (size_t)output->BytecodeLength);
					}
					return true;
				}
				if (LiveCompileShader(static_cast<const uint8_t*>(input->pShaderBytecode),
				                      input->BytecodeLength, stageType, key) &&
				    TryLoadCached(key, output))
				{
					if (stageType < 8)
					{
						InterlockedIncrement(&GStageLive[stageType]);
					}
					NotePsoKey(StageName(stageType), key);
					static LONG liveCompiled = 0;
					LONG count = InterlockedIncrement(&liveCompiled);
					if (count <= 16 || (count % 50) == 0)
					{
						LOGF("recompiler[%s]: LIVE-compiled and memoized %s (%ld total)",
						     StageName(stageType), key.c_str(), count);
					}
					return true;
				}
			}
		}

		if (StagePlaceholder(stageType, hasRenderTarget, output))
		{
			if (stageType < 8)
			{
				InterlockedIncrement(&GStagePlaceholder[stageType]);
			}
			static LONG loggedPlaceholders = 0;
			if (InterlockedIncrement(&loggedPlaceholders) <= 8)
			{
				LOGF("recompiler[%s]: no cache entry, emitted placeholder", StageName(stageType));
			}
			return true;
		}
		return false;
	}
}
