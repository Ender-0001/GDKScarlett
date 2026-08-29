#include "GcnDecoder.h"

#include <cstdio>

namespace GDKScarlett::D3D12X
{
	const char* EncodingName(Encoding encoding)
	{
		switch (encoding)
		{
		case Encoding::Sop1:   return "SOP1";
		case Encoding::Sop2:   return "SOP2";
		case Encoding::Sopc:   return "SOPC";
		case Encoding::Sopp:   return "SOPP";
		case Encoding::Sopk:   return "SOPK";
		case Encoding::Smrd:   return "SMRD";
		case Encoding::Smem:   return "SMEM";
		case Encoding::Vop1:   return "VOP1";
		case Encoding::Vop2:   return "VOP2";
		case Encoding::Vopc:   return "VOPC";
		case Encoding::Vop3:   return "VOP3";
		case Encoding::Vop3p:  return "VOP3P";
		case Encoding::Vintrp: return "VINTRP";
		case Encoding::Ds:     return "DS";
		case Encoding::Mubuf:  return "MUBUF";
		case Encoding::Mtbuf:  return "MTBUF";
		case Encoding::Mimg:   return "MIMG";
		case Encoding::Flat:   return "FLAT";
		case Encoding::Exp:    return "EXP";
		default:               return "?";
		}
	}

	static bool SrcIsLiteral(uint32_t src9)
	{
		return src9 == 0xFF;
	}

	// DPP8 (0xE9/0xEA), DPP (0xFA), SDWA (0xF9) and literal (0xFF) all pull an
	// extra dword through a 9-bit VOP source field.
	static bool Vop9BitPullsExtra(uint32_t src9)
	{
		return src9 == 0xE9 || src9 == 0xEA || src9 == 0xF9 || src9 == 0xFA || src9 == 0xFF;
	}

	// Unnamed opcodes still decode at the correct width, so the walk never desyncs.
	static const char* NameSop2(uint32_t op)
	{
		switch (op)
		{
		case 0x00: return "s_add_u32";
		case 0x01: return "s_sub_u32";
		case 0x02: return "s_add_i32";
		case 0x03: return "s_sub_i32";
		case 0x04: return "s_addc_u32";
		case 0x05: return "s_subb_u32";
		case 0x06: return "s_min_i32";
		case 0x07: return "s_min_u32";
		case 0x08: return "s_max_i32";
		case 0x09: return "s_max_u32";
		case 0x0A: return "s_cselect_b32";
		case 0x0B: return "s_cselect_b64";
		case 0x0E: return "s_and_b32";
		case 0x0F: return "s_and_b64";
		case 0x10: return "s_or_b32";
		case 0x11: return "s_or_b64";
		case 0x12: return "s_xor_b32";
		case 0x13: return "s_xor_b64";
		case 0x14: return "s_andn2_b32";
		case 0x15: return "s_andn2_b64";
		case 0x16: return "s_orn2_b32";
		case 0x17: return "s_orn2_b64";
		case 0x18: return "s_nand_b32";
		case 0x1A: return "s_nor_b32";
		case 0x1C: return "s_xnor_b32";
		case 0x1E: return "s_lshl_b32";
		case 0x1F: return "s_lshl_b64";
		case 0x20: return "s_lshr_b32";
		case 0x21: return "s_lshr_b64";
		case 0x22: return "s_ashr_i32";
		case 0x24: return "s_bfm_b32";
		case 0x26: return "s_mul_i32";
		case 0x27: return "s_bfe_u32";
		case 0x28: return "s_bfe_i32";
		case 0x2E: return "s_lshl1_add_u32";
		case 0x2F: return "s_lshl2_add_u32";
		case 0x30: return "s_lshl3_add_u32";
		case 0x31: return "s_lshl4_add_u32";
		case 0x35: return "s_mul_hi_u32";
		case 0x36: return "s_mul_hi_i32";
		default:   return "";
		}
	}

	static const char* NameSop1(uint32_t op)
	{
		switch (op)
		{
		case 0x03: return "s_mov_b32";
		case 0x04: return "s_mov_b64";
		case 0x05: return "s_cmov_b32";
		case 0x07: return "s_not_b32";
		case 0x08: return "s_not_b64";
		case 0x09: return "s_wqm_b32";
		case 0x0A: return "s_wqm_b64";
		case 0x0B: return "s_brev_b32";
		case 0x0D: return "s_bcnt0_i32_b32";
		case 0x0F: return "s_bcnt1_i32_b32";
		case 0x10: return "s_bcnt1_i32_b64";
		case 0x11: return "s_ff0_i32_b32";
		case 0x13: return "s_ff1_i32_b32";
		case 0x14: return "s_ff1_i32_b64";
		case 0x15: return "s_flbit_i32_b32";
		case 0x17: return "s_flbit_i32";
		case 0x19: return "s_sext_i32_i8";
		case 0x1A: return "s_sext_i32_i16";
		case 0x1B: return "s_bitset0_b32";
		case 0x1D: return "s_bitset1_b32";
		case 0x1F: return "s_getpc_b64";
		case 0x20: return "s_setpc_b64";
		case 0x21: return "s_swappc_b64";
		case 0x24: return "s_and_saveexec_b64";
		case 0x25: return "s_or_saveexec_b64";
		case 0x26: return "s_xor_saveexec_b64";
		case 0x27: return "s_andn2_saveexec_b64";
		case 0x2E: return "s_movrels_b32";
		case 0x30: return "s_movreld_b32";
		case 0x32: return "s_cbranch_join";
		case 0x34: return "s_abs_i32";
		default:   return "";
		}
	}

	static const char* NameSopc(uint32_t op)
	{
		switch (op)
		{
		case 0x00: return "s_cmp_eq_i32";
		case 0x01: return "s_cmp_lg_i32";
		case 0x02: return "s_cmp_gt_i32";
		case 0x03: return "s_cmp_ge_i32";
		case 0x04: return "s_cmp_lt_i32";
		case 0x05: return "s_cmp_le_i32";
		case 0x06: return "s_cmp_eq_u32";
		case 0x07: return "s_cmp_lg_u32";
		case 0x08: return "s_cmp_gt_u32";
		case 0x09: return "s_cmp_ge_u32";
		case 0x0A: return "s_cmp_lt_u32";
		case 0x0B: return "s_cmp_le_u32";
		default:   return "";
		}
	}

	static const char* NameSopp(uint32_t op)
	{
		switch (op)
		{
		case 0x00: return "s_nop";
		case 0x01: return "s_endpgm";
		case 0x02: return "s_branch";
		case 0x04: return "s_cbranch_scc0";
		case 0x05: return "s_cbranch_scc1";
		case 0x06: return "s_cbranch_vccz";
		case 0x07: return "s_cbranch_vccnz";
		case 0x08: return "s_cbranch_execz";
		case 0x09: return "s_cbranch_execnz";
		case 0x0A: return "s_barrier";
		case 0x0C: return "s_waitcnt";
		case 0x10: return "s_sendmsg";
		case 0x20: return "s_inst_prefetch";
		case 0x21: return "s_clause";
		case 0x23: return "s_waitcnt_depctr";
		default:   return "";
		}
	}

	// `op` is already normalized (raw field minus 0x60).
	static const char* NameSopk(uint32_t op)
	{
		switch (op)
		{
		case 0x00: return "s_movk_i32";
		case 0x0F: return "s_addk_i32";
		case 0x10: return "s_mulk_i32";
		default:   return "";
		}
	}

