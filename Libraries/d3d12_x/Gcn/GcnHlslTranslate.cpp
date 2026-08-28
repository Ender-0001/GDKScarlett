#include "GcnHlslInternal.h"

#include <algorithm>
#include <cstdlib>

namespace GDKScarlett::D3D12X
{
	static unsigned VectorCountSuffix(const std::string& mnemonic)
	{
		if (mnemonic.rfind("x16") == mnemonic.size() - 3)
		{
			return 16;
		}
		if (mnemonic.rfind("x8") == mnemonic.size() - 2)
		{
			return 8;
		}
		if (mnemonic.rfind("x4") == mnemonic.size() - 2)
		{
			return 4;
		}
		if (mnemonic.rfind("x2") == mnemonic.size() - 2)
		{
			return 2;
		}
		return 1;
	}

	static unsigned ModifierValue(const AsmInstruction& AsmInstruction, const char* key, unsigned fallback)
	{
		auto found = AsmInstruction.mods.find(key);
		return found == AsmInstruction.mods.end()
		           ? fallback
		           : (unsigned)strtoul(found->second.c_str(), nullptr, 0);
	}

	void Translate(Context& context, const AsmInstruction& AsmInstruction)
	{
		const std::string mnemonic = BaseMnemonic(AsmInstruction.mnemonic);
		const std::vector<std::string>& ops = AsmInstruction.ops;

		context.outClamp = AsmInstruction.mods.count("clamp") != 0;
		context.outMul = nullptr;
		{
			auto found = AsmInstruction.mods.find("mul");
			if (found != AsmInstruction.mods.end())
			{
				context.outMul = (found->second == "4") ? "4.0" : "2.0";
			}
			else if (AsmInstruction.mods.count("div"))
			{
				context.outMul = "0.5";
			}
		}

		if (mnemonic == "s_wqm_b64" || mnemonic == "s_waitcnt" || mnemonic == "s_nop")
		{
			EmitComment(context, AsmInstruction.raw);
			return;
		}

		// x4 descriptor fetch: record the CBV table index so a later s_buffer_load
		// through this base reads the right cbuffer register. Stride is 8 dwords.
		if (mnemonic == "s_load_dwordx4" && ops.size() >= 3 && ops[2].rfind("0x", 0) == 0)
		{
			unsigned offset = (unsigned)strtoul(ops[2].c_str(), nullptr, 16);
			context.cbufIndex[(unsigned)RegisterBase(ops[0])] = offset / 8;
			context.sampSlotLive[(unsigned)RegisterBase(ops[0])] = offset / 4;
			EmitComment(context, AsmInstruction.raw + "  (descriptor fetch: table index " +
			                         std::to_string(offset / 8) + ")");
			return;
		}
		if (mnemonic == "s_load_dwordx8" && ops.size() >= 3 && ops[2].rfind("0x", 0) == 0)
		{
			unsigned offset = (unsigned)strtoul(ops[2].c_str(), nullptr, 16);
			context.texSlotLive[(unsigned)RegisterBase(ops[0])] = offset / 8;
			EmitComment(context, AsmInstruction.raw + "  (SRV descriptor fetch: t" +
			                         std::to_string(offset / 8) + ")");
			return;
		}
		// Remaining s_load_* forms are descriptor fetches the SMRD prepass consumed.
		if (mnemonic.rfind("s_load_dword", 0) == 0)
		{
			EmitComment(context, AsmInstruction.raw + "  (descriptor fetch)");
			return;
		}

		// Literal-offset s_buffer_loads. VS routes through the base V#'s own cbuffer
		// register; PS and CS keep a neutral 1.0 because GPU-written buffers reach
		// them this way and no CPU-side shadow can supply those.
		if (mnemonic.rfind("s_buffer_load_dword", 0) == 0 && ops.size() >= 3 &&
		    ops[2].rfind("lit:", 0) == 0)
		{
			unsigned count = VectorCountSuffix(mnemonic);
			unsigned base = (unsigned)RegisterBase(ops[0]);
			// The literal offset is in dwords, the same unit as the imm field.
			unsigned litDwords = (unsigned)strtoul(ops[2].c_str() + 4, nullptr, 16);
			unsigned cbuffer = 0;
			bool haveCbuffer = false;
			{
				auto found = context.cbufIndex.find((unsigned)RegisterBase(ops[1]));
				if (found != context.cbufIndex.end() && found->second < 16)
				{
					cbuffer = found->second;
					haveCbuffer = true;
				}
			}
			if (context.isVertex && haveCbuffer && (litDwords + count) <= 4096)
			{
				std::string array = cbuffer ? "g_udata" + std::to_string(cbuffer) : "g_udata";
				if (cbuffer)
				{
					context.cbufUsed.insert(cbuffer);
				}
				static const char* swizzle = "xyzw";
				EmitComment(context, AsmInstruction.raw + "  (lit-offset via cbuffer b" +
				                         std::to_string(cbuffer) + ", VS only)");
				for (unsigned i = 0; i < count && base + i <= 101; ++i)
				{
					unsigned slot = litDwords + i;
					int& maxSlot = context.cbufSize[cbuffer];
					if ((int)(slot / 4) >= maxSlot)
					{
						maxSlot = (int)(slot / 4) + 1;
					}
					EmitAssign(context, DestName(context, "s" + std::to_string(base + i)),
					           array + "[" + std::to_string(slot / 4) + "]." + std::string(1, swizzle[slot % 4]));
				}
				return;
			}
			EmitComment(context, AsmInstruction.raw + "  (unattributed structured-buffer read: neutral 1.0)");
			for (unsigned i = 0; i < count && base + i <= 101; ++i)
			{
				EmitAssign(context, DestName(context, "s" + std::to_string(base + i)), "1.0");
			}
			return;
		}
		if (mnemonic.rfind("s_buffer_load_dword", 0) == 0 && ops.size() >= 3 &&
		    ops[2].rfind("0x", 0) == 0)
		{
			unsigned count = VectorCountSuffix(mnemonic);
			unsigned offset = (unsigned)strtoul(ops[2].c_str(), nullptr, 16);
			unsigned base = (unsigned)RegisterBase(ops[0]);
			unsigned cbuffer = 0;
			{
				auto found = context.cbufIndex.find((unsigned)RegisterBase(ops[1]));
				if (found != context.cbufIndex.end() && found->second < 16)
				{
					cbuffer = found->second;
				}
			}
			std::string array = cbuffer ? "g_udata" + std::to_string(cbuffer) : "g_udata";
			if (cbuffer)
			{
				context.cbufUsed.insert(cbuffer);
			}
			static const char* swizzle = "xyzw";
			EmitComment(context, AsmInstruction.raw);
			for (unsigned i = 0; i < count; ++i)
			{
				unsigned slot = offset + i;
				if (slot / 4 >= 1024 || base + i > 101)
				{
					continue;
				}
				int& maxSlot = context.cbufSize[cbuffer];
				if ((int)(slot / 4) >= maxSlot)
				{
					maxSlot = (int)(slot / 4) + 1;
				}
				EmitAssign(context, DestName(context, "s" + std::to_string(base + i)),
				           array + "[" + std::to_string(slot / 4) + "]." + std::string(1, swizzle[slot % 4]));
			}
			return;
		}
		// SGPR-offset form, VS and CS only for the same reason as the literal form.
		if (mnemonic.rfind("s_buffer_load_dword", 0) == 0 && ops.size() >= 3 &&
		    ops[2].size() >= 2 && ops[2][0] == 's' && isdigit((unsigned char)ops[2][1]) &&
		    (context.isVertex || context.isCompute))
		{
			unsigned count = VectorCountSuffix(mnemonic);
			unsigned base = (unsigned)RegisterBase(ops[0]);
			unsigned cbuffer = 0;
			{
				auto found = context.cbufIndex.find((unsigned)RegisterBase(ops[1]));
				if (found != context.cbufIndex.end() && found->second < 16)
				{
					cbuffer = found->second;
				}
			}
			std::string array = cbuffer ? "g_udata" + std::to_string(cbuffer) : "g_udata";
			if (cbuffer)
			{
				context.cbufUsed.insert(cbuffer);
			}
			{
				int& maxSlot = context.cbufSize[cbuffer];
				if (maxSlot < 64)
				{
					maxSlot = 64;
				}
			}
			std::string offset = SourceExpr(context, ops[2]);
			std::string temp = "xso" + std::to_string(context.predCounter++);
			EmitComment(context, AsmInstruction.raw + "  (SGPR dword offset via cbuffer b" +
			                         std::to_string(cbuffer) + ")");
			Emit(context, "uint " + temp + " = asuint(" + offset + ");");
			for (unsigned i = 0; i < count && base + i <= 101; ++i)
			{
				EmitAssign(context, DestName(context, "s" + std::to_string(base + i)),
				           array + "[(" + temp + "+" + std::to_string(i) + ")>>2][(" +
				               temp + "+" + std::to_string(i) + ")&3]");
			}
			return;
		}

		// The fetch shader is not in the container, so emulate it from the real
		// signature: attribute at input register R lands in VGPRs 4+4R and up.
		if (mnemonic == "s_swappc_b64")
		{
			if (context.isVertex)
			{
				static const char kSwizzle[4] = { 'x', 'y', 'z', 'w' };
				if (context.vsNoInputs)
				{
					EmitComment(context, AsmInstruction.raw + "  (vertex fetch: NO input layout - fetch VGPRs stay 0)");
					return;
				}
				if (!context.vsIn.empty())
				{
					EmitComment(context, AsmInstruction.raw + "  (vertex fetch: emulated from the real ISG1)");
					for (const VsInputElem& element : context.vsIn)
					{
						for (unsigned component = 0; component < element.comps && component < 4; ++component)
						{
							int vgpr = 4 + 4 * (int)element.reg + (int)component;
							context.vgprUsed.insert(vgpr);
							Emit(context, "v" + std::to_string(vgpr) + " = vin.attr" +
							                  std::to_string(element.semIdx) + "." +
							                  std::string(1, kSwizzle[component]) + ";");
						}
					}
				}
				else
				{
					EmitComment(context, AsmInstruction.raw + "  (vertex fetch: assumed FSlateVertex layout)");
					struct FetchLoad
					{
						int vgpr;
						const char* expr;
					};
					static const FetchLoad loads[] = {
						{ 4, "vin.attr0.x" }, { 5, "vin.attr0.y" }, { 6, "vin.attr0.z" }, { 7, "vin.attr0.w" },
						{ 8, "vin.attr1.x" }, { 9, "vin.attr1.y" },
						{ 12, "vin.attr2.x" }, { 13, "vin.attr2.y" },
						{ 16, "vin.attr3.x" }, { 17, "vin.attr3.y" }, { 18, "vin.attr3.z" }, { 19, "vin.attr3.w" },
						{ 20, "vin.attr4.x" }, { 21, "vin.attr4.y" }, { 22, "vin.attr4.z" }, { 23, "vin.attr4.w" },
					};
					for (const FetchLoad& load : loads)
					{
						context.vgprUsed.insert(load.vgpr);
						Emit(context, "v" + std::to_string(load.vgpr) + " = " + load.expr + ";");
					}
				}
			}
			else
			{
				EmitComment(context, AsmInstruction.raw);
			}
			return;
		}

		if (mnemonic == "s_mov_b32" && ops.size() >= 1 && ops[0] == "m0")
		{
			EmitComment(context, AsmInstruction.raw + "  (interp setup)");
			return;
		}
		if (mnemonic == "s_mov_b64" && ops.size() >= 2 && ops[1] == "exec")
		{
			std::string saved = context.currentPredicate.empty() ? "true" : context.currentPredicate;
			context.execSaves[ops[0]] = { saved, saved };
			EmitComment(context, AsmInstruction.raw + "  (exec save)");
			return;
		}

		if (mnemonic == "v_interp_p1_f32")
		{
			EmitComment(context, AsmInstruction.raw + "  (p1 setup)");
			return;
		}
		// p2 is the interpolated read, mov is the flat read; same attribute routing.
		if (mnemonic == "v_interp_p2_f32" || mnemonic == "v_interp_mov_f32")
		{
			std::string dst = DestName(context, ops[0]);
			std::string attribute = ops.size() >= 3 ? ops[2] : "";
			int attributeIndex = 0, componentIndex = 0;
			if (attribute.rfind("attr", 0) == 0)
			{
				size_t dot = attribute.find('.');
				attributeIndex = atoi(attribute.substr(4, dot - 4).c_str());
				componentIndex = ComponentIndex(dot != std::string::npos && dot + 1 < attribute.size()
				                                    ? attribute[dot + 1]
				                                    : 'x');
				int previous = context.attrMaxComp.count(attributeIndex) ? context.attrMaxComp[attributeIndex] : 0;
				context.attrMaxComp[attributeIndex] = (std::max)(previous, componentIndex);
			}
			Emit(context, dst + " = in_attr" + std::to_string(attributeIndex) + "." +
			                  ComponentName(componentIndex) + ";");
			return;
		}

		if (mnemonic == "v_mov_b32" || mnemonic == "s_mov_b32")
		{
			if (ops[0] == "exec" || ops[0] == "m0")
			{
				EmitComment(context, AsmInstruction.raw);
				return;
			}
			EmitAssign(context, DestName(context, ops[0]), SourceExpr(context, ops[1]));
			if (ops.size() >= 2 && ops[1].rfind("0x", 0) == 0 &&
			    (ops[0][0] == 'v' || ops[0][0] == 's') && ops[0].size() > 1 &&
			    isdigit((unsigned char)ops[0][1]) &&
			    (context.currentPredicate.empty() || context.predTrue.count(context.currentPredicate)))
			{
				uint32_t bits = (uint32_t)strtoul(ops[1].c_str(), nullptr, 16);
				int index = atoi(ops[0].c_str() + 1);
				if (ops[0][0] == 'v')
				{
					context.vKnownLit[index] = bits;
				}
				else
				{
					context.sKnownLit[index] = bits;
				}
			}
			return;
		}

		if (mnemonic == "v_add_f32")    { EmitAssign(context, DestName(context, ops[0]), SourceExpr(context, ops[1]) + " + " + SourceExpr(context, ops[2])); return; }
		if (mnemonic == "v_subrev_f32") { EmitAssign(context, DestName(context, ops[0]), SourceExpr(context, ops[2]) + " - " + SourceExpr(context, ops[1])); return; }
		if (mnemonic == "v_sub_f32")    { EmitAssign(context, DestName(context, ops[0]), SourceExpr(context, ops[1]) + " - " + SourceExpr(context, ops[2])); return; }
		if (mnemonic == "v_mul_f32")    { EmitAssign(context, DestName(context, ops[0]), SourceExpr(context, ops[1]) + " * " + SourceExpr(context, ops[2])); return; }
		if (mnemonic == "v_mul_legacy_f32")
		{
			std::string left = SourceExpr(context, ops[1]), right = SourceExpr(context, ops[2]);
			EmitAssign(context, DestName(context, ops[0]),
			           "(" + left + "==0.0||" + right + "==0.0)?0.0:(" + left + "*" + right + ")");
			return;
		}
		if (mnemonic == "v_log_f32")  { EmitAssign(context, DestName(context, ops[0]), "log2(" + SourceExpr(context, ops[1]) + ")"); return; }
		if (mnemonic == "v_exp_f32")  { EmitAssign(context, DestName(context, ops[0]), "exp2(" + SourceExpr(context, ops[1]) + ")"); return; }
		if (mnemonic == "v_rcp_f32" || mnemonic == "v_rcp_iflag_f32")
		                              { EmitAssign(context, DestName(context, ops[0]), "rcp(" + SourceExpr(context, ops[1]) + ")"); return; }
		if (mnemonic == "v_sqrt_f32") { EmitAssign(context, DestName(context, ops[0]), "sqrt(" + SourceExpr(context, ops[1]) + ")"); return; }
		if (mnemonic == "v_rsq_f32")  { EmitAssign(context, DestName(context, ops[0]), "rsqrt(" + SourceExpr(context, ops[1]) + ")"); return; }
		if (mnemonic == "v_max_f32")  { EmitAssign(context, DestName(context, ops[0]), "max(" + SourceExpr(context, ops[1]) + "," + SourceExpr(context, ops[2]) + ")"); return; }
		if (mnemonic == "v_min_f32")  { EmitAssign(context, DestName(context, ops[0]), "min(" + SourceExpr(context, ops[1]) + "," + SourceExpr(context, ops[2]) + ")"); return; }
		if (mnemonic == "v_min3_f32") { EmitAssign(context, DestName(context, ops[0]), "min(min(" + SourceExpr(context, ops[1]) + "," + SourceExpr(context, ops[2]) + ")," + SourceExpr(context, ops[3]) + ")"); return; }
		if (mnemonic == "v_max3_f32") { EmitAssign(context, DestName(context, ops[0]), "max(max(" + SourceExpr(context, ops[1]) + "," + SourceExpr(context, ops[2]) + ")," + SourceExpr(context, ops[3]) + ")"); return; }
		if (mnemonic == "v_med3_f32") { EmitAssign(context, DestName(context, ops[0]), "clamp(" + SourceExpr(context, ops[1]) + ",min(" + SourceExpr(context, ops[2]) + "," + SourceExpr(context, ops[3]) + "),max(" + SourceExpr(context, ops[2]) + "," + SourceExpr(context, ops[3]) + "))"); return; }
		if (mnemonic == "v_fract_f32") { EmitAssign(context, DestName(context, ops[0]), "frac(" + SourceExpr(context, ops[1]) + ")"); return; }
		if (mnemonic == "v_trunc_f32") { EmitAssign(context, DestName(context, ops[0]), "trunc(" + SourceExpr(context, ops[1]) + ")"); return; }
		if (mnemonic == "v_floor_f32") { EmitAssign(context, DestName(context, ops[0]), "floor(" + SourceExpr(context, ops[1]) + ")"); return; }
		if (mnemonic == "v_ceil_f32")  { EmitAssign(context, DestName(context, ops[0]), "ceil(" + SourceExpr(context, ops[1]) + ")"); return; }
		if (mnemonic == "v_rndne_f32") { EmitAssign(context, DestName(context, ops[0]), "round(" + SourceExpr(context, ops[1]) + ")"); return; }
		// GCN sin/cos take turns; the compiler already scaled by 1/2pi.
		if (mnemonic == "v_sin_f32")   { EmitAssign(context, DestName(context, ops[0]), "sin((" + SourceExpr(context, ops[1]) + ")*6.28318530718)"); return; }
		if (mnemonic == "v_cos_f32")   { EmitAssign(context, DestName(context, ops[0]), "cos((" + SourceExpr(context, ops[1]) + ")*6.28318530718)"); return; }

		{
			auto asUint = [&](const std::string& op) { return SourceExprUint(context, op); };
			auto asInt = [&](const std::string& op) { return SourceExprInt(context, op); };

			if (mnemonic == "v_and_b32") { EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asUint(ops[1]) + " & " + asUint(ops[2]) + ")"); return; }
			if (mnemonic == "v_or_b32")  { EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asUint(ops[1]) + " | " + asUint(ops[2]) + ")"); return; }
			if (mnemonic == "v_xor_b32") { EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asUint(ops[1]) + " ^ " + asUint(ops[2]) + ")"); return; }
			if (mnemonic == "v_not_b32") { EmitAssign(context, DestName(context, ops[0]), "asfloat(~" + asUint(ops[1]) + ")"); return; }
			// On the rev forms the shift amount is src0 and the value is src1.
			if (mnemonic == "v_lshlrev_b32") { EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asUint(ops[2]) + " << (" + asUint(ops[1]) + " & 31u))"); return; }
			if (mnemonic == "v_lshrrev_b32") { EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asUint(ops[2]) + " >> (" + asUint(ops[1]) + " & 31u))"); return; }
			if (mnemonic == "v_ashrrev_i32") { EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asInt(ops[2]) + " >> (" + asUint(ops[1]) + " & 31u))"); return; }
			if (mnemonic == "v_lshl_b32")    { EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asUint(ops[1]) + " << (" + asUint(ops[2]) + " & 31u))"); return; }
			if (mnemonic == "v_lshr_b32")    { EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asUint(ops[1]) + " >> (" + asUint(ops[2]) + " & 31u))"); return; }
			if (mnemonic == "v_add_nc_u32" || mnemonic == "v_add_u32")
			                                 { EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asUint(ops[1]) + " + " + asUint(ops[2]) + ")"); return; }
			if (mnemonic == "v_sub_nc_u32" || mnemonic == "v_sub_u32")
			                                 { EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asUint(ops[1]) + " - " + asUint(ops[2]) + ")"); return; }
			if (mnemonic == "v_subrev_nc_u32" || mnemonic == "v_subrev_u32")
			                                 { EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asUint(ops[2]) + " - " + asUint(ops[1]) + ")"); return; }
			if (mnemonic == "v_mul_u32_u24") { EmitAssign(context, DestName(context, ops[0]), "asfloat((" + asUint(ops[1]) + " & 0xFFFFFFu) * (" + asUint(ops[2]) + " & 0xFFFFFFu))"); return; }
			if (mnemonic == "v_mul_lo_u32")  { EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asUint(ops[1]) + " * " + asUint(ops[2]) + ")"); return; }
			if (mnemonic == "v_max_u32")     { EmitAssign(context, DestName(context, ops[0]), "asfloat(max(" + asUint(ops[1]) + "," + asUint(ops[2]) + "))"); return; }
			if (mnemonic == "v_min_u32")     { EmitAssign(context, DestName(context, ops[0]), "asfloat(min(" + asUint(ops[1]) + "," + asUint(ops[2]) + "))"); return; }
			if (mnemonic == "v_max_i32")     { EmitAssign(context, DestName(context, ops[0]), "asfloat(max(" + asInt(ops[1]) + "," + asInt(ops[2]) + "))"); return; }
			if (mnemonic == "v_min_i32")     { EmitAssign(context, DestName(context, ops[0]), "asfloat(min(" + asInt(ops[1]) + "," + asInt(ops[2]) + "))"); return; }
			if (mnemonic == "s_max_i32")     { EmitAssign(context, DestName(context, ops[0]), "asfloat(max(" + asInt(ops[1]) + "," + asInt(ops[2]) + "))"); return; }
			if (mnemonic == "s_min_i32")     { EmitAssign(context, DestName(context, ops[0]), "asfloat(min(" + asInt(ops[1]) + "," + asInt(ops[2]) + "))"); return; }
			if (mnemonic == "s_max_u32")     { EmitAssign(context, DestName(context, ops[0]), "asfloat(max(" + asUint(ops[1]) + "," + asUint(ops[2]) + "))"); return; }
			if (mnemonic == "s_min_u32")     { EmitAssign(context, DestName(context, ops[0]), "asfloat(min(" + asUint(ops[1]) + "," + asUint(ops[2]) + "))"); return; }
			if (mnemonic == "s_and_b32")     { EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asUint(ops[1]) + " & " + asUint(ops[2]) + ")"); return; }
			if (mnemonic == "s_or_b32")      { EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asUint(ops[1]) + " | " + asUint(ops[2]) + ")"); return; }
			if (mnemonic == "s_xor_b32")     { EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asUint(ops[1]) + " ^ " + asUint(ops[2]) + ")"); return; }
			if (mnemonic == "s_lshl_b32")    { EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asUint(ops[1]) + " << (" + asUint(ops[2]) + " & 31u))"); return; }
			if (mnemonic == "s_lshr_b32")    { EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asUint(ops[1]) + " >> (" + asUint(ops[2]) + " & 31u))"); return; }
			if (mnemonic == "s_ashr_i32")    { EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asInt(ops[1]) + " >> (" + asUint(ops[2]) + " & 31u))"); return; }

			// The decoder renders simm16 as a raw hex literal; sign-extend it here.
			if ((mnemonic == "s_movk_i32" || mnemonic == "s_addk_i32" || mnemonic == "s_mulk_i32") &&
			    ops.size() >= 2 && ops[1].rfind("0x", 0) == 0)
			{
				int immediate = (int)(int16_t)strtoul(ops[1].c_str(), nullptr, 16);
				std::string literal = std::to_string(immediate);
				if (mnemonic == "s_movk_i32")
				{
					EmitAssign(context, DestName(context, ops[0]), "asfloat(" + literal + ")");
				}
				else if (mnemonic == "s_addk_i32")
				{
					EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asInt(ops[0]) + " + " + literal + ")");
				}
				else
				{
					EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asInt(ops[0]) + " * " + literal + ")");
				}
				return;
			}
			// src1 packs the offset in [5:0] and the width in [22:16].
			if (mnemonic == "s_bfe_u32" && ops.size() >= 3)
			{
				std::string offsetWidth = asUint(ops[2]);
				EmitAssign(context, DestName(context, ops[0]),
				           "asfloat(((" + asUint(ops[1]) + " >> (" + offsetWidth + " & 31u)) & ((1u << ((" +
				               offsetWidth + " >> 16) & 31u)) - 1u)))");
				return;
			}
			if (mnemonic == "s_bfe_i32" && ops.size() >= 3)
			{
				std::string offsetWidth = asUint(ops[2]);
				EmitAssign(context, DestName(context, ops[0]),
				           "asfloat(((" + asInt(ops[1]) + " << (32 - int(" + offsetWidth +
				               " & 31u) - int((" + offsetWidth + " >> 16) & 31u)))"
				               " >> (32 - int((" + offsetWidth + " >> 16) & 31u))))");
				return;
			}
			if (mnemonic == "v_mul_hi_u32")
			{
				context.needMulHi = true;
				EmitAssign(context, DestName(context, ops[0]),
				           "asfloat(xc_mulhi(" + asUint(ops[1]) + "," + asUint(ops[2]) + "))");
				return;
			}
			// Uniform-wave model: "lane 0" is just the value.
			if (mnemonic == "v_readfirstlane_b32" || mnemonic == "v_readlane_b32")
			                                { EmitAssign(context, DestName(context, ops[0]), SourceExpr(context, ops[1])); return; }
			if (mnemonic == "s_add_u32" || mnemonic == "s_add_i32")
			                                { EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asUint(ops[1]) + " + " + asUint(ops[2]) + ")"); return; }
			if (mnemonic == "s_sub_u32" || mnemonic == "s_sub_i32")
			                                { EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asUint(ops[1]) + " - " + asUint(ops[2]) + ")"); return; }
			if (mnemonic == "s_mul_i32")    { EmitAssign(context, DestName(context, ops[0]), "asfloat(" + asInt(ops[1]) + " * " + asInt(ops[2]) + ")"); return; }
			if (mnemonic == "v_bfe_u32")    { EmitAssign(context, DestName(context, ops[0]),
			                                      "asfloat((" + asUint(ops[1]) + " >> (" + asUint(ops[2]) + " & 31u)) & ((1u << (" + asUint(ops[3]) + " & 31u)) - 1u))"); return; }
			if (mnemonic == "v_bfe_i32")    { EmitAssign(context, DestName(context, ops[0]),
			                                      "asfloat((" + asInt(ops[1]) + " << (32 - int(" + asUint(ops[2]) + " & 31u) - int(" + asUint(ops[3]) + " & 31u))) >> (32 - int(" + asUint(ops[3]) + " & 31u)))"); return; }

			if (mnemonic == "v_cvt_f32_u32") { EmitAssign(context, DestName(context, ops[0]), "float(" + asUint(ops[1]) + ")"); return; }
			if (mnemonic == "v_cvt_f32_i32") { EmitAssign(context, DestName(context, ops[0]), "float(" + asInt(ops[1]) + ")"); return; }
			if (mnemonic == "v_cvt_u32_f32") { EmitAssign(context, DestName(context, ops[0]), "asfloat(uint(" + SourceExpr(context, ops[1]) + "))"); return; }
			if (mnemonic == "v_cvt_i32_f32") { EmitAssign(context, DestName(context, ops[0]), "asfloat(int(" + SourceExpr(context, ops[1]) + "))"); return; }
			if (mnemonic == "v_cvt_f32_ubyte0") { EmitAssign(context, DestName(context, ops[0]), "float(" + asUint(ops[1]) + " & 0xFFu)"); return; }
			if (mnemonic == "v_cvt_f32_ubyte1") { EmitAssign(context, DestName(context, ops[0]), "float((" + asUint(ops[1]) + " >> 8) & 0xFFu)"); return; }
			if (mnemonic == "v_cvt_f32_ubyte2") { EmitAssign(context, DestName(context, ops[0]), "float((" + asUint(ops[1]) + " >> 16) & 0xFFu)"); return; }
			if (mnemonic == "v_cvt_f32_ubyte3") { EmitAssign(context, DestName(context, ops[0]), "float((" + asUint(ops[1]) + " >> 24) & 0xFFu)"); return; }
			if (mnemonic == "v_cvt_f32_f16") { EmitAssign(context, DestName(context, ops[0]), "f16tof32(" + asUint(ops[1]) + " & 0xFFFFu)"); return; }
			if (mnemonic == "v_cvt_f16_f32") { EmitAssign(context, DestName(context, ops[0]), "asfloat(f32tof16(" + SourceExpr(context, ops[1]) + "))"); return; }
		}

		if (mnemonic == "v_dot2c_f32_f16")
		{
			// The Xbox ISA repurposes VOP2 opcode 2, which llvm-mc misdecodes as
			// v_dot2c_f32_f16 with a phantom vdst. Real fields:
			//   [8:0] src0, also the in-place destination (VGPR when >=256)
			//   [16:9] SGPR multiplier
			//   [24:18] VGPR addend
			uint32_t encoding = AsmInstruction.enc0;
			uint32_t src0 = encoding & 0x1FF;
			uint32_t multiplier = (encoding >> 9) & 0xFF;
			uint32_t addend = (encoding >> 18) & 0x7F;
			if (encoding && src0 >= 256)
			{
				std::string dst = "v" + std::to_string(src0 - 256);
				EmitAssign(context, DestName(context, dst),
				           SourceExpr(context, dst) + " * " + SourceExpr(context, "s" + std::to_string(multiplier)) +
				               " + " + SourceExpr(context, "v" + std::to_string(addend)));
			}
			else
			{
				EmitComment(context, AsmInstruction.raw + "  (xbox op2: unsupported operand form, dropped)");
			}
			return;
		}

		if (mnemonic == "v_fmac_f32" || mnemonic == "v_fmac_legacy_f32" ||
		    mnemonic == "v_mac_f32" || mnemonic == "v_mac_legacy_f32")
		{
			EmitAccum(context, DestName(context, ops[0]), "+",
			          SourceExpr(context, ops[1]) + " * " + SourceExpr(context, ops[2]));
			return;
		}
		// madmk is D = S0*K + S1 and madak is D = S0*S1 + K; the decoder puts the
		// literal in the matching slot, so both are a*b + c over ops 1..3.
		if (mnemonic == "v_fma_f32" || mnemonic == "v_mad_f32" || mnemonic == "v_mad_legacy_f32" ||
		    mnemonic == "v_madmk_f32" || mnemonic == "v_madak_f32")
		{
			EmitAssign(context, DestName(context, ops[0]),
			           SourceExpr(context, ops[1]) + " * " + SourceExpr(context, ops[2]) + " + " + SourceExpr(context, ops[3]));
			return;
		}

		if (mnemonic == "v_cmp_gt_f32") { EmitCondition(context, ops[0], "(" + SourceExpr(context, ops[1]) + " > " + SourceExpr(context, ops[2]) + ")"); return; }
		if (mnemonic == "v_cmp_lt_f32") { EmitCondition(context, ops[0], "(" + SourceExpr(context, ops[1]) + " < " + SourceExpr(context, ops[2]) + ")"); return; }
		if (mnemonic == "v_cmp_ge_f32") { EmitCondition(context, ops[0], "(" + SourceExpr(context, ops[1]) + " >= " + SourceExpr(context, ops[2]) + ")"); return; }
		if (mnemonic == "v_cmp_le_f32") { EmitCondition(context, ops[0], "(" + SourceExpr(context, ops[1]) + " <= " + SourceExpr(context, ops[2]) + ")"); return; }
		if (mnemonic == "v_cmp_eq_f32") { EmitCondition(context, ops[0], "(" + SourceExpr(context, ops[1]) + " == " + SourceExpr(context, ops[2]) + ")"); return; }
		if (mnemonic == "v_cmp_lg_f32" || mnemonic == "v_cmp_neq_f32")
		                                { EmitCondition(context, ops[0], "(" + SourceExpr(context, ops[1]) + " != " + SourceExpr(context, ops[2]) + ")"); return; }
		// Unordered compares approximate to the plain complement: no NaN model.
		if (mnemonic == "v_cmp_nlt_f32") { EmitCondition(context, ops[0], "(" + SourceExpr(context, ops[1]) + " >= " + SourceExpr(context, ops[2]) + ")"); return; }
		if (mnemonic == "v_cmp_nle_f32") { EmitCondition(context, ops[0], "(" + SourceExpr(context, ops[1]) + " > " + SourceExpr(context, ops[2]) + ")"); return; }
		if (mnemonic == "v_cmp_ngt_f32") { EmitCondition(context, ops[0], "(" + SourceExpr(context, ops[1]) + " <= " + SourceExpr(context, ops[2]) + ")"); return; }
		if (mnemonic == "v_cmp_nge_f32") { EmitCondition(context, ops[0], "(" + SourceExpr(context, ops[1]) + " < " + SourceExpr(context, ops[2]) + ")"); return; }
		if (mnemonic == "v_cmp_nlg_f32") { EmitCondition(context, ops[0], "(" + SourceExpr(context, ops[1]) + " == " + SourceExpr(context, ops[2]) + ")"); return; }
		if (mnemonic == "v_cmp_o_f32")   { EmitCondition(context, ops[0], "true"); return; }
		if (mnemonic == "v_cmp_u_f32")   { EmitCondition(context, ops[0], "false"); return; }
		if (mnemonic == "v_cmp_eq_i32" || mnemonic == "v_cmp_eq_u32")
		                                { EmitCondition(context, ops[0], "(" + SourceExprInt(context, ops[1]) + " == " + SourceExprInt(context, ops[2]) + ")"); return; }
		if (mnemonic == "v_cmp_ne_i32" || mnemonic == "v_cmp_ne_u32" ||
		    mnemonic == "v_cmp_lg_i32" || mnemonic == "v_cmp_lg_u32")
		                                { EmitCondition(context, ops[0], "(" + SourceExprInt(context, ops[1]) + " != " + SourceExprInt(context, ops[2]) + ")"); return; }
		if (mnemonic == "v_cmp_gt_i32") { EmitCondition(context, ops[0], "(" + SourceExprInt(context, ops[1]) + " > " + SourceExprInt(context, ops[2]) + ")"); return; }
		if (mnemonic == "v_cmp_lt_i32") { EmitCondition(context, ops[0], "(" + SourceExprInt(context, ops[1]) + " < " + SourceExprInt(context, ops[2]) + ")"); return; }
		if (mnemonic == "v_cmp_ge_i32") { EmitCondition(context, ops[0], "(" + SourceExprInt(context, ops[1]) + " >= " + SourceExprInt(context, ops[2]) + ")"); return; }
		if (mnemonic == "v_cmp_le_i32") { EmitCondition(context, ops[0], "(" + SourceExprInt(context, ops[1]) + " <= " + SourceExprInt(context, ops[2]) + ")"); return; }
		if (mnemonic == "v_cmp_gt_u32") { EmitCondition(context, ops[0], "(" + SourceExprUint(context, ops[1]) + " > " + SourceExprUint(context, ops[2]) + ")"); return; }
		if (mnemonic == "v_cmp_lt_u32") { EmitCondition(context, ops[0], "(" + SourceExprUint(context, ops[1]) + " < " + SourceExprUint(context, ops[2]) + ")"); return; }
		if (mnemonic == "v_cmp_ge_u32") { EmitCondition(context, ops[0], "(" + SourceExprUint(context, ops[1]) + " >= " + SourceExprUint(context, ops[2]) + ")"); return; }
		if (mnemonic == "v_cmp_le_u32") { EmitCondition(context, ops[0], "(" + SourceExprUint(context, ops[1]) + " <= " + SourceExprUint(context, ops[2]) + ")"); return; }

		// These carry an explicit VCC operand, so the sources sit one slot further
		// along: dst, vcc/sdst, src0, src1 [, carry-in].
		if (mnemonic == "v_add_i32" || mnemonic == "v_sub_i32" || mnemonic == "v_subrev_i32" ||
		    mnemonic == "v_addc_u32" || mnemonic == "v_subb_u32" || mnemonic == "v_subbrev_u32")
		{
			size_t first = (ops.size() >= 4) ? 2 : 1;
			std::string left = SourceExprInt(context, ops[first]), right = SourceExprInt(context, ops[first + 1]);
			const char* op = (mnemonic == "v_add_i32" || mnemonic == "v_addc_u32") ? " + " : " - ";
			std::string rhs = (mnemonic == "v_subrev_i32" || mnemonic == "v_subbrev_u32")
			                      ? (right + op + left)
			                      : (left + op + right);
			// The VOP3 forms name an explicit carry-in pair; vcc may be stale.
			std::string carryIn = (ops.size() >= 5) ? MaskExpr(context, ops[4]) : std::string("vcc");
			if (mnemonic == "v_addc_u32")
			{
				rhs += " + (" + carryIn + " ? 1 : 0)";
			}
			else if (mnemonic == "v_subb_u32" || mnemonic == "v_subbrev_u32")
			{
				rhs += " - (" + carryIn + " ? 1 : 0)";
			}
			EmitAssign(context, DestName(context, ops[0]), "asfloat(" + rhs + ")");
			return;
		}
		if (mnemonic == "v_mul_lo_i32" || mnemonic == "v_mul_lo_u32")
		{
			EmitAssign(context, DestName(context, ops[0]),
			           "asfloat(" + SourceExprInt(context, ops[1]) + " * " + SourceExprInt(context, ops[2]) + ")");
			return;
		}
		if (mnemonic == "v_mad_u32_u24" && ops.size() >= 4)
		{
			EmitAssign(context, DestName(context, ops[0]),
			           "asfloat(((" + SourceExprUint(context, ops[1]) + " & 0xFFFFFFu) * (" +
			               SourceExprUint(context, ops[2]) + " & 0xFFFFFFu)) + " + SourceExprUint(context, ops[3]) + ")");
			return;
		}
		if (mnemonic == "v_mul_u32_u24" && ops.size() >= 3)
		{
			EmitAssign(context, DestName(context, ops[0]),
			           "asfloat((" + SourceExprUint(context, ops[1]) + " & 0xFFFFFFu) * (" +
			               SourceExprUint(context, ops[2]) + " & 0xFFFFFFu))");
			return;
		}

		// Cube face math: the major axis picks the face, sc/tc are the in-face
		// coordinates, ma is twice the signed major-axis value.
		if ((mnemonic == "v_cubeid_f32" || mnemonic == "v_cubesc_f32" ||
		     mnemonic == "v_cubetc_f32" || mnemonic == "v_cubema_f32") && ops.size() >= 4)
		{
			std::string x = SourceExpr(context, ops[1]);
			std::string y = SourceExpr(context, ops[2]);
			std::string z = SourceExpr(context, ops[3]);
			std::string xMajor = "(abs(" + x + ")>=abs(" + y + ") && abs(" + x + ")>=abs(" + z + "))";
			std::string yMajor = "(abs(" + y + ")>=abs(" + z + "))";
			std::string rhs;
			if (mnemonic == "v_cubeid_f32")
			{
				rhs = xMajor + " ? (" + x + ">=0.0?0.0:1.0) : " + yMajor + " ? (" + y + ">=0.0?2.0:3.0) : (" + z + ">=0.0?4.0:5.0)";
			}
			else if (mnemonic == "v_cubema_f32")
			{
				rhs = "2.0 * (" + xMajor + " ? " + x + " : " + yMajor + " ? " + y + " : " + z + ")";
			}
			else if (mnemonic == "v_cubesc_f32")
			{
				rhs = xMajor + " ? (" + x + ">=0.0 ? -(" + z + ") : " + z + ") : " + yMajor + " ? " + x + " : (" + z + ">=0.0 ? " + x + " : -(" + x + "))";
			}
			else
			{
				rhs = xMajor + " ? -(" + y + ") : " + yMajor + " ? (" + y + ">=0.0 ? " + z + " : -(" + z + ")) : -(" + y + ")";
			}
			EmitAssign(context, DestName(context, ops[0]), rhs);
			return;
		}
		// Uniform-wave model: any lane swizzle is the identity.
		if (mnemonic == "ds_swizzle_b32" && ops.size() >= 2)
		{
			EmitAssign(context, DestName(context, ops[0]), SourceExpr(context, ops[1]));
			return;
		}
		if (mnemonic == "s_brev_b32" && ops.size() >= 2)
		{
			EmitAssign(context, DestName(context, ops[0]), "asfloat(reversebits(" + SourceExprUint(context, ops[1]) + "))");
			return;
		}

		if (mnemonic == "v_cndmask_b32")
		{
			// VOP2 has 3 operands and an implicit VCC; VOP3 names the condition 4th.
			std::string condition = (ops.size() >= 4) ? MaskExpr(context, ops[3]) : std::string("vcc");
			EmitAssign(context, DestName(context, ops[0]),
			           condition + " ? (" + SourceExpr(context, ops[2]) + ") : (" + SourceExpr(context, ops[1]) + ")");
			return;
		}

		if (mnemonic == "v_dot2_f32_f16")
		{
			EmitAccum(context, DestName(context, ops[0]), "+",
			          SourceExpr(context, ops[1]) + " * " + SourceExpr(context, ops[2]) + " /*dot2c approx*/");
			return;
		}

		if (mnemonic == "v_cvt_pkrtz_f16_f32")
		{
			int dst = atoi(ops[0].c_str() + 1);
			context.vgprUsed.insert(dst);
			context.packedPairs[dst] = { SourceExpr(context, ops[1]), SourceExpr(context, ops[2]) };
			EmitComment(context, AsmInstruction.raw + "  (packed pair tracked)");
			return;
		}

		// Image and buffer UAVs are both 8 dwords in the UAV table, so x4-loaded
		// V#s (recorded as off/4) are halved again.
		if (context.isCompute && (mnemonic == "image_store" || mnemonic == "image_load"))
		{
			int uav = 0;
			if (ops.size() > 2 && ops[2][0] == 's')
			{
				auto found = context.descIndex.find((unsigned)RegisterBase(ops[2]));
				if (found != context.descIndex.end())
				{
					uav = (int)found->second;
				}
			}
			bool is3d = context.ntZ > 1;
			context.uavSlots[uav] = is3d ? "RWTexture3D<float4>" : "RWTexture2D<float4>";
			int addressBase = RegisterBase(ops[1]);
			auto coordinate = [&](int index)
			{
				return "asuint(" + SourceExpr(context, "v" + std::to_string(addressBase + index)) + ")";
			};
			std::string coord = is3d
			                        ? "uint3(" + coordinate(0) + "," + coordinate(1) + "," + coordinate(2) + ")"
			                        : "uint2(" + coordinate(0) + "," + coordinate(1) + ")";
			int dataBase = RegisterBase(ops[0]);
			if (mnemonic == "image_store")
			{
				std::string value = "float4(" + SourceExpr(context, "v" + std::to_string(dataBase)) + "," +
				                    SourceExpr(context, "v" + std::to_string(dataBase + 1)) + "," +
				                    SourceExpr(context, "v" + std::to_string(dataBase + 2)) + "," +
				                    SourceExpr(context, "v" + std::to_string(dataBase + 3)) + ")";
				std::string store = "uav" + std::to_string(uav) + "[" + coord + "] = " + value + ";";
				if (context.currentPredicate.empty())
				{
					Emit(context, store);
				}
				else
				{
					Emit(context, "if (" + context.currentPredicate + ") { " + store + " }");
				}
			}
			else
			{
				std::string temp = "ld" + std::to_string(context.sampleCount++);
				Emit(context, "float4 " + temp + " = uav" + std::to_string(uav) + "[" + coord + "];");
				uint32_t dmask = ModifierValue(AsmInstruction, "dmask", 0xF);
				const char* swizzle = "xyzw";
				int written = 0;
				for (int component = 0; component < 4; ++component)
				{
					if (dmask & (1u << component))
					{
						EmitAssign(context, DestName(context, "v" + std::to_string(dataBase + written++)),
						           temp + "." + std::string(1, swizzle[component]));
					}
				}
			}
			return;
		}

		// offen puts the byte offset in a VGPR, which the typed float4 model cannot
		// address, so read through a ByteAddressBuffer. Combined idxen+offen forms
		// stay unhandled: they need the V# stride, which is not recovered.
		if (ops.size() > 2 && ops[2][0] == 's' &&
		    AsmInstruction.mods.count("offen") && !AsmInstruction.mods.count("idxen") &&
		    mnemonic.rfind("buffer_load_dword", 0) == 0)
		{
			int slot = BufferSlotFor(context, ops[2]);
			int componentCount = 1;
			if (mnemonic.rfind("x4") == mnemonic.size() - 2)      componentCount = 4;
			else if (mnemonic.rfind("x3") == mnemonic.size() - 2) componentCount = 3;
			else if (mnemonic.rfind("x2") == mnemonic.size() - 2) componentCount = 2;
			unsigned byteOffset = ModifierValue(AsmInstruction, "offset", 0);
			if (slot >= 0 && !context.texSlots.count(slot) && !context.texReserved.count(slot) &&
			    !context.bufSrvSlots.count(slot) && !context.uavBufSlots.count(slot))
			{
				context.rawSrvSlots.insert(slot);
				int dataBase = RegisterBase(ops[0]);
				std::string address = "(" + SourceExprUint(context, ops.size() > 1 ? ops[1] : "0") + " + " +
				                      std::to_string(byteOffset) + "u";
				if (ops.size() > 3 && ops[3] != "0")
				{
					address += " + " + SourceExprUint(context, ops[3]);
				}
				address += ")";
				std::string temp = "ld" + std::to_string(context.sampleCount++);
				if (componentCount == 1)
				{
					Emit(context, "uint " + temp + " = rawsrv" + std::to_string(slot) + ".Load" + address + ";");
					EmitAssign(context, DestName(context, "v" + std::to_string(dataBase)), "asfloat(" + temp + ")");
				}
				else
				{
					static const char* swizzle = "xyzw";
					Emit(context, "uint" + std::to_string(componentCount) + " " + temp + " = rawsrv" +
					                  std::to_string(slot) + ".Load" + std::to_string(componentCount) + address + ";");
					for (int component = 0; component < componentCount; ++component)
					{
						EmitAssign(context, DestName(context, "v" + std::to_string(dataBase + component)),
						           "asfloat(" + temp + "." + std::string(1, swizzle[component]) + ")");
					}
				}
				return;
			}
		}

		// Typed Buffer<float4> SRV at the V#'s own table slot. UE's V# stride is 16
		// (one float4 element), so `idxen offset:N` reads channel N/4 of element
		// vindex and the raw dword forms map onto the same typed SRV.
		if (ops.size() > 2 && ops[2][0] == 's' &&
		    AsmInstruction.mods.count("idxen") && !AsmInstruction.mods.count("offen") &&
		    ((!context.isCompute && mnemonic.rfind("buffer_load_format", 0) == 0) ||
		     (mnemonic.rfind("buffer_load_dword", 0) == 0 && (ops.size() <= 3 || ops[3] == "0"))))
		{
			bool raw = mnemonic.rfind("buffer_load_dword", 0) == 0;
			int slot = BufferSlotFor(context, ops[2]);
			int componentCount = 1;
			if (raw)
			{
				if (mnemonic.rfind("x4") == mnemonic.size() - 2)      componentCount = 4;
				else if (mnemonic.rfind("x3") == mnemonic.size() - 2) componentCount = 3;
				else if (mnemonic.rfind("x2") == mnemonic.size() - 2) componentCount = 2;
			}
			else
			{
				if (mnemonic.rfind("xyzw") != std::string::npos)     componentCount = 4;
				else if (mnemonic.rfind("xyz") != std::string::npos) componentCount = 3;
				else if (mnemonic.rfind("xy") != std::string::npos)  componentCount = 2;
			}
			unsigned byteOffset = ModifierValue(AsmInstruction, "offset", 0);
			unsigned channelOffset = raw ? byteOffset / 4 : 0;
			bool addressOk = !raw || ((byteOffset % 4) == 0 && channelOffset + componentCount <= 4);
			if (slot >= 0 && addressOk && !context.texSlots.count(slot) &&
			    !context.texReserved.count(slot) && !context.rawSrvSlots.count(slot))
			{
				context.bufSrvSlots.insert(slot);
				int dataBase = RegisterBase(ops[0]);
				std::string index = "asuint(" + SourceExpr(context, ops.size() > 1 ? ops[1] : "0") + ")";
				std::string temp = "ld" + std::to_string(context.sampleCount++);
				Emit(context, "float4 " + temp + " = srvbuf" + std::to_string(slot) + "[" + index + "];");
				const char* swizzle = "xyzw";
				for (int component = 0; component < componentCount; ++component)
				{
					EmitAssign(context, DestName(context, "v" + std::to_string(dataBase + component)),
					           temp + "." + std::string(1, swizzle[channelOffset + component]));
				}
				return;
			}
		}

		// LDS as one dword-addressed array. Reads are (vdst, vaddr), writes are
		// (vaddr, vdata0 [, vdata1]); read2/write2 offsets are element units while
		// the single-dword forms carry a byte offset split as off0 + off1<<8.
		if (context.isCompute && (mnemonic.rfind("ds_read", 0) == 0 || mnemonic.rfind("ds_write", 0) == 0))
		{
			unsigned offset0 = ModifierValue(AsmInstruction, "offset0", 0);
			unsigned offset1 = ModifierValue(AsmInstruction, "offset1", 0);
			bool paired = mnemonic.rfind("ds_read2", 0) == 0 || mnemonic.rfind("ds_write2", 0) == 0;
			bool wide = mnemonic == "ds_read_b64" || mnemonic == "ds_write_b64";
			bool stride64 = mnemonic.find("st64") != std::string::npos;
			if ((paired || wide || mnemonic == "ds_read_b32" || mnemonic == "ds_write_b32") && ops.size() >= 2)
			{
				context.usesLds = true;
				unsigned stride = stride64 ? 64u : 1u;
				bool isRead = mnemonic.rfind("ds_read", 0) == 0;
				const std::string& addressOp = isRead ? ops[1] : ops[0];
				std::string base = "(asuint(" + SourceExpr(context, addressOp) + ") >> 2)";
				if (isRead)
				{
					int dataBase = RegisterBase(ops[0]);
					if (paired || wide)
					{
						unsigned element0 = paired ? offset0 * stride : 0;
						unsigned element1 = paired ? offset1 * stride : 1;
						EmitAssign(context, DestName(context, "v" + std::to_string(dataBase)),
						           "asfloat(xc_lds[(" + base + " + " + std::to_string(element0) + "u) & 1023u])");
						EmitAssign(context, DestName(context, "v" + std::to_string(dataBase + 1)),
						           "asfloat(xc_lds[(" + base + " + " + std::to_string(element1) + "u) & 1023u])");
					}
					else
					{
						unsigned byteOffset = offset0 + (offset1 << 8);
						EmitAssign(context, DestName(context, "v" + std::to_string(dataBase)),
						           "asfloat(xc_lds[(" + base + " + " + std::to_string(byteOffset / 4) + "u) & 1023u])");
					}
				}
				else
				{
					auto store = [&](unsigned elementOffset, const std::string& value)
					{
						// InterlockedExchange dodges fxc's X3695 on groupshared stores
						// it cannot prove disjoint.
						std::string statement = context.ldsShared
						    ? "InterlockedExchange(xc_lds[(" + base + " + " + std::to_string(elementOffset) +
						          "u) & 1023u], asuint(" + value + "), xc_shx);"
						    : "xc_lds[(" + base + " + " + std::to_string(elementOffset) + "u) & 1023u] = asuint(" + value + ");";
						if (context.currentPredicate.empty())
						{
							Emit(context, statement);
						}
						else
						{
							Emit(context, "if (" + context.currentPredicate + ") { " + statement + " }");
						}
					};
					if (paired && ops.size() >= 3)
					{
						store(offset0 * stride, SourceExpr(context, ops[1]));
						store(offset1 * stride, SourceExpr(context, ops.size() > 2 ? ops[2] : ops[1]));
					}
					else if (wide && ops.size() >= 2)
					{
						int sourceBase = RegisterBase(ops[1]);
						store(0, SourceExpr(context, "v" + std::to_string(sourceBase)));
						store(1, SourceExpr(context, "v" + std::to_string(sourceBase + 1)));
					}
					else
					{
						unsigned byteOffset = offset0 + (offset1 << 8);
						store(byteOffset / 4, SourceExpr(context, ops[1]));
					}
				}
				return;
			}
		}
		// A barrier orders nothing under per-thread LDS, and emitting the real
		// intrinsic inside a reconstructed `if` fails fxc's divergent-flow check.
		if (mnemonic == "s_barrier")
		{
			if (context.ldsShared && context.currentPredicate.empty() && context.indent == 0)
			{
				Emit(context, "GroupMemoryBarrierWithGroupSync();");
			}
			else
			{
				EmitComment(context, AsmInstruction.raw + (context.ldsShared
				                                            ? "  (barrier in divergent flow: skipped)"
				                                            : "  (no cross-thread LDS model)"));
			}
			return;
		}
		// Shared model only: with per-thread LDS every thread sees its own counter.
		if (context.isCompute && mnemonic.rfind("ds_add", 0) == 0 && ops.size() >= 2)
		{
			context.usesLds = true;
			bool returns = mnemonic.rfind("ds_add_rtn", 0) == 0;
			// The rtn form omits vdst in the printed operands; recover it from the
			// DS encoding's last byte in the raw line.
			int vdst = -1;
			if (returns)
			{
				size_t at = AsmInstruction.raw.rfind("0x");
				if (at != std::string::npos)
				{
					vdst = (int)strtoul(AsmInstruction.raw.c_str() + at, nullptr, 16);
				}
			}
			std::string base = "(asuint(" + SourceExpr(context, ops[0]) + ") >> 2)";
			std::string previous = "old" + std::to_string(context.sampleCount++);
			Emit(context, "uint " + previous + " = 0;");
			std::string statement = "InterlockedAdd(xc_lds[" + base + " & 1023u], asuint(" +
			                        SourceExpr(context, ops[1]) + "), " + previous + ");";
			if (context.currentPredicate.empty())
			{
				Emit(context, statement);
			}
			else
			{
				Emit(context, "if (" + context.currentPredicate + ") { " + statement + " }");
			}
			if (returns && vdst >= 0)
			{
				EmitAssign(context, DestName(context, "v" + std::to_string(vdst)), "asfloat(" + previous + ")");
			}
			return;
		}
		// Each translated thread is one lane, so "lanes below me" is empty and
		// "first active lane" is me.
		if (mnemonic.rfind("v_mbcnt_lo_u32_b32", 0) == 0 || mnemonic.rfind("v_mbcnt_hi_u32_b32", 0) == 0)
		{
			EmitComment(context, AsmInstruction.raw + "  (solo-lane mbcnt: base passthrough)");
			EmitAssign(context, DestName(context, ops[0]), SourceExpr(context, ops.size() > 2 ? ops[2] : "0"));
			return;
		}
		if (mnemonic == "s_ff1_i32_b64")
		{
			EmitComment(context, AsmInstruction.raw + "  (solo-lane ff1: this thread is the first active lane)");
			EmitAssign(context, DestName(context, ops[0]), "0.0");
			return;
		}
		if (context.isCompute && mnemonic.rfind("buffer_atomic_add", 0) == 0 &&
		    ops.size() > 2 && ops[2][0] == 's' && !AsmInstruction.mods.count("offen"))
		{
			int uav = 0;
			{
				int slot = BufferSlotFor(context, ops[2]);
				if (slot >= 0)
				{
					uav = slot;
				}
			}
			if (!context.uavSlots.count(uav) && !context.uavBufSlots.count(uav))
			{
				context.rawUavSlots.insert(uav);
				unsigned byteOffset = ModifierValue(AsmInstruction, "offset", 0);
				std::string address = AsmInstruction.mods.count("idxen")
				    ? "(asuint(" + SourceExpr(context, ops.size() > 1 ? ops[1] : "0") + ") * 16u + " +
				          std::to_string(byteOffset) + "u)"
				    : "(" + std::to_string(byteOffset) + "u)";
				std::string previous = "old" + std::to_string(context.sampleCount++);
				Emit(context, "uint " + previous + " = 0;");
				std::string statement = "rawuav" + std::to_string(uav) + ".InterlockedAdd(" + address +
				                        ", asuint(" + SourceExpr(context, ops[0]) + "), " + previous + ");";
				if (context.currentPredicate.empty())
				{
					Emit(context, statement);
				}
				else
				{
					Emit(context, "if (" + context.currentPredicate + ") { " + statement + " }");
				}
				if (AsmInstruction.mods.count("glc"))
				{
					EmitAssign(context, DestName(context, ops[0]), "asfloat(" + previous + ")");
				}
				return;
			}
		}
		// Without idxen/offen the address is just the AsmInstruction offset (counters).
		if (context.isCompute && (mnemonic == "buffer_load_dword" || mnemonic == "buffer_store_dword") &&
		    ops.size() > 2 && ops[2][0] == 's' &&
		    !AsmInstruction.mods.count("idxen") && !AsmInstruction.mods.count("offen"))
		{
			int uav = 0;
			{
				int slot = BufferSlotFor(context, ops[2]);
				if (slot >= 0)
				{
					uav = slot;
				}
			}
			unsigned byteOffset = ModifierValue(AsmInstruction, "offset", 0);
			if (mnemonic == "buffer_store_dword")
			{
				if (!context.uavSlots.count(uav) && !context.uavBufSlots.count(uav))
				{
					context.rawUavSlots.insert(uav);
					std::string statement = "rawuav" + std::to_string(uav) + ".Store(" +
					                        std::to_string(byteOffset) + "u, asuint(" + SourceExpr(context, ops[0]) + "));";
					if (context.currentPredicate.empty())
					{
						Emit(context, statement);
					}
					else
					{
						Emit(context, "if (" + context.currentPredicate + ") { " + statement + " }");
					}
					return;
				}
			}
			else if (context.rawUavSlots.count(uav))
			{
				EmitAssign(context, DestName(context, ops[0]),
				           "asfloat(rawuav" + std::to_string(uav) + ".Load(" + std::to_string(byteOffset) + "u))");
				return;
			}
			else if (!context.texSlots.count(uav) && !context.bufSrvSlots.count(uav))
			{
				context.rawSrvSlots.insert(uav);
				EmitAssign(context, DestName(context, ops[0]),
				           "asfloat(rawsrv" + std::to_string(uav) + ".Load(" + std::to_string(byteOffset) + "u))");
				return;
			}
		}
		// Full-element x4 stores go through the typed model: the game's GPUScene UAV
		// descriptors are structured stride-16, and a raw RWByteAddressBuffer
		// declaration against them silently drops every write. Partial stores stay raw.
		if (context.isCompute && mnemonic == "buffer_store_dwordx4" &&
		    ops.size() > 2 && ops[2][0] == 's' &&
		    AsmInstruction.mods.count("idxen") && !AsmInstruction.mods.count("offen") &&
		    (!AsmInstruction.mods.count("offset") || AsmInstruction.mods.at("offset") == "0"))
		{
			int uav = 0;
			{
				int slot = BufferSlotFor(context, ops[2]);
				if (slot >= 0)
				{
					uav = slot;
				}
			}
			if (!context.uavSlots.count(uav) && !context.rawUavSlots.count(uav))
			{
				context.uavBufSlots.insert(uav);
				int dataBase = RegisterBase(ops[0]);
				std::string index = "asuint(" + SourceExpr(context, ops.size() > 1 ? ops[1] : "0") + ")";
				std::string value = "float4(";
				for (int component = 0; component < 4; ++component)
				{
					if (component)
					{
						value += ",";
					}
					value += SourceExpr(context, "v" + std::to_string(dataBase + component));
				}
				value += ")";
				std::string statement = "buf" + std::to_string(uav) + "[" + index + "] = " + value + ";";
				if (context.currentPredicate.empty())
				{
					Emit(context, statement);
				}
				else
				{
					Emit(context, "if (" + context.currentPredicate + ") { " + statement + " }");
				}
				return;
			}
		}
		if (context.isCompute && mnemonic.rfind("buffer_store_dword", 0) == 0 &&
		    ops.size() > 2 && ops[2][0] == 's' &&
		    AsmInstruction.mods.count("idxen") && !AsmInstruction.mods.count("offen"))
		{
			int uav = 0;
			{
				int slot = BufferSlotFor(context, ops[2]);
				if (slot >= 0)
				{
					uav = slot;
				}
			}
			if (!context.uavSlots.count(uav) && !context.uavBufSlots.count(uav))
			{
				context.rawUavSlots.insert(uav);
				int componentCount = 1;
				if (mnemonic.rfind("x4") == mnemonic.size() - 2)      componentCount = 4;
				else if (mnemonic.rfind("x3") == mnemonic.size() - 2) componentCount = 3;
				else if (mnemonic.rfind("x2") == mnemonic.size() - 2) componentCount = 2;
				unsigned byteOffset = ModifierValue(AsmInstruction, "offset", 0);
				std::string address = "(asuint(" + SourceExpr(context, ops.size() > 1 ? ops[1] : "0") +
				                      ") * 16u + " + std::to_string(byteOffset) + "u)";
				int dataBase = RegisterBase(ops[0]);
				std::string value;
				if (componentCount == 1)
				{
					value = "asuint(" + SourceExpr(context, "v" + std::to_string(dataBase)) + ")";
				}
				else
				{
					value = "uint" + std::to_string(componentCount) + "(";
					for (int component = 0; component < componentCount; ++component)
					{
						if (component)
						{
							value += ",";
						}
						value += "asuint(" + SourceExpr(context, "v" + std::to_string(dataBase + component)) + ")";
					}
					value += ")";
				}
				std::string statement = "rawuav" + std::to_string(uav) + ".Store" +
				                        (componentCount > 1 ? std::to_string(componentCount) : "") +
				                        "(" + address + ", " + value + ");";
				if (context.currentPredicate.empty())
				{
					Emit(context, statement);
				}
				else
				{
					Emit(context, "if (" + context.currentPredicate + ") { " + statement + " }");
				}
				return;
			}
		}
		if (context.isCompute && (mnemonic.rfind("buffer_store_format", 0) == 0 ||
		                          mnemonic.rfind("buffer_load_format", 0) == 0))
		{
			int uav = 0;
			if (ops.size() > 2 && ops[2][0] == 's')
			{
				int slot = BufferSlotFor(context, ops[2]);
				if (slot >= 0)
				{
					uav = slot;
				}
			}
			context.uavBufSlots.insert(uav);
			int componentCount = 1;
			if (mnemonic.rfind("xyzw") != std::string::npos)     componentCount = 4;
			else if (mnemonic.rfind("xyz") != std::string::npos) componentCount = 3;
			else if (mnemonic.rfind("xy") != std::string::npos)  componentCount = 2;
			int dataBase = RegisterBase(ops[0]);
			std::string index = "asuint(" + SourceExpr(context, ops.size() > 1 ? ops[1] : "0") + ")";
			if (mnemonic.rfind("buffer_store", 0) == 0)
			{
				std::string value = "float4(";
				for (int component = 0; component < 4; ++component)
				{
					if (component)
					{
						value += ",";
					}
					value += (component < componentCount)
					             ? SourceExpr(context, "v" + std::to_string(dataBase + component))
					             : "0.0";
				}
				value += ")";
				std::string statement = "buf" + std::to_string(uav) + "[" + index + "] = " + value + ";";
				if (context.currentPredicate.empty())
				{
					Emit(context, statement);
				}
				else
				{
					Emit(context, "if (" + context.currentPredicate + ") { " + statement + " }");
				}
			}
			else
			{
				std::string temp = "ld" + std::to_string(context.sampleCount++);
				Emit(context, "float4 " + temp + " = buf" + std::to_string(uav) + "[" + index + "];");
				const char* swizzle = "xyzw";
				for (int component = 0; component < componentCount; ++component)
				{
					EmitAssign(context, DestName(context, "v" + std::to_string(dataBase + component)),
					           temp + "." + std::string(1, swizzle[component]));
				}
			}
			return;
		}

		if (!context.isCompute && mnemonic == "image_load" && ops.size() > 2 && ops[2][0] == 's')
		{
			uint32_t dmask = ModifierValue(AsmInstruction, "dmask", 0xF);
			int dataBase = (ops[0][0] == 'v') ? RegisterBase(ops[0]) : 0;
			if (context.resourceFree)
			{
				int written = 0;
				for (int component = 0; component < 4; ++component)
				{
					if (dmask & (1u << component))
					{
						EmitAssign(context, DestName(context, "v" + std::to_string(dataBase + written)), "1.0");
						written++;
					}
				}
				context.sampleCount++;
				return;
			}
			int texture = -1;
			{
				auto found = context.descIndex.find((unsigned)RegisterBase(ops[2]));
				if (found != context.descIndex.end())
				{
					texture = (int)found->second;
				}
			}
			if (texture >= 0 && !context.bufSrvSlots.count(texture) && !context.rawSrvSlots.count(texture))
			{
				context.texSlots[texture] = "Texture2D";
				int addressBase = RegisterBase(ops[1]);
				context.sampleCount++;
				std::string temp = "smp" + std::to_string(context.sampleCount);
				Emit(context, "float4 " + temp + " = tex" + std::to_string(texture) + ".Load(int3(asint(" +
				                  SourceExpr(context, "v" + std::to_string(addressBase)) + "), asint(" +
				                  SourceExpr(context, "v" + std::to_string(addressBase + 1)) + "), 0));");
				const char* swizzle = "xyzw";
				int written = 0;
				for (int component = 0; component < 4; ++component)
				{
					if (dmask & (1u << component))
					{
						EmitAssign(context, DestName(context, "v" + std::to_string(dataBase + written)),
						           temp + "." + std::string(1, swizzle[component]));
						written++;
					}
				}
				return;
			}
		}
		// Four neighbourhood samples of the channel the dmask bit picks.
		if (!context.isCompute && mnemonic.rfind("image_gather4", 0) == 0 && ops.size() > 3 &&
		    ops[2][0] == 's' && ops[3][0] == 's' && !context.resourceFree)
		{
			int texture = -1, sampler = -1;
			{
				auto found = context.descIndex.find((unsigned)RegisterBase(ops[2]));
				if (found != context.descIndex.end())
				{
					texture = (int)found->second;
				}
			}
			{
				auto found = context.descIndex.find((unsigned)RegisterBase(ops[3]));
				if (found != context.descIndex.end())
				{
					sampler = (int)found->second;
				}
			}
			if (texture >= 0 && !context.bufSrvSlots.count(texture) && !context.rawSrvSlots.count(texture))
			{
				if (sampler < 0)
				{
					sampler = texture;
				}
				context.texSlots[texture] = "Texture2D";
				context.sampSlots.insert(sampler);
				uint32_t dmask = ModifierValue(AsmInstruction, "dmask", 0x1);
				const char* gather = (dmask & 8) ? "GatherAlpha" : (dmask & 4) ? "GatherBlue"
				                   : (dmask & 2) ? "GatherGreen" : "GatherRed";
				int addressBase = RegisterBase(ops[1]);
				std::string coord = "float2(" + SourceExpr(context, "v" + std::to_string(addressBase)) + "," +
				                    SourceExpr(context, "v" + std::to_string(addressBase + 1)) + ")";
				context.sampleCount++;
				std::string temp = "smp" + std::to_string(context.sampleCount);
				Emit(context, "float4 " + temp + " = tex" + std::to_string(texture) + "." + gather +
				                  "(samp" + std::to_string(sampler) + ", " + coord + ");");
				int dataBase = (ops[0][0] == 'v') ? RegisterBase(ops[0]) : 0;
				const char* swizzle = "xyzw";
				for (int component = 0; component < 4; ++component)
				{
					EmitAssign(context, DestName(context, "v" + std::to_string(dataBase + component)),
					           temp + "." + std::string(1, swizzle[component]));
				}
				return;
			}
		}

		if (mnemonic == "image_sample" || AsmInstruction.mnemonic.rfind("image_sample", 0) == 0)
		{
			if (context.resourceFree)
			{
				uint32_t dmask = ModifierValue(AsmInstruction, "dmask", 0xF);
				int dataBase = (ops[0][0] == 'v') ? RegisterBase(ops[0]) : 0;
				int written = 0;
				for (int component = 0; component < 4; ++component)
				{
					if (dmask & (1u << component))
					{
						EmitAssign(context, DestName(context, "v" + std::to_string(dataBase + written)), "1.0");
						written++;
					}
				}
				if (written == 0)
				{
					EmitComment(context, AsmInstruction.raw + "  (dmask 0)");
				}
				context.sampleCount++;
				return;
			}
			std::string dimension = AsmInstruction.mods.count("dim") ? AsmInstruction.mods.at("dim") : "SQ_RSRC_IMG_2D";
			int coordCount;
			std::string textureType;
			// GDK encodes DIM=1D but the descriptor determines the real dimension,
			// and the game binds Texture2D SRVs.
			if (dimension.find("1D") != std::string::npos)        { coordCount = 2; textureType = "Texture2D"; }
			else if (dimension.find("3D") != std::string::npos)   { coordCount = 3; textureType = "Texture3D"; }
			else if (dimension.find("CUBE") != std::string::npos) { coordCount = 3; textureType = "TextureCube"; }
			else                                                  { coordCount = 2; textureType = "Texture2D"; }

			context.sampleCount++;
			int texture = -1, sampler = -1;
			// Program order first; descIndex is a fallback for registers loaded
			// before any tracked fetch. Numbering by order of appearance instead
			// binds the wrong texture whenever the first sample is not descriptor 0.
			if (ops.size() > 2 && ops[2][0] == 's')
			{
				unsigned base = (unsigned)RegisterBase(ops[2]);
				auto live = context.texSlotLive.find(base);
				if (live != context.texSlotLive.end())
				{
					texture = (int)live->second;
				}
				else
				{
					auto found = context.descIndex.find(base);
					if (found != context.descIndex.end())
					{
						texture = (int)found->second;
					}
				}
			}
			if (ops.size() > 3 && ops[3][0] == 's')
			{
				unsigned base = (unsigned)RegisterBase(ops[3]);
				auto live = context.sampSlotLive.find(base);
				if (live != context.sampSlotLive.end())
				{
					sampler = (int)live->second;
				}
				else
				{
					auto found = context.descIndex.find(base);
					if (found != context.descIndex.end())
					{
						sampler = (int)found->second;
					}
				}
			}
			if (texture < 0)
			{
				texture = 0;
				while (context.texSlots.count(texture))
				{
					++texture;
				}
			}
			if (sampler < 0)
			{
				sampler = texture;
			}
			context.texSlots[texture] = textureType;
			context.sampSlots.insert(sampler);

			int addressBase = (ops.size() > 1 && ops[1][0] == 'v') ? RegisterBase(ops[1]) : 0;
			std::string coord;
			if (coordCount == 1)
			{
				coord = SourceExpr(context, "v" + std::to_string(addressBase));
			}
			else
			{
				coord = (coordCount == 2 ? "float2(" : "float3(");
				for (int component = 0; component < coordCount; ++component)
				{
					if (component)
					{
						coord += ",";
					}
					coord += SourceExpr(context, "v" + std::to_string(addressBase + component));
				}
				coord += ")";
			}
			// Outside a PS there are no quad neighbours to derive a gradient from,
			// so the LOD must be explicit.
			std::string sample = (context.isVertex || context.isCompute)
			    ? "tex" + std::to_string(texture) + ".SampleLevel(samp" + std::to_string(sampler) + ", " + coord + ", 0)"
			    : "tex" + std::to_string(texture) + ".Sample(samp" + std::to_string(sampler) + ", " + coord + ")";
			uint32_t dmask = ModifierValue(AsmInstruction, "dmask", 0xF);
			const char* swizzle = "xyzw";
			int dataBase = (ops[0][0] == 'v') ? RegisterBase(ops[0]) : 0;
			int written = 0;
			// Sample once into a temp, then distribute: GCN reads all coords before
			// writing results, so a dest overlapping a coord register is legal and
			// a per-channel re-sample would read the coord after it was overwritten.
			std::string temp = "smp" + std::to_string(context.sampleCount);
			Emit(context, "float4 " + temp + " = " + sample + ";");
			for (int component = 0; component < 4; ++component)
			{
				if (dmask & (1u << component))
				{
					EmitAssign(context, DestName(context, "v" + std::to_string(dataBase + written)),
					           temp + "." + std::string(1, swizzle[component]));
					written++;
				}
			}
			if (written == 0)
			{
				EmitComment(context, AsmInstruction.raw + "  (dmask 0 - no result)");
			}
			return;
		}

		if (mnemonic == "exp" || mnemonic == "export")
		{
			bool compressed = AsmInstruction.mods.count("compr") != 0;
			std::string target = ops.empty() ? "" : ops[0];
			auto store = [&](const std::string& dst, const std::string& rhs)
			{
				if (context.currentPredicate.empty())
				{
					Emit(context, dst + " = " + rhs + ";");
				}
				else
				{
					Emit(context, dst + " = (" + context.currentPredicate + ") ? (" + rhs + ") : " + dst + ";");
				}
			};

			// Assign at the export's program position: capturing the register names
			// and emitting at the end re-read registers later code had overwritten.
			if (context.isVertex && (target.rfind("pos", 0) == 0 || target.rfind("param", 0) == 0))
			{
				auto source = [&](int index) -> std::string
				{
					return (int)ops.size() > index ? SourceExpr(context, ops[index]) : "0";
				};
				std::array<std::string, 4> values = { source(1), source(2), source(3), source(4) };
				std::string rhs = "float4(" + values[0] + ", " + values[1] + ", " + values[2] + ", " + values[3] + ")";
				if (target.rfind("pos", 0) == 0)
				{
					context.posOut = values;
					context.hasPos = true;
					store("o.pos", rhs);
				}
				else
				{
					int index = atoi(target.c_str() + 5);
					if (context.remapActive)
					{
						auto remapped = context.paramRemap.find(index);
						if (remapped == context.paramRemap.end())
						{
							EmitComment(context, AsmInstruction.raw + "  (vertex export: PS reads no such row, dropped)");
							return;
						}
						index = remapped->second;
					}
					context.params[index] = values;
					if (index > context.maxParam)
					{
						context.maxParam = index;
					}
					store("o.param" + std::to_string(index), rhs);
				}
				EmitComment(context, AsmInstruction.raw + "  (vertex export)");
				return;
			}
			// mrtz carries depth and null is a discard target; neither is colour.
			if (target == "mrtz" || target == "null")
			{
				EmitComment(context, AsmInstruction.raw + "  (non-color export, dropped)");
				return;
			}
			std::string dst = "out_color";
			if (context.mrtMulti && target.rfind("mrt", 0) == 0)
			{
				dst = "out" + std::string(target.c_str() + 3);
			}
			auto sourceOr = [&](int index) -> std::string
			{
				return (int)ops.size() > index ? ops[index] : "off";
			};
			if (compressed)
			{
				auto unpack = [&](const std::string& reg, bool high) -> std::string
				{
					if (!reg.empty() && reg[0] == 'v')
					{
						int index = atoi(reg.c_str() + 1);
						auto found = context.packedPairs.find(index);
						if (found != context.packedPairs.end())
						{
							return high ? found->second.second : found->second.first;
						}
						return SourceExpr(context, reg);
					}
					return "0.0";
				};
				// compr packs all four channels as f16x2 into vsrc0 and vsrc1, i.e.
				// operands 1 and 2; vsrc2 is unused.
				std::string red = unpack(sourceOr(1), false), green = unpack(sourceOr(1), true);
				std::string blue = unpack(sourceOr(2), false), alpha = unpack(sourceOr(2), true);
				Emit(context, "// export " + target + " (compr f16x2 pair)");
				store(dst, "float4(" + red + ", " + green + ", " + blue + ", " + alpha + ")");
			}
			else
			{
				store(dst, "float4(" + SourceExpr(context, sourceOr(1)) + ", " + SourceExpr(context, sourceOr(2)) + ", " +
				               SourceExpr(context, sourceOr(3)) + ", " + SourceExpr(context, sourceOr(4)) + ")");
			}
			return;
		}

		if (mnemonic == "s_and_saveexec_b64" || mnemonic == "s_or_saveexec_b64")
		{
			int counter = context.predCounter++;
			std::string saved = context.currentPredicate.empty() ? "true" : context.currentPredicate;
			// The condition is the AsmInstruction's own source operand; hardcoding vcc
			// made every `s_and_saveexec_b64 sPAIR, sPAIR` site test a stale value.
			std::string condition = MaskExpr(context, ops.size() > 1 ? ops[1] : "vcc");
			std::string thenPredicate;
			if (mnemonic == "s_and_saveexec_b64")
			{
				thenPredicate = context.currentPredicate.empty()
				                    ? condition
				                    : "((" + context.currentPredicate + ") && " + condition + ")";
			}
			else   // exec = src | exec, so active lanes can only grow
			{
				thenPredicate = (context.currentPredicate.empty() || condition == "true")
				                    ? (condition == "true" ? "true" : context.currentPredicate.empty() ? "true" : context.currentPredicate)
				                    : "((" + context.currentPredicate + ") || " + condition + ")";
			}
			std::string savedName = "exsave" + std::to_string(counter);
			std::string predicateName = "pred" + std::to_string(counter);
			context.predDecls.push_back(savedName);
			context.predDecls.push_back(predicateName);
			Emit(context, savedName + " = " + saved + ";");
			Emit(context, predicateName + " = " + thenPredicate + ";");
			if (saved == "true")
			{
				context.predTrue.insert(savedName);
			}
			if (thenPredicate == "true")
			{
				context.predTrue.insert(predicateName);
			}
			context.execSaves[ops[0]] = { savedName, predicateName };
			context.currentPredicate = (thenPredicate == "true") ? std::string() : predicateName;
			return;
		}
		if (mnemonic == "s_andn2_b64" && ops.size() >= 3 && ops[0] == "exec")
		{
			auto saved = context.execSaves.find(ops[1]);
			if (saved != context.execSaves.end())
			{
				std::string predicateName = "pred" + std::to_string(context.predCounter++);
				context.predDecls.push_back(predicateName);
				Emit(context, predicateName + " = " + saved->second.first + " && !(" + saved->second.second + ");");
				context.currentPredicate = predicateName;
			}
			else
			{
				EmitComment(context, AsmInstruction.raw + "  (andn2: no saved exec?)");
			}
			return;
		}
		// `s_xor_b64 exec, exec, s[saved]` is the standard GCN else: flip to the
		// lanes the `if` skipped. Non-exec forms are value ops, handled below.
		if (mnemonic == "s_xor_b64" && ops.size() >= 3 && ops[0] == "exec" &&
		    (ops[1] == "exec" || ops[2] == "exec"))
		{
			const std::string& savedToken = (ops[1] == "exec") ? ops[2] : ops[1];
			auto saved = context.execSaves.find(savedToken);
			if (saved != context.execSaves.end())
			{
				std::string predicateName = "pred" + std::to_string(context.predCounter++);
				context.predDecls.push_back(predicateName);
				Emit(context, predicateName + " = " + saved->second.first + " && !(" + saved->second.second + ");");
				context.currentPredicate = predicateName;
				EmitComment(context, AsmInstruction.raw + "  (else: invert within saved exec)");
			}
			else
			{
				EmitComment(context, AsmInstruction.raw + "  (xor-else: no saved exec?)");
			}
			return;
		}
		if (mnemonic == "s_mov_b64" && ops.size() >= 2 && ops[0] == "exec")
		{
			auto saved = context.execSaves.find(ops[1]);
			context.currentPredicate = (saved != context.execSaves.end()) ? saved->second.first : std::string();
			if (context.currentPredicate == "true")
			{
				context.currentPredicate.clear();
			}
			EmitComment(context, AsmInstruction.raw + "  (endif: restore exec)");
			return;
		}
		// A mask copy is not boolean algebra: the value just moves.
		if (mnemonic == "s_mov_b64" && ops.size() >= 2)
		{
			if (ops[1] == "-1" || ops[1] == "0" || ops[1][0] == 's' || ops[1] == "vcc")
			{
				EmitCondition(context, ops[0], MaskExpr(context, ops[1]));
				return;
			}
			EmitComment(context, AsmInstruction.raw);
			return;
		}
		// `s_and_b64 exec, exec, sSAVED` is the WQM epilogue; anything else narrows
		// the active predicate, an if without a saveexec.
		if (mnemonic == "s_and_b64" && ops.size() >= 3 && ops[0] == "exec")
		{
			auto saved = context.execSaves.find(ops[1] != "exec" ? ops[1] : ops[2]);
			if ((ops[1] == "exec" || ops[2] == "exec") && saved != context.execSaves.end())
			{
				context.currentPredicate = saved->second.first;
				if (context.currentPredicate == "true")
				{
					context.currentPredicate.clear();
				}
				EmitComment(context, AsmInstruction.raw + "  (WQM restore)");
				return;
			}
			std::string left = MaskExpr(context, ops[1]), right = MaskExpr(context, ops[2]);
			std::string narrowed = (left == "true") ? right : (right == "true") ? left : "(" + left + " && " + right + ")";
			if (narrowed == "true")
			{
				context.currentPredicate.clear();
				EmitComment(context, AsmInstruction.raw + "  (exec: all lanes)");
				return;
			}
			std::string predicateName = "pred" + std::to_string(context.predCounter++);
			context.predDecls.push_back(predicateName);
			Emit(context, predicateName + " = " + narrowed + ";  // " + AsmInstruction.raw);
			context.currentPredicate = predicateName;
			return;
		}
		// The mask chains computing the conditions that feed saveexec guards.
		if ((mnemonic == "s_and_b64" || mnemonic == "s_or_b64" || mnemonic == "s_xor_b64" ||
		     mnemonic == "s_andn2_b64" || mnemonic == "s_orn2_b64") &&
		    ops.size() >= 3 && ops[0] != "exec")
		{
			std::string left = MaskExpr(context, ops[1]), right = MaskExpr(context, ops[2]);
			std::string expr;
			if (mnemonic == "s_and_b64")        expr = (left == "true") ? right : (right == "true") ? left : "(" + left + " && " + right + ")";
			else if (mnemonic == "s_or_b64")    expr = (left == "false") ? right : (right == "false") ? left : "(" + left + " || " + right + ")";
			else if (mnemonic == "s_xor_b64")   expr = (right == "true") ? "!(" + left + ")" : (left == "true") ? "!(" + right + ")" : "(" + left + " != " + right + ")";
			else if (mnemonic == "s_andn2_b64") expr = (right == "false") ? left : "(" + left + " && !(" + right + "))";
			else                                expr = (right == "false") ? "true" : "(" + left + " || !(" + right + "))";
			EmitCondition(context, ops[0], expr);
			context.vccExpr = "vcc";
			return;
		}
		if (mnemonic == "s_cselect_b64" && ops.size() >= 3 && ops[0] != "exec")
		{
			std::string left = MaskExpr(context, ops[1]), right = MaskExpr(context, ops[2]);
			EmitCondition(context, ops[0], "(" + ConditionName(context, "scc") + " ? " + left + " : " + right + ")");
			return;
		}
		// Single-lane model: the one lane contributes 0 or 1.
		if (mnemonic == "s_bcnt1_i32_b64" && ops.size() >= 2)
		{
			EmitAssign(context, DestName(context, ops[0]), "asfloat(" + MaskExpr(context, ops[1]) + " ? 1u : 0u)");
			return;
		}
		// Scalar compares write SCC; only s_cselect consumes it, branches on it stay
		// fall-through.
		if (mnemonic.rfind("s_cmp_", 0) == 0 && ops.size() >= 2)
		{
			std::string suffix = mnemonic.substr(6);
			const char* op = nullptr;
			if (suffix.rfind("eq_", 0) == 0)                                     op = "==";
			else if (suffix.rfind("lg_", 0) == 0 || suffix.rfind("ne_", 0) == 0) op = "!=";
			else if (suffix.rfind("gt_", 0) == 0)                                op = ">";
			else if (suffix.rfind("ge_", 0) == 0)                                op = ">=";
			else if (suffix.rfind("lt_", 0) == 0)                                op = "<";
			else if (suffix.rfind("le_", 0) == 0)                                op = "<=";
			if (op)
			{
				bool isUnsigned = suffix.size() > 3 && suffix[3] == 'u';
				std::string left = isUnsigned ? SourceExprUint(context, ops[0]) : SourceExprInt(context, ops[0]);
				std::string right = isUnsigned ? SourceExprUint(context, ops[1]) : SourceExprInt(context, ops[1]);
				EmitCondition(context, "scc", "(" + left + " " + op + " " + right + ")");
				return;
			}
		}
		if (mnemonic == "s_cselect_b32" && ops.size() >= 3)
		{
			EmitAssign(context, DestName(context, ops[0]),
			           "(" + ConditionName(context, "scc") + " ? (" + SourceExpr(context, ops[1]) + ") : (" +
			               SourceExpr(context, ops[2]) + "))");
			return;
		}
		// Branches the emit loop did not reconstruct, e.g. backward ones.
		if (mnemonic == "s_and_b64" || mnemonic == "s_or_b64" ||
		    mnemonic == "s_cbranch_vccnz" || mnemonic == "s_cbranch_vccz" ||
		    mnemonic == "s_cbranch_execz" || mnemonic == "s_cbranch_execnz" ||
		    mnemonic == "s_branch" || mnemonic == "s_endpgm" ||
		    mnemonic == "s_cbranch_scc0" || mnemonic == "s_cbranch_scc1")
		{
			EmitComment(context, AsmInstruction.raw + "  (branch not reconstructed: left as fall-through)");
			return;
		}

		context.unhandled.push_back(AsmInstruction.mnemonic);
		EmitComment(context, "UNHANDLED: " + AsmInstruction.raw);
		if (!ops.empty() && (ops[0][0] == 'v' || ops[0][0] == 's'))
		{
			DestName(context, ops[0]);
		}
	}
}
