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
#include <map>
#include <set>
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

	static bool TryLoadCached(const uint8_t* prog, size_t progBytes, D3D12_SHADER_BYTECODE* out)
	{
		std::string key = HexKey(Fnv1a(prog, progBytes));

		AcquireSRWLockExclusive(&GLoadedLock);
		auto found = GLoadedBlobs.find(key);
		if (found != GLoadedBlobs.end())
		{
			D3D12_SHADER_BYTECODE bytecode = found->second;
			ReleaseSRWLockExclusive(&GLoadedLock);
			if (!bytecode.pShaderBytecode)
			{
				return false;
			}
			*out = bytecode;
			return true;
		}

		D3D12_SHADER_BYTECODE bytecode = {};
		std::string path = CacheDir() + "\\" + CacheFileName(key);
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
		GLoadedBlobs[key] = bytecode;
		ReleaseSRWLockExclusive(&GLoadedLock);

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
		bool ok = TranslateContainer(blob, length,
		                             stageType == 1 ? 1 : stageType == 6 ? 6 : 2,
		                             options, translated, error);
		if (ok && !translated.ok && !TranslationIsClean(translated, error))
		{
			ok = false;
		}

		ID3DBlob* code = nullptr;
		ID3DBlob* errors = nullptr;
		HRESULT hr = E_FAIL;
		if (ok)
		{
			hr = compile(translated.hlsl.data(), translated.hlsl.size(), key.c_str(), nullptr, nullptr,
			             "main", translated.target.c_str(), D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
			             &code, &errors);
		}
		if (!ok || FAILED(hr) || !code)
		{
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

		const uint8_t* prog = nullptr;
		size_t progBytes = 0;
		Program program;
		std::string error;
		substitutable = LocateMicrocode(static_cast<const uint8_t*>(input->pShaderBytecode),
		                                input->BytecodeLength, prog, progBytes, program, error);

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

		const uint8_t* vsProg = nullptr;
		size_t vsLength = 0;
		const uint8_t* psProg = nullptr;
		size_t psLength = 0;
		Program program;
		std::string error;
		if (!LocateMicrocode((const uint8_t*)vs->pShaderBytecode, vs->BytecodeLength,
		                     vsProg, vsLength, program, error))
		{
			return bail("VS microcode not located");
		}
		std::string vsKey = HexKey(Fnv1a(vsProg, vsLength));
		if (!LocateMicrocode((const uint8_t*)ps->pShaderBytecode, ps->BytecodeLength,
		                     psProg, psLength, program, error))
		{
			return bail("PS microcode not located");
		}
		std::string psKey = HexKey(Fnv1a(psProg, psLength));

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

		TranslateOptions options;
		options.vsLinkOutputs = &rows;
		options.vsInOverride = layoutInputs.empty() ? nullptr : &layoutInputs;
		options.vsNoInputs = noInputs;
		TranslateOutput translated;
		bool ok = TranslateContainer((const uint8_t*)vs->pShaderBytecode, vs->BytecodeLength,
		                             1, options, translated, error);
		if (ok && !translated.ok && !TranslationIsClean(translated, error))
		{
			ok = false;
		}

		ID3DBlob* code = nullptr;
		ID3DBlob* errors = nullptr;
		HRESULT hr = E_FAIL;
		if (ok)
		{
			hr = compile(translated.hlsl.data(), translated.hlsl.size(), pairKey.c_str(), nullptr, nullptr,
			             "main", "vs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
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

		const uint8_t* prog = nullptr;
		size_t progBytes = 0;
		Program program;
		std::string error;
		bool located = LocateMicrocode(static_cast<const uint8_t*>(input->pShaderBytecode),
		                               input->BytecodeLength, prog, progBytes, program, error);
		if (!located)
		{
			static LONG loggedUndecodable = 0;
			if (InterlockedIncrement(&loggedUndecodable) <= 8)
			{
				LOGF("recompiler[%s]: no decodable GCN stream (%s)", StageName(stageType), error.c_str());
			}
		}
		else
		{
			std::string key = HexKey(Fnv1a(prog, progBytes));
			static LONG loggedLocated = 0;
			if (InterlockedIncrement(&loggedLocated) <= 64)
			{
				LOGF("recompiler[%s hasRT=%d]: GCN microcode %zu bytes (%zu instrs, %u named) key=%s",
				     StageName(stageType), hasRenderTarget ? 1 : 0, progBytes, program.instructions.size(),
				     (unsigned)(program.instructions.size() - program.unknownCount), key.c_str());
			}

			if (allowCache && IsSubstitutableStage(stageType, hasRenderTarget))
			{
				if (TryLoadCached(prog, progBytes, output))
				{
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
				    TryLoadCached(prog, progBytes, output))
				{
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