	static const char* NameVop1(uint32_t op)
	{
		switch (op)
		{
		case 0x00: return "v_nop";
		case 0x01: return "v_mov_b32";
		case 0x02: return "v_readfirstlane_b32";
		case 0x05: return "v_cvt_f32_i32";
		case 0x06: return "v_cvt_f32_u32";
		case 0x07: return "v_cvt_u32_f32";
		case 0x08: return "v_cvt_i32_f32";
		case 0x0A: return "v_cvt_f16_f32";
		case 0x0B: return "v_cvt_f32_f16";
		case 0x0D: return "v_cvt_flr_i32_f32";
		case 0x11: return "v_cvt_f32_ubyte0";
		case 0x12: return "v_cvt_f32_ubyte1";
		case 0x13: return "v_cvt_f32_ubyte2";
		case 0x14: return "v_cvt_f32_ubyte3";
		case 0x20: return "v_fract_f32";
		case 0x21: return "v_trunc_f32";
		case 0x22: return "v_ceil_f32";
		case 0x23: return "v_rndne_f32";
		case 0x24: return "v_floor_f32";
		case 0x25: return "v_exp_f32";
		case 0x27: return "v_log_f32";
		case 0x2A: return "v_rcp_f32";
		case 0x2B: return "v_rcp_iflag_f32";
		case 0x2E: return "v_rsq_f32";
		case 0x33: return "v_sqrt_f32";
		case 0x35: return "v_sin_f32";
		case 0x36: return "v_cos_f32";
		case 0x37: return "v_not_b32";
		case 0x38: return "v_bfrev_b32";
		case 0x39: return "v_ffbh_u32";
		case 0x3A: return "v_ffbl_b32";
		case 0x3B: return "v_ffbh_i32";
		case 0x3F: return "v_frexp_exp_i32_f32";
		case 0x40: return "v_frexp_mant_f32";
		case 0x41: return "v_clrexcp";
		default:   return "";
		}
	}

	static const char* NameVop2(uint32_t op)
	{
		switch (op)
		{
		case 0x00: return "v_cndmask_b32";
		case 0x01: return "v_readlane_b32";
		case 0x02: return "v_writelane_b32";
		case 0x03: return "v_add_f32";
		case 0x04: return "v_sub_f32";
		case 0x05: return "v_subrev_f32";
		case 0x06: return "v_mac_legacy_f32";
		case 0x07: return "v_mul_legacy_f32";
		case 0x08: return "v_mul_f32";
		case 0x09: return "v_mul_i32_i24";
		case 0x0A: return "v_mul_hi_i32_i24";
		case 0x0B: return "v_mul_u32_u24";
		case 0x0C: return "v_mul_hi_u32_u24";
		case 0x0D: return "v_min_legacy_f32";
		case 0x0E: return "v_max_legacy_f32";
		case 0x0F: return "v_min_f32";
		case 0x10: return "v_max_f32";
		case 0x11: return "v_min_i32";
		case 0x12: return "v_max_i32";
		case 0x13: return "v_min_u32";
		case 0x14: return "v_max_u32";
		case 0x15: return "v_lshr_b32";
		case 0x16: return "v_lshrrev_b32";
		case 0x17: return "v_ashr_i32";
		case 0x18: return "v_ashrrev_i32";
		case 0x19: return "v_lshl_b32";
		case 0x1A: return "v_lshlrev_b32";
		case 0x1B: return "v_and_b32";
		case 0x1C: return "v_or_b32";
		case 0x1D: return "v_xor_b32";
		case 0x1E: return "v_bfm_b32";
		case 0x1F: return "v_mac_f32";
		case 0x20: return "v_madmk_f32";
		case 0x21: return "v_madak_f32";
		case 0x22: return "v_bcnt_u32_b32";
		case 0x23: return "v_mbcnt_lo_u32_b32";
		case 0x24: return "v_mbcnt_hi_u32_b32";
		case 0x25: return "v_add_i32";
		case 0x26: return "v_sub_i32";
		case 0x27: return "v_subrev_i32";
		case 0x28: return "v_addc_u32";
		case 0x29: return "v_subb_u32";
		case 0x2A: return "v_subbrev_u32";
		case 0x2B: return "v_ldexp_f32";
		case 0x2C: return "v_cvt_pkaccum_u8_f32";
		case 0x2D: return "v_cvt_pknorm_i16_f32";
		case 0x2E: return "v_cvt_pknorm_u16_f32";
		case 0x2F: return "v_cvt_pkrtz_f16_f32";
		default:   return "";
		}
	}

	// VOP3-only opcodes (0x140-0x17F). Everything below 0x140 and the 0x180+
	// block are re-encodings of VOPC/VOP2/VOP1 and are dispatched to those tables.
	static const char* NameVop3(uint32_t op)
	{
		switch (op)
		{
		case 0x140: return "v_mad_legacy_f32";
		case 0x141: return "v_mad_f32";
		case 0x142: return "v_mad_i32_i24";
		case 0x143: return "v_mad_u32_u24";
		case 0x144: return "v_cubeid_f32";
		case 0x145: return "v_cubesc_f32";
		case 0x146: return "v_cubetc_f32";
		case 0x147: return "v_cubema_f32";
		case 0x148: return "v_bfe_u32";
		case 0x149: return "v_bfe_i32";
		case 0x14A: return "v_bfi_b32";
		case 0x14B: return "v_fma_f32";
		case 0x14D: return "v_lerp_u8";
		case 0x14E: return "v_alignbit_b32";
		case 0x14F: return "v_alignbyte_b32";
		case 0x150: return "v_mullit_f32";
		case 0x151: return "v_min3_f32";
		case 0x152: return "v_min3_i32";
		case 0x153: return "v_min3_u32";
		case 0x154: return "v_max3_f32";
		case 0x155: return "v_max3_i32";
		case 0x156: return "v_max3_u32";
		case 0x157: return "v_med3_f32";
		case 0x158: return "v_med3_i32";
		case 0x159: return "v_med3_u32";
		case 0x15D: return "v_sad_u32";
		case 0x15F: return "v_div_fixup_f32";
		case 0x161: return "v_lshl_b64";
		case 0x162: return "v_lshr_b64";
		case 0x163: return "v_ashr_i64";
		case 0x169: return "v_mul_lo_u32";
		case 0x16A: return "v_mul_hi_u32";
		case 0x16B: return "v_mul_lo_i32";
		case 0x16C: return "v_mul_hi_i32";
		case 0x16D: return "v_div_scale_f32";
		case 0x16F: return "v_div_fmas_f32";
		case 0x171: return "v_msad_u8";
		case 0x176: return "v_mad_u64_u32";
		default:    return "";
		}
	}

	// VOPC compares, opcode [24:17]. The suffix set repeats per f32/i32/u32 block:
	// f32 at 0x00 (cmp) and 0x10 (cmpx), i32 at 0x80/0x90, u32 at 0xC0/0xD0.
	static const char* NameVopc(uint32_t op)
	{
		switch (op)
		{
		case 0x00: return "v_cmp_f_f32";
		case 0x01: return "v_cmp_lt_f32";
		case 0x02: return "v_cmp_eq_f32";
		case 0x03: return "v_cmp_le_f32";
		case 0x04: return "v_cmp_gt_f32";
		case 0x05: return "v_cmp_lg_f32";
		case 0x06: return "v_cmp_ge_f32";
		case 0x07: return "v_cmp_o_f32";
		case 0x08: return "v_cmp_u_f32";
		case 0x09: return "v_cmp_nge_f32";
		case 0x0A: return "v_cmp_nlg_f32";
		case 0x0B: return "v_cmp_ngt_f32";
		case 0x0C: return "v_cmp_nle_f32";
		case 0x0D: return "v_cmp_neq_f32";
		case 0x0E: return "v_cmp_nlt_f32";
		case 0x0F: return "v_cmp_tru_f32";
		case 0x11: return "v_cmpx_lt_f32";
		case 0x12: return "v_cmpx_eq_f32";
		case 0x13: return "v_cmpx_le_f32";
		case 0x14: return "v_cmpx_gt_f32";
		case 0x16: return "v_cmpx_ge_f32";
		case 0x1D: return "v_cmpx_neq_f32";
		case 0x80: return "v_cmp_f_i32";
		case 0x81: return "v_cmp_lt_i32";
		case 0x82: return "v_cmp_eq_i32";
		case 0x83: return "v_cmp_le_i32";
		case 0x84: return "v_cmp_gt_i32";
		case 0x85: return "v_cmp_ne_i32";
		case 0x86: return "v_cmp_ge_i32";
		case 0x87: return "v_cmp_t_i32";
		case 0x88: return "v_cmp_class_f32";
		case 0x91: return "v_cmpx_lt_i32";
		case 0x92: return "v_cmpx_eq_i32";
		case 0x95: return "v_cmpx_ne_i32";
		case 0xC0: return "v_cmp_f_u32";
		case 0xC1: return "v_cmp_lt_u32";
		case 0xC2: return "v_cmp_eq_u32";
		case 0xC3: return "v_cmp_le_u32";
		case 0xC4: return "v_cmp_gt_u32";
		case 0xC5: return "v_cmp_ne_u32";
		case 0xC6: return "v_cmp_ge_u32";
		case 0xC7: return "v_cmp_t_u32";
		case 0xD2: return "v_cmpx_eq_u32";
		case 0xD5: return "v_cmpx_ne_u32";
		default:   return "";
		}
	}

	static const char* NameVintrp(uint32_t op)
	{
		switch (op)
		{
		case 0x00: return "v_interp_p1_f32";
		case 0x01: return "v_interp_p2_f32";
		case 0x02: return "v_interp_mov_f32";
		default:   return "";
		}
	}

	static const char* NameVop3p(uint32_t op)
	{
		switch (op)
		{
		case 0x0E: return "v_pk_fma_f16";
		case 0x0F: return "v_pk_add_f16";
		case 0x10: return "v_pk_mul_f16";
		case 0x11: return "v_pk_min_f16";
		case 0x12: return "v_pk_max_f16";
		case 0x20: return "v_fma_mix_f32";
		case 0x21: return "v_fma_mixlo_f16";
		case 0x22: return "v_fma_mixhi_f16";
		default:   return "";
		}
	}

	// Shared by SMRD and SMEM.
	static const char* NameSmem(uint32_t op)
	{
		switch (op)
		{
		case 0x00: return "s_load_dword";
		case 0x01: return "s_load_dwordx2";
		case 0x02: return "s_load_dwordx4";
		case 0x03: return "s_load_dwordx8";
		case 0x04: return "s_load_dwordx16";
		case 0x08: return "s_buffer_load_dword";
		case 0x09: return "s_buffer_load_dwordx2";
		case 0x0A: return "s_buffer_load_dwordx4";
		case 0x0B: return "s_buffer_load_dwordx8";
		case 0x0C: return "s_buffer_load_dwordx16";
		default:   return "";
		}
	}

	static const char* NameDs(uint32_t op)
	{
		switch (op)
		{
		case 0x00: return "ds_add_u32";
		case 0x0D: return "ds_write_b32";
		case 0x0E: return "ds_write2_b32";
		case 0x0F: return "ds_write2st64_b32";
		case 0x1E: return "ds_write_b8";
		case 0x1F: return "ds_write_b16";
		case 0x20: return "ds_add_rtn_u32";
		case 0x27: return "ds_min_rtn_u32";
		case 0x28: return "ds_max_rtn_u32";
		case 0x29: return "ds_and_rtn_b32";
		case 0x2A: return "ds_or_rtn_b32";
		case 0x2B: return "ds_xor_rtn_b32";
		case 0x2D: return "ds_wrxchg_rtn_b32";
		case 0x30: return "ds_cmpst_rtn_b32";
		case 0x33: return "ds_max_rtn_f32";
		case 0x34: return "ds_wrap_rtn_b32";
		case 0x35: return "ds_swizzle_b32";
		case 0x36: return "ds_read_b32";
		case 0x37: return "ds_read2_b32";
		case 0x38: return "ds_read2st64_b32";
		case 0x39: return "ds_read_i8";
		case 0x3A: return "ds_read_u8";
		case 0x3B: return "ds_read_i16";
		case 0x3C: return "ds_read_u16";
		case 0x76: return "ds_read_b64";
		default:   return "";
		}
	}

	static const char* NameMubuf(uint32_t op)
	{
		switch (op)
		{
		case 0x00: return "buffer_load_format_x";
		case 0x01: return "buffer_load_format_xy";
		case 0x02: return "buffer_load_format_xyz";
		case 0x03: return "buffer_load_format_xyzw";
		case 0x04: return "buffer_store_format_x";
		case 0x07: return "buffer_store_format_xyzw";
		case 0x08: return "buffer_load_ubyte";
		case 0x09: return "buffer_load_sbyte";
		case 0x0A: return "buffer_load_ushort";
		case 0x0B: return "buffer_load_sshort";
		case 0x0C: return "buffer_load_dword";
		case 0x0D: return "buffer_load_dwordx2";
		case 0x0E: return "buffer_load_dwordx4";
		case 0x0F: return "buffer_load_dwordx3";
		case 0x18: return "buffer_store_byte";
		case 0x1A: return "buffer_store_short";
		case 0x1C: return "buffer_store_dword";
		case 0x1D: return "buffer_store_dwordx2";
		case 0x1E: return "buffer_store_dwordx4";
		case 0x1F: return "buffer_store_dwordx3";
		case 0x30: return "buffer_atomic_swap";
		case 0x32: return "buffer_atomic_add";
		default:   return "";
		}
	}

	// The sample group is 0x20-0x2F with its offset form at 0x30-0x3F; gather4 is
	// 0x40-0x4F with its offset form at 0x50-0x5F.
	static const char* NameMimg(uint32_t op)
	{
		switch (op)
		{
		case 0x00: return "image_load";
		case 0x08: return "image_store";
		case 0x0E: return "image_get_resinfo";
		case 0x0F: return "image_atomic_swap";
		case 0x11: return "image_atomic_add";
		case 0x20: return "image_sample";
		case 0x22: return "image_sample_d";
		case 0x24: return "image_sample_l";
		case 0x25: return "image_sample_b";
		case 0x27: return "image_sample_lz";
		case 0x2A: return "image_sample_c_d";
		case 0x2E: return "image_sample_c_b_cl";
		case 0x2F: return "image_sample_c_lz";
		case 0x37: return "image_sample_lz_o";
		case 0x3F: return "image_sample_c_lz_o";
		case 0x40: return "image_gather4";
		case 0x47: return "image_gather4_lz";
		case 0x4F: return "image_gather4_c_lz";
		case 0x57: return "image_gather4_lz_o";
		case 0x5F: return "image_gather4_c_lz_o";
		default:   return "";
		}
	}

	static std::string RegRange(char prefix, int base, int count)
	{
		char buffer[32];
		if (count <= 1)
		{
			_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%c%d", prefix, base);
		}
		else
		{
			_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%c[%d:%d]", prefix, base, base + count - 1);
		}
		return buffer;
	}

	static std::string FloatLit(float value)
	{
		char buffer[32];
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%g", (double)value);
		std::string text = buffer;
		if (text.find('.') == std::string::npos && text.find('e') == std::string::npos)
		{
			text += ".0";
		}
		return text;
	}

	// The 9-bit source field:
	//   0-101 SGPR0-101   102-103 flat_scratch   104-105 xnack   106-107 vcc_lo/hi
	//   124 m0   126-127 exec_lo/hi   128 zero   129-192 +1..+64   193-208 -1..-16
	//   240-247 +-0.5/1.0/2.0/4.0   255 literal in the next dword   256-511 VGPR0-255
	// The 8-bit scalar field used by SOP* is the same table truncated.
	static Operand DecodeSrcOperand(uint32_t code, uint32_t literal, int count = 1)
	{
		Operand operand;
		operand.count = count;
		if (code < 102)
		{
			operand.kind = Operand::Kind::Sgpr;
			operand.reg = (int)code;
			operand.text = RegRange('s', operand.reg, count);
			return operand;
		}
		if (code >= 256)
		{
			operand.kind = Operand::Kind::Vgpr;
			operand.reg = (int)(code - 256);
			operand.text = RegRange('v', operand.reg, count);
			return operand;
		}
		operand.kind = Operand::Kind::Special;
		switch (code)
		{
		// Referenced as a 64-bit pair these are named without the _lo suffix.
		case 102: operand.text = count > 1 ? "flat_scratch" : "flat_scratch_lo"; return operand;
		case 103: operand.text = "flat_scratch_hi"; return operand;
		case 106: operand.text = count > 1 ? "vcc" : "vcc_lo"; return operand;
		case 107: operand.text = "vcc_hi"; return operand;
		case 124: operand.text = "m0"; return operand;
		case 126: operand.text = count > 1 ? "exec" : "exec_lo"; return operand;
		case 127: operand.text = "exec_hi"; return operand;
		case 255:
		{
			operand.kind = Operand::Kind::Literal;
			operand.bits = literal;
			char buffer[16];
			_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "0x%x", literal);
			operand.text = buffer;
			return operand;
		}
		default: break;
		}

		operand.kind = Operand::Kind::InlineConst;
		if (code == 128)
		{
			operand.fval = 0.0f;
			operand.text = "0";
			return operand;
		}
		if (code >= 129 && code <= 192)
		{
			operand.fval = (float)(code - 128);
			operand.text = std::to_string(code - 128);
			return operand;
		}
		if (code >= 193 && code <= 208)
		{
			operand.fval = -(float)(code - 192);
			operand.text = std::to_string(-(int)(code - 192));
			return operand;
		}
		switch (code)
		{
		case 240: operand.fval =  0.5f; break;
		case 241: operand.fval = -0.5f; break;
		case 242: operand.fval =  1.0f; break;
		case 243: operand.fval = -1.0f; break;
		case 244: operand.fval =  2.0f; break;
		case 245: operand.fval = -2.0f; break;
		case 246: operand.fval =  4.0f; break;
		case 247: operand.fval = -4.0f; break;
		default:
			operand.kind = Operand::Kind::Special;
			operand.text = "unk" + std::to_string(code);
			return operand;
		}
		operand.text = FloatLit(operand.fval);
		return operand;
	}

	// Keyed off the mnemonic suffix, which is how the ISA names them.
	static bool Is64BitScalar(const std::string& name)
	{
		if (name.size() >= 4)
		{
			std::string suffix = name.substr(name.size() - 4);
			if (suffix == "_b64" || suffix == "_i64" || suffix == "_u64")
			{
				return true;
			}
		}
		return false;
	}

	static Operand MakeVgpr(int reg, int count = 1)
	{
		Operand operand;
		operand.kind = Operand::Kind::Vgpr;
		operand.reg = reg;
		operand.count = count;
		operand.text = RegRange('v', reg, count);
		return operand;
	}

	// A scalar DESTINATION field uses the source code space, so indices >= 102 are
	// the special registers, not SGPR 102+.
	static Operand MakeSgpr(int reg, int count = 1)
	{
		Operand operand;
		operand.count = count;
		if (reg >= 102)
		{
			operand.kind = Operand::Kind::Special;
			switch (reg)
			{
			case 102: operand.text = count > 1 ? "flat_scratch" : "flat_scratch_lo"; return operand;
			case 103: operand.text = "flat_scratch_hi"; return operand;
			case 106: operand.text = count > 1 ? "vcc" : "vcc_lo"; return operand;
			case 107: operand.text = "vcc_hi"; return operand;
			case 124: operand.text = "m0"; return operand;
			case 126: operand.text = count > 1 ? "exec" : "exec_lo"; return operand;
			case 127: operand.text = "exec_hi"; return operand;
			default: break;
			}
		}
		operand.kind = Operand::Kind::Sgpr;
		operand.reg = reg;
		operand.text = RegRange('s', reg, count);
		return operand;
	}

	static Operand MakeSpecial(const char* text)
	{
		Operand operand;
		operand.kind = Operand::Kind::Special;
		operand.text = text;
		return operand;
	}

	// Re-render the token so the emitter sees the syntax llvm-mc would print.
	static void ApplyOperandMods(Operand& operand, bool abs, bool neg)
	{
		operand.abs = abs;
		operand.neg = neg;
		if (abs)
		{
			operand.text = "|" + operand.text + "|";
		}
		if (neg)
		{
			operand.text = "-" + operand.text;
		}
	}

	static void AddModifier(Instruction& instruction, const char* key, const std::string& value)
	{
		instruction.mods.push_back({ key, value });
	}

	static Operand MakeLiteral(uint32_t bits, const char* format = "0x%x")
	{
		Operand operand;
		operand.kind = Operand::Kind::Literal;
		operand.bits = bits;
		char buffer[24];
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, bits);
		operand.text = buffer;
		return operand;
	}

	static int VectorCountSuffix(const std::string& name)
	{
		if (name.size() > 3 && name.rfind("x16") == name.size() - 3) return 16;
		if (name.size() > 2 && name.rfind("x8") == name.size() - 2)  return 8;
		if (name.size() > 2 && name.rfind("x4") == name.size() - 2)  return 4;
		if (name.size() > 2 && name.rfind("x2") == name.size() - 2)  return 2;
		return 1;
	}

	std::string FormatInstruction(const Instruction& instruction)
	{
		std::string text = instruction.name.empty() ? "<unknown>" : instruction.name;
		// A VOPC/VOP2/VOP1 op re-encoded in VOP3 form must carry the _e64 suffix,
		// else the assembler picks the shorter native encoding and a round-trip
		// compares a 1-dword result against our 2-dword instruction.
		if (instruction.encoding == Encoding::Vop3 && !instruction.words.empty())
		{
			uint32_t op = (instruction.words[0] >> 17) & 0x1FF;
			if (op < 0x140 || op >= 0x180)
			{
				text += "_e64";
			}
		}
		for (size_t i = 0; i < instruction.ops.size(); ++i)
		{
			text += (i ? ", " : " ") + instruction.ops[i].text;
		}
		if (instruction.clamp)
		{
			text += " clamp";
		}
		// omod 3 is a halve, which the assembler spells "div:2".
		if (instruction.omod)
		{
			text += (instruction.omod == 1 ? " mul:2" : instruction.omod == 2 ? " mul:4" : " div:2");
		}
		for (const auto& modifier : instruction.mods)
		{
			text += " " + modifier.first;
			if (!modifier.second.empty())
			{
				text += ":" + modifier.second;
			}
		}
		text += " ; encoding: [";
		for (size_t word = 0; word < instruction.words.size(); ++word)
		{
			for (int byte = 0; byte < 4; ++byte)
			{
				char buffer[8];
				_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "0x%02x",
				            (instruction.words[word] >> (8 * byte)) & 0xFF);
				if (word || byte)
				{
					text += ",";
				}
				text += buffer;
			}
		}
		text += "]";
		return text;
	}

	// Called after the encoding/name/width pass, so instruction.words is complete.
	static void DecodeOperands(Instruction& instruction)
	{
		if (instruction.words.empty())
		{
			return;
		}
		const uint32_t word0 = instruction.words[0];
		const uint32_t word1 = instruction.words.size() > 1 ? instruction.words[1] : 0u;
		const uint32_t literal = instruction.words.size() > 1 ? instruction.words.back() : 0u;
		const std::string& name = instruction.name;

		switch (instruction.encoding)
		{
		case Encoding::Vop1:
		{
			// v_readfirstlane_b32 moves lane 0 into a scalar register.
			if (name == "v_readfirstlane_b32")
			{
				instruction.ops.push_back(MakeSgpr((int)((word0 >> 17) & 0xFF)));
			}
			else
			{
				instruction.ops.push_back(MakeVgpr((int)((word0 >> 17) & 0xFF)));
			}
			instruction.ops.push_back(DecodeSrcOperand(word0 & 0x1FF, literal));
			break;
		}
		case Encoding::Vop2:
		{
			instruction.ops.push_back(MakeVgpr((int)((word0 >> 17) & 0xFF)));
			// The integer add/sub family writes its carry to VCC and the assembler
			// requires that destination written explicitly; addc/subb also read it.
			bool carryOut = name == "v_add_i32" || name == "v_sub_i32" || name == "v_subrev_i32" ||
			                name == "v_addc_u32" || name == "v_subb_u32" || name == "v_subbrev_u32";
			bool carryIn = name == "v_addc_u32" || name == "v_subb_u32" || name == "v_subbrev_u32";
			if (carryOut)
			{
				instruction.ops.push_back(MakeSpecial("vcc"));
			}
			instruction.ops.push_back(DecodeSrcOperand(word0 & 0x1FF, literal));
			instruction.ops.push_back(MakeVgpr((int)((word0 >> 9) & 0xFF)));
			if (carryIn)
			{
				instruction.ops.push_back(MakeSpecial("vcc"));
			}
			// readlane writes a scalar dst, and both lane ops take a scalar lane
			// select rather than the VGPR the generic path assumes.
			if (name == "v_readlane_b32" || name == "v_writelane_b32")
			{
				instruction.ops.clear();
				instruction.ops.push_back(name == "v_readlane_b32"
				                              ? MakeSgpr((int)((word0 >> 17) & 0xFF))
				                              : MakeVgpr((int)((word0 >> 17) & 0xFF)));
				instruction.ops.push_back(DecodeSrcOperand(word0 & 0x1FF, literal));
				instruction.ops.push_back(DecodeSrcOperand((word0 >> 9) & 0xFF, literal));
				break;
			}
			// madmk takes the literal as its middle source (D = S0*K + S1), madak
			// as its last (D = S0*S1 + K).
			if (name == "v_madmk_f32" || name == "v_madak_f32")
			{
				Operand constant = MakeLiteral(literal);
				if (name == "v_madmk_f32")
				{
					instruction.ops.insert(instruction.ops.begin() + 2, constant);
				}
				else
				{
					instruction.ops.push_back(constant);
				}
			}
			break;
		}
		case Encoding::Vopc:
		{
			instruction.ops.push_back(MakeSpecial("vcc"));
			instruction.ops.push_back(DecodeSrcOperand(word0 & 0x1FF, literal));
			instruction.ops.push_back(MakeVgpr((int)((word0 >> 9) & 0xFF)));
			break;
		}
		case Encoding::Vop3:
		{
			// VOP3a: vdst[7:0] abs[10:8] clamp[11] op[25:17], then
			//        src0[8:0] src1[17:9] src2[26:18] omod[28:27] neg[31:29].
			// VOP3b (the ops writing a second scalar result) replaces abs/clamp
			// with sdst[14:8].
			unsigned vdst = word0 & 0xFF;
			unsigned absFlags = (word0 >> 8) & 0x7;
			unsigned op = (word0 >> 17) & 0x1FF;
			instruction.omod = (word1 >> 27) & 0x3;
			unsigned negFlags = (word1 >> 29) & 0x7;
			bool vop3b = name == "v_add_i32" || name == "v_sub_i32" || name == "v_subrev_i32" ||
			             name == "v_addc_u32" || name == "v_subb_u32" || name == "v_subbrev_u32" ||
			             name == "v_div_scale_f32" || name == "v_mad_u64_u32" || name == "v_mad_i64_i32";
			bool is64 = name == "v_lshl_b64" || name == "v_lshr_b64" || name == "v_ashr_i64";
			if (vop3b)
			{
				absFlags = 0;
				instruction.clamp = false;
			}
			else
			{
				instruction.clamp = ((word0 >> 11) & 1) != 0;
			}
			// The VOPC re-encode range writes a 64-bit SGPR pair, not a VGPR.
			if (op < 0x100)
			{
				instruction.ops.push_back(MakeSgpr((int)vdst, 2));
			}
			else if (is64)
			{
				instruction.ops.push_back(MakeVgpr((int)vdst, 2));
			}
			else
			{
				instruction.ops.push_back(MakeVgpr((int)vdst));
			}
			if (vop3b)
			{
				instruction.ops.push_back(MakeSgpr((int)((word0 >> 8) & 0x7F), 2));
			}
			// Source count: the VOP1 re-encode range has one, the VOPC/VOP2 ranges
			// two, native VOP3 up to three.
			bool thirdSourceIsMask = name == "v_cndmask_b32" || name == "v_addc_u32" ||
			                         name == "v_subb_u32" || name == "v_subbrev_u32";
			int sourceCount;
			if (op >= 0x180)          sourceCount = 1;
			else if (thirdSourceIsMask) sourceCount = 3;
			else if (op < 0x140)      sourceCount = 2;
			else if (name.rfind("v_mad", 0) == 0 || name.rfind("v_fma", 0) == 0 ||
			         name.rfind("v_med3", 0) == 0 || name.rfind("v_min3", 0) == 0 ||
			         name.rfind("v_max3", 0) == 0 || name.rfind("v_bfe", 0) == 0 ||
			         name == "v_bfi_b32" || name == "v_alignbit_b32" ||
			         name == "v_alignbyte_b32" || name == "v_sad_u32" ||
			         name == "v_lerp_u8" || name == "v_div_fixup_f32" ||
			         name == "v_div_fmas_f32" || name == "v_msad_u8" ||
			         name == "v_cubeid_f32" || name == "v_cubesc_f32" ||
			         name == "v_cubetc_f32" || name == "v_cubema_f32" ||
			         name == "v_mullit_f32") sourceCount = 3;
			else                      sourceCount = 2;
			const uint32_t codes[3] = { word1 & 0x1FF, (word1 >> 9) & 0x1FF, (word1 >> 18) & 0x1FF };
			for (int i = 0; i < sourceCount; ++i)
			{
				// The condition/carry-in source is a 64-bit pair; the 64-bit shifts
				// take a pair as src0 but a 32-bit shift amount.
				int width = 1;
				if (i == 2 && thirdSourceIsMask)
				{
					width = 2;
				}
				if (i == 0 && is64)
				{
					width = 2;
				}
				Operand operand = DecodeSrcOperand(codes[i], literal, width);
				ApplyOperandMods(operand, (absFlags >> i) & 1, (negFlags >> i) & 1);
				instruction.ops.push_back(operand);
			}
			break;
		}
		case Encoding::Smrd:
		{
			unsigned op = (word0 >> 22) & 0x1F;
			unsigned sdst = (word0 >> 15) & 0x7F;
			unsigned sbase = (word0 >> 9) & 0x3F;
			unsigned imm = (word0 >> 8) & 1;
			unsigned offset = word0 & 0xFF;
			instruction.ops.push_back(MakeSgpr((int)sdst, VectorCountSuffix(name)));
			// sbase is a 6-bit field naming an even-aligned SGPR pair or quad.
			instruction.ops.push_back(MakeSgpr((int)(sbase * 2), op <= 4 ? 2 : 4));
			if (imm)
			{
				instruction.ops.push_back(MakeLiteral(offset));
			}
			else if (offset == 0xFF)
			{
				// Literal-offset form, in dwords. Rendered distinctly so constant
				// recovery skips it: these are typically structured-buffer reads
				// through an SRV-table V#, not b0 cbuffer reads.
				instruction.ops.push_back(MakeLiteral(literal, "lit:0x%x"));
			}
			else
			{
				instruction.ops.push_back(MakeSgpr((int)offset));
			}
			break;
		}
		case Encoding::Sop1:
		{
			// 64-bit scalar ops address register pairs; rendering singles made the
			// assembler reject most of the wave control flow.
			int width = Is64BitScalar(name) ? 2 : 1;
			// The bit-counting ops are named for their source width but write a
			// 32-bit result: s_bcnt1_i32_b64 s0, s[2:3].
			int dstWidth = (name.rfind("s_bcnt", 0) == 0 || name.rfind("s_ff", 0) == 0 ||
			                name.rfind("s_flbit", 0) == 0) ? 1 : width;
			instruction.ops.push_back(MakeSgpr((int)((word0 >> 16) & 0x7F), dstWidth));
			if (name != "s_getpc_b64")   // has a destination but no source
			{
				instruction.ops.push_back(DecodeSrcOperand(word0 & 0xFF, literal, width));
			}
			break;
		}
		case Encoding::Sop2:
		{
			int width = Is64BitScalar(name) ? 2 : 1;
			// The 64-bit shifts take a 64-bit destination and first source but a
			// 32-bit shift amount.
			bool shift = name.rfind("s_lshl_b64", 0) == 0 || name.rfind("s_lshr_b64", 0) == 0 ||
			             name.rfind("s_ashr_i64", 0) == 0;
			instruction.ops.push_back(MakeSgpr((int)((word0 >> 16) & 0x7F), width));
			instruction.ops.push_back(DecodeSrcOperand(word0 & 0xFF, literal, width));
			instruction.ops.push_back(DecodeSrcOperand((word0 >> 8) & 0xFF, literal, shift ? 1 : width));
			break;
		}
		case Encoding::Sopc:
		{
			int width = Is64BitScalar(name) ? 2 : 1;
			instruction.ops.push_back(DecodeSrcOperand(word0 & 0xFF, literal, width));
			instruction.ops.push_back(DecodeSrcOperand((word0 >> 8) & 0xFF, literal, width));
			break;
		}
		case Encoding::Sopk:
		{
			instruction.ops.push_back(MakeSgpr((int)((word0 >> 16) & 0x7F)));
			instruction.ops.push_back(MakeLiteral(word0 & 0xFFFF));
			break;
		}
		case Encoding::Sopp:
		{
			// Most SOPP ops take no operand at all; emitting the raw simm field for
			// them is a syntax error.
			bool takesImm = name.rfind("s_branch", 0) == 0 || name.rfind("s_cbranch", 0) == 0 ||
			                name == "s_waitcnt" || name == "s_sleep" || name == "s_setprio" ||
			                name == "s_sethalt" || name == "s_nop" || name == "s_sendmsg";
			if (!takesImm)
			{
				break;
			}
			// Branch targets are a signed 16-bit simm in instruction units, and the
			// emitter reads them with atol, so print signed decimal not raw hex.
			int16_t simm = (int16_t)(word0 & 0xFFFF);
			Operand operand;
			operand.kind = Operand::Kind::Literal;
			operand.bits = (uint32_t)(int32_t)simm;
			operand.text = std::to_string((int)simm);
			instruction.ops.push_back(operand);
			break;
		}
		case Encoding::Vintrp:
		{
			unsigned vdst = (word0 >> 18) & 0xFF;
			unsigned channel = (word0 >> 8) & 0x3;
			unsigned attribute = (word0 >> 10) & 0x3F;
			unsigned vsrc = word0 & 0xFF;
			instruction.ops.push_back(MakeVgpr((int)vdst));
			if (name == "v_interp_mov_f32")
			{
				// Here vsrc is the parameter slot selector, not a register.
				Operand parameter;
				parameter.kind = Operand::Kind::Special;
				parameter.text = vsrc == 0 ? "p10" : vsrc == 1 ? "p20" : "p0";
				instruction.ops.push_back(parameter);
			}
			else
			{
				instruction.ops.push_back(MakeVgpr((int)vsrc));
			}
			Operand attributeOperand;
			attributeOperand.kind = Operand::Kind::Special;
			attributeOperand.text = "attr" + std::to_string(attribute) + "." +
			                        std::string(1, "xyzw"[channel]);
			instruction.ops.push_back(attributeOperand);
			break;
		}
		case Encoding::Mimg:
		{
			unsigned dmask = (word0 >> 8) & 0xF;
			unsigned vdata = (word1 >> 8) & 0xFF;
			unsigned vaddr = word1 & 0xFF;
			unsigned srsrc = (word1 >> 16) & 0x1F;
			unsigned ssamp = (word1 >> 21) & 0x1F;
			int channels = 0;
			for (int i = 0; i < 4; ++i)
			{
				if (dmask & (1u << i))
				{
					++channels;
				}
			}
			if (name.rfind("image_gather4", 0) == 0)   // always returns four texels
			{
				channels = 4;
			}
			instruction.ops.push_back(MakeVgpr((int)vdata, channels ? channels : 1));
			instruction.ops.push_back(MakeVgpr((int)vaddr, 4));
			instruction.ops.push_back(MakeSgpr((int)(srsrc * 4), 8));
			// Only the sampling ops take a sampler; naming one on load/store/atomic
			// is a syntax error.
			if (name.rfind("image_sample", 0) == 0 || name.rfind("image_gather", 0) == 0)
			{
				instruction.ops.push_back(MakeSgpr((int)(ssamp * 4), 4));
			}
			{
				char buffer[16];
				_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "0x%x", dmask);
				AddModifier(instruction, "dmask", buffer);
			}
			if ((word0 >> 12) & 1) AddModifier(instruction, "unrm", "");
			if ((word0 >> 13) & 1) AddModifier(instruction, "glc", "");
			if ((word0 >> 14) & 1) AddModifier(instruction, "da", "");
			if ((word0 >> 15) & 1) AddModifier(instruction, "r128", "");
			if ((word0 >> 16) & 1) AddModifier(instruction, "tfe", "");
			if ((word0 >> 17) & 1) AddModifier(instruction, "lwe", "");
			break;
		}
		case Encoding::Mubuf:
		{
			unsigned vdata = (word1 >> 8) & 0xFF;
			unsigned vaddr = word1 & 0xFF;
			unsigned srsrc = (word1 >> 16) & 0x1F;
			unsigned soffset = (word1 >> 24) & 0xFF;
			unsigned offset = word0 & 0xFFF;
			int channels = 1;
			if (name.size() > 2 && name.rfind("x2") == name.size() - 2)      channels = 2;
			else if (name.size() > 2 && name.rfind("x3") == name.size() - 2) channels = 3;
			else if (name.size() > 2 && name.rfind("x4") == name.size() - 2) channels = 4;
			else if (name.rfind("buffer_load_format_xyzw", 0) == 0 ||
			         name.rfind("buffer_store_format_xyzw", 0) == 0) channels = 4;
			instruction.ops.push_back(MakeVgpr((int)vdata, channels));
			instruction.ops.push_back(MakeVgpr((int)vaddr));
			instruction.ops.push_back(MakeSgpr((int)(srsrc * 4), 4));
			instruction.ops.push_back(DecodeSrcOperand(soffset, literal));
			// The addressing-mode flags are not optional: without offen/idxen the
			// assembler rejects the line outright.
			if ((word0 >> 12) & 1) AddModifier(instruction, "offen", "");
			if ((word0 >> 13) & 1) AddModifier(instruction, "idxen", "");
			if ((word0 >> 14) & 1) AddModifier(instruction, "glc", "");
			if ((word0 >> 16) & 1) AddModifier(instruction, "lds", "");
			if ((word1 >> 22) & 1) AddModifier(instruction, "slc", "");
			if ((word1 >> 23) & 1) AddModifier(instruction, "tfe", "");
			if (offset) AddModifier(instruction, "offset", std::to_string(offset));
			break;
		}
		case Encoding::Exp:
		{
			// word0: en[3:0] target[9:4] compr[10] done[11] vm[12]
			unsigned target = (word0 >> 4) & 0x3F;
			bool compressed = ((word0 >> 10) & 1) != 0;
			Operand targetOperand;
			targetOperand.kind = Operand::Kind::Special;
			if (target < 8)                        targetOperand.text = "mrt" + std::to_string(target);
			else if (target == 8)                  targetOperand.text = "mrtz";
			else if (target == 9)                  targetOperand.text = "null";
			else if (target >= 12 && target <= 15) targetOperand.text = "pos" + std::to_string(target - 12);
			else if (target >= 32)                 targetOperand.text = "param" + std::to_string(target - 32);
			else                                   targetOperand.text = "invalid_target";
			instruction.ops.push_back(targetOperand);
			// With compr the four channels pack into two VGPRs and the upper two
			// source fields are unused.
			int sourceCount = compressed ? 2 : 4;
			unsigned enable = word0 & 0xF;
			for (int i = 0; i < sourceCount; ++i)
			{
				// A channel disabled in `en` is written "off"; naming a register
				// there re-encodes as en=0xF instead of en=0.
				bool live = compressed ? ((enable >> (i * 2)) & 0x3) != 0 : ((enable >> i) & 1) != 0;
				instruction.ops.push_back(live ? MakeVgpr((int)((word1 >> (8 * i)) & 0xFF))
				                               : MakeSpecial("off"));
			}
			if (compressed)        AddModifier(instruction, "compr", "");
			if ((word0 >> 11) & 1) AddModifier(instruction, "done", "");
			if ((word0 >> 12) & 1) AddModifier(instruction, "vm", "");
			break;
		}
		case Encoding::Ds:
		{
			unsigned vdst = (word1 >> 24) & 0xFF;
			unsigned addr = word1 & 0xFF;
			unsigned data0 = (word1 >> 8) & 0xFF;
			unsigned data1 = (word1 >> 16) & 0xFF;
			unsigned offset0 = word0 & 0xFF;
			unsigned offset1 = (word0 >> 8) & 0xFF;
			bool isRead = name.rfind("ds_read", 0) == 0;
			int readCount = (name.rfind("ds_read2", 0) == 0 || name == "ds_read_b64") ? 2 : 1;
			if (isRead)
			{
				instruction.ops.push_back(MakeVgpr((int)vdst, readCount));
				instruction.ops.push_back(MakeVgpr((int)addr));
			}
			else
			{
				instruction.ops.push_back(MakeVgpr((int)addr));
				instruction.ops.push_back(MakeVgpr((int)data0));
				if (data1)
				{
					instruction.ops.push_back(MakeVgpr((int)data1));
				}
			}
			if (offset0) AddModifier(instruction, "offset0", std::to_string(offset0));
			if (offset1) AddModifier(instruction, "offset1", std::to_string(offset1));
			break;
		}
		default: break;
		}
	}

	// `extra` is the following dword for formats whose width depends on it, or 0
	// past the end of the buffer, where the caller catches the overrun.
	static bool DecodeEncoding(uint32_t word, uint32_t extra, Encoding& encoding,
	                           std::string& name, uint32_t& sizeDwords)
	{
		encoding = Encoding::Unknown;
		name.clear();
		sizeDwords = 1;

		// Vector ALU 32-bit block: top bit clear.
		if ((word & 0x80000000u) == 0)
		{
			uint32_t vop = (word >> 25) & 0x3F;
			uint32_t src0 = word & 0x1FF;
			if (vop == 0x3F)
			{
				encoding = Encoding::Vop1;
				name = NameVop1((word >> 9) & 0xFF);
				sizeDwords = Vop9BitPullsExtra(src0) ? 2u : 1u;
				return true;
			}
			if (vop == 0x3E)
			{
				encoding = Encoding::Vopc;
				name = NameVopc((word >> 17) & 0xFF);
				sizeDwords = Vop9BitPullsExtra(src0) ? 2u : 1u;
				return true;
			}
			encoding = Encoding::Vop2;
			// Only madmk and madak carry an inline 32-bit constant. Counting one
			// for 0x2C/0x2D (the RDNA2 fmamk/fmaak numbering) would consume the
			// next instruction and desync the whole stream.
			bool inlineConst = (vop == 0x20 || vop == 0x21);
			name = NameVop2(vop);
			sizeDwords = (inlineConst || Vop9BitPullsExtra(src0)) ? 2u : 1u;
			return true;
		}

		// SMRD, the legacy scalar memory read.
		if ((word & 0xF8000000u) == 0xC0000000u)
		{
			encoding = Encoding::Smrd;
			uint32_t offset = word & 0xFF;
			bool imm = ((word >> 8) & 1) != 0;
			name = NameSmem((word >> 22) & 0x1F);
			sizeDwords = (!imm && offset == 0xFF) ? 2u : 1u;
			return true;
		}

		// Scalar ALU / control.
		if ((word & 0xC0000000u) == 0x80000000u)
		{
			uint32_t sop = (word >> 23) & 0x7F;
			uint32_t src0 = word & 0xFF;
			uint32_t src1 = (word >> 8) & 0xFF;
			if (sop == 0x7D)
			{
				encoding = Encoding::Sop1;
				name = NameSop1((word >> 8) & 0xFF);
				sizeDwords = SrcIsLiteral(src0) ? 2u : 1u;
				return true;
			}
			if (sop == 0x7E)
			{
				encoding = Encoding::Sopc;
				name = NameSopc((word >> 16) & 0x7F);
				sizeDwords = (SrcIsLiteral(src0) || SrcIsLiteral(src1)) ? 2u : 1u;
				return true;
			}
			if (sop == 0x7F)
			{
				encoding = Encoding::Sopp;
				name = NameSopp((word >> 16) & 0x7F);
				return true;
			}
			if (sop >= 0x60)
			{
				encoding = Encoding::Sopk;
				name = NameSopk(sop - 0x60);
				return true;
			}
			encoding = Encoding::Sop2;
			name = NameSop2(sop);
			sizeDwords = (SrcIsLiteral(src0) || SrcIsLiteral(src1)) ? 2u : 1u;
			return true;
		}

		// VOP3P, packed 16-bit math.
		if ((word & 0xFF800000u) == 0xCC000000u)
		{
			encoding = Encoding::Vop3p;
			uint32_t src0 = extra & 0x1FF;
			uint32_t src1 = (extra >> 9) & 0x1FF;
			uint32_t src2 = (extra >> 18) & 0x1FF;
			name = NameVop3p((word >> 16) & 0x7F);
			sizeDwords = (src0 == 0xFF || src1 == 0xFF || src2 == 0xFF) ? 3u : 2u;
			return true;
		}

		// Remaining formats keyed by the 6-bit major opcode.
		switch (word >> 26)
		{
		case 0x33:
		case 0x3D:
			encoding = Encoding::Smem;
			name = NameSmem((word >> 18) & 0xFF);
			sizeDwords = 2;
			return true;
		case 0x32:
			encoding = Encoding::Vintrp;
			name = NameVintrp((word >> 16) & 0x3);
			return true;
		case 0x34:
		case 0x35:
		{
			encoding = Encoding::Vop3;
			// The opcode is 9 bits at [25:17]. RDNA2 widened it to 10 bits at
			// [25:16]; reading it that way shifts every VOP3 opcode by one bit and
			// nothing matches the table.
			uint32_t op = (word >> 17) & 0x1FF;
			uint32_t src0 = extra & 0x1FF;
			uint32_t src1 = (extra >> 9) & 0x1FF;
			uint32_t src2 = (extra >> 18) & 0x1FF;
			// VOP3 re-encodes other formats at fixed offsets: VOPC +0x000,
			// VOP2 +0x100, VOP1 +0x180, VINTERP +0x270.
			if (op < 0x100)                     name = NameVopc(op);
			else if (op >= 0x100 && op < 0x140) name = NameVop2(op - 0x100);
			else if (op >= 0x180 && op < 0x200) name = NameVop1(op - 0x180);
			else if (op >= 0x270 && op < 0x280) name = NameVintrp(op - 0x270);
			else                                name = NameVop3(op);
			sizeDwords = (src0 == 0xFF || src1 == 0xFF || src2 == 0xFF) ? 3u : 2u;
			return true;
		}
		case 0x36:
			encoding = Encoding::Ds;
			name = NameDs((word >> 18) & 0xFF);
			sizeDwords = 2;
			return true;
		case 0x37:
			encoding = Encoding::Flat;
			sizeDwords = 2;
			return true;
		case 0x38:
			encoding = Encoding::Mubuf;
			name = NameMubuf((word >> 18) & 0x7F);
			sizeDwords = ((extra >> 24) == 0xFF) ? 3u : 2u;
			return true;
		case 0x3A:
			encoding = Encoding::Mtbuf;
			sizeDwords = ((extra >> 24) == 0xFF) ? 3u : 2u;
			return true;
		case 0x3C:
			encoding = Encoding::Mimg;
			name = NameMimg((word >> 18) & 0x7F);
			sizeDwords = 2 + ((word >> 1) & 0x3);
			return true;
		case 0x3E:
			encoding = Encoding::Exp;
			name = "exp";
			sizeDwords = 2;
			return true;
		default:
			return false;
		}
	}

	bool DecodeProgram(const uint32_t* words, size_t wordCount, Program& out, std::string& error,
	                   uint64_t baseAddress)
	{
		out = Program{};
		out.address = baseAddress;
		error.clear();
		if (!words || wordCount == 0)
		{
			error = "empty";
			return false;
		}

		constexpr size_t kMaxInstructions = 4096;
		size_t index = 0;
		while (out.instructions.size() < kMaxInstructions)
		{
			if (index >= wordCount)
			{
				error = "unterminated (ran off the end before s_endpgm)";
				return false;
			}

			uint32_t word = words[index];
			// Zero is inter-shader padding, not an instruction start. It would
			// decode as a bogus VOP2 and let a scan swallow a zero-filled gap as
			// hundreds of fake instructions.
			if (word == 0)
			{
				error = "zero/padding word (not an instruction)";
				return false;
			}
			uint32_t extra = (index + 1 < wordCount) ? words[index + 1] : 0u;
			Encoding encoding;
			std::string name;
			uint32_t size = 1;
			if (!DecodeEncoding(word, extra, encoding, name, size))
			{
				char buffer[96];
				_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
				            "unknown-encoding at dword %zu word=0x%08X", index, word);
				error = buffer;
				return false;
			}
			if (size == 0 || index + size > wordCount)
			{
				error = "instruction overruns buffer";
				return false;
			}

			Instruction instruction;
			instruction.pc = static_cast<uint32_t>(index * sizeof(uint32_t));
			instruction.encoding = encoding;
			instruction.name = name;
			instruction.words.assign(words + index, words + index + size);
			DecodeOperands(instruction);
			if (name.empty())
			{
				out.unknownCount++;
			}
			out.instructions.push_back(std::move(instruction));

			index += size;

			if (encoding == Encoding::Sopp && ((word >> 16) & 0x7F) == 0x01)   // s_endpgm
			{
				out.terminated = true;
				return true;
			}
		}
		error = "instruction cap reached without s_endpgm";
		return false;
	}

	volatile LONG GScanAttempts = 0;
	volatile LONG64 GScanInstructions = 0;

	bool LocateProgram(const uint32_t* words, size_t wordCount,
	                   size_t& startWord, Program& out, std::string& error)
	{
		startWord = 0;
		out = Program{};
		error = "no decodable GCN stream found";
		if (!words || wordCount == 0)
		{
			return false;
		}

		// The real shader is the LONGEST run decoding cleanly to s_endpgm: a
		// correct start stays aligned through the whole body, a mis-aligned one
		// desyncs and dies early. Do not gate on how many opcodes are named -- an
		// instruction can decode at the right width without a mnemonic, and doing
		// so once discarded a true 472-instruction decode for a lucky 13-instruction
		// tail.
		bool found = false;
		size_t bestStart = 0;
		size_t bestInstructions = 0;
		Program program;
		std::string decodeError;
		std::vector<bool> covered(wordCount, false);
		for (size_t start = 0; start < wordCount; ++start)
		{
			if (covered[start])
			{
				continue;
			}
			program.instructions.clear();
			program.terminated = false;
			program.unknownCount = 0;
			InterlockedIncrement(&GScanAttempts);
			if (!DecodeProgram(words + start, wordCount - start, program, decodeError, 0))
			{
				continue;
			}
			InterlockedAdd64(&GScanInstructions, (LONG64)program.instructions.size());
			for (const Instruction& instruction : program.instructions)
			{
				size_t boundary = start + instruction.pc / 4;
				if (boundary > start && boundary < wordCount)
				{
					covered[boundary] = true;
				}
			}
			if (!program.terminated || program.instructions.size() < 8)
			{
				continue;
			}
			if (!found || program.instructions.size() > bestInstructions)
			{
				found = true;
				bestStart = start;
				bestInstructions = program.instructions.size();
			}
		}
		if (!found)
		{
			return false;
		}

		startWord = bestStart;
		DecodeProgram(words + bestStart, wordCount - bestStart, out, decodeError, 0);
		error.clear();
		return true;
	}
}
