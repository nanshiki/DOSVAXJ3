/*
 *  Copyright (C) 2019 takapyu
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "dosbox.h"
#include "regs.h"
#include "paging.h"
#include "mem.h"
#include "inout.h"
#include "callback.h"
#include "int10.h"
#include "SDL.h"
#include "jega.h"//for AX
#include "render.h"
#include "support.h"
#include "control.h"
#include "jfont.h"
#include "dosv.h"
#include "dos_inc.h"

static Bitu dosv_font_handler[DOSV_FONT_MAX];
static Bit16u dosv_font_handler_offset[DOSV_FONT_MAX];
static Bitu dosv_timer;
static Bit8u dosv_cursor_stat;
static Bitu dosv_cursor_x;
static Bitu dosv_cursor_y;
static enum DOSV_VTEXT_MODE dosv_vtext_mode[VTEXT_MODE_COUNT];
static enum DOSV_FEP_CTRL dosv_fep_ctrl;
Bit8u TrueVideoMode;

Bit8u GetTrueVideoMode()
{
	return TrueVideoMode;
}

void SetTrueVideoMode(Bit8u mode)
{
	TrueVideoMode = mode;
}

bool DOSV_CheckJapaneseVideoMode()
{
	if(IS_DOS_JAPANESE && (TrueVideoMode == 0x03 || TrueVideoMode == 0x12 || (TrueVideoMode >= 0x70 && TrueVideoMode <= 0x73) || (TrueVideoMode >= 0x78 && TrueVideoMode < 0x78 + VTEXT_MODE_COUNT - 1))) {
		return true;
	}
	if(IS_J3_ARCH && TrueVideoMode == 0x75) {
		return true;
	}
	return false;
}

bool INT10_DOSV_SetCRTBIOSMode(Bitu mode)
{
	TrueVideoMode = mode;
	if (!IS_DOSV) return false;
	if (mode == 0x03 || mode == 0x70) {
		LOG(LOG_INT10, LOG_NORMAL)("DOS/V CRT BIOS has been set to JP mode.");
		INT10_SetVideoMode(0x12);
		INT10_SetDOSVModeVtext(mode, DOSV_VGA);
		return true;
	}
	return false;
}

static Bitu font8x16(void)
{
	PhysPt data	= PhysMake(SegValue(es), reg_si);
	Bit8u *font = &jfont_sbcs_16[reg_cl * 16];
	for(Bitu ct = 0 ; ct < 16 ; ct++) {
		mem_writeb(data++, *font++);
	}
	reg_al = 0x00;
	return CBRET_NONE;
}

static Bitu font8x19(void)
{
	PhysPt data	= PhysMake(SegValue(es), reg_si);
	Bit8u *font = &jfont_sbcs_19[reg_cl * 19];
	for(Bitu ct = 0 ; ct < 19 ; ct++) {
		mem_writeb(data++, *font++);
	}
	reg_al = 0x00;
	return CBRET_NONE;
}

static Bitu font16x16(void)
{
	PhysPt data	= PhysMake(SegValue(es), reg_si);
	Bit16u *font = (Bit16u *)GetDbcsFont(reg_cx);
	for(Bitu ct = 0 ; ct < 16 ; ct++) {
		mem_writew(data, *font++);
		data += 2;
	}
	reg_al = 0x00;
	return CBRET_NONE;
}

static Bitu font12x24(void)
{
	PhysPt data	= PhysMake(SegValue(es), reg_si);
	Bit16u *font = (Bit16u *)GetSbcs24Font(reg_cl);
	for(Bitu ct = 0 ; ct < 24 ; ct++) {
		mem_writew(data, *font++);
		data += 2;
	}
	reg_al = 0x00;
	return CBRET_NONE;
}

static Bitu font24x24(void)
{
	PhysPt data	= PhysMake(SegValue(es), reg_si);
	Bit8u *font = GetDbcs24Font(reg_cx);
	for(Bitu ct = 0 ; ct < 24 ; ct++) {
		mem_writeb(data++, *font++);
		mem_writeb(data++, *font++);
		mem_writeb(data++, *font++);
	}
	reg_al = 0x00;
	return CBRET_NONE;
}

static Bitu write_font16x16(void)
{
	if(SetGaijiData(reg_cx, PhysMake(SegValue(es), reg_si))) {
		reg_al = 0x00;
	} else {
		reg_al = 0x06;
	}
	return CBRET_NONE;
}

static Bitu write_font24x24(void)
{
	if(SetGaijiData24(reg_cx, PhysMake(SegValue(es), reg_si))) {
		reg_al = 0x00;
	} else {
		reg_al = 0x06;
	}
	return CBRET_NONE;
}

static Bitu mskanji_api(void)
{
	Bit16u param_seg, param_off;
	Bit16u func, mode;

	param_seg = real_readw(SegValue(ss), reg_sp + 6);
	param_off = real_readw(SegValue(ss), reg_sp + 4);
	func = real_readw(param_seg, param_off);
	mode = real_readw(param_seg, param_off + 2);

	reg_ax = 0xffff;
	if(func == 1) {
		Bit16u kk_seg, kk_off;
		kk_seg = real_readw(param_seg, param_off + 6);
		kk_off = real_readw(param_seg, param_off + 4);
		real_writew(kk_seg, kk_off, 1);
		real_writeb(kk_seg, kk_off + 2, 'I');
		real_writeb(kk_seg, kk_off + 3, 'M');
		real_writeb(kk_seg, kk_off + 4, 'E');
		real_writeb(kk_seg, kk_off + 5, 0);
		reg_ax = 0;
	} else if(func == 5) {
		if(mode & 0x8000) {
			if(mode & 0x0001) {
				SDL_SetIMValues(SDL_IM_ONOFF, 0, NULL);
			} else if(mode & 0x0002) {
				SDL_SetIMValues(SDL_IM_ONOFF, 1, NULL);
			}
		} else {
			int onoff;
			if(SDL_GetIMValues(SDL_IM_ONOFF, &onoff, NULL) == NULL) {
				if(onoff) {
					real_writew(param_seg, param_off + 2, 0x000a);
				} else {
					real_writew(param_seg, param_off + 2, 0x0009);
				}
			}
		}
		reg_ax = 0;
	}
	return CBRET_NONE;
}

static CallBack_Handler font_handler_list[] = {
	font8x16,
	font8x19,
	font16x16,
	font12x24,
	font24x24,
	write_font16x16,
	write_font24x24,

	mskanji_api,
};

Bit16u DOSV_GetFontHandlerOffset(enum DOSV_FONT font)
{
	if(font >= DOSV_FONT_8X16 && font < DOSV_FONT_MAX) {
		return dosv_font_handler_offset[font];
	}
	return 0;
}


static Bitu dosv_cursor_24[] = {
	0, 3, 8, 12, 15, 18, 21, 23
};

static Bitu dosv_cursor_19[] = {
	0, 3, 6, 9, 11, 13, 16, 18
};

static Bitu dosv_cursor_16[] = {
	0, 3, 5, 7, 9, 11, 13, 15
};

Bit8u StartBankSelect(Bitu &off);
Bit8u CheckBankSelect(Bit8u select, Bitu &off);

void DOSV_CursorXor24(Bitu x, Bitu y, Bitu start, Bitu end)
{
	IO_Write(0x3ce, 0x05); IO_Write(0x3cf, 0x03);
	IO_Write(0x3ce, 0x00); IO_Write(0x3cf, 0x0f);
	IO_Write(0x3ce, 0x03); IO_Write(0x3cf, 0x18);

	Bitu width = (real_readw(BIOSMEM_SEG, BIOSMEM_NB_COLS) == 85) ? 128 : 160;
	volatile Bit8u dummy;
	Bitu off = (y + start) * width + (x * 12) / 8;
	Bit8u select = StartBankSelect(off);
	while(start <= end) {
		if(dosv_cursor_stat == 2) {
			const Bit8u cursor_data[2][4] = {{ 0xff, 0xff, 0xff, 0x00 }, { 0x0f, 0xff, 0xff, 0xf0 }};
			for(Bit8u i = 0 ; i < 4 ; i++) {
				if(cursor_data[x & 1][i] != 0) {
					dummy = real_readb(0xa000, off);
					real_writeb(0xa000, off, cursor_data[x & 1][i]);
				}
				off++;
				select = CheckBankSelect(select, off);
			}
			off += width - 4;
		} else {
			const Bit8u cursor_data[2][2] = { { 0xff, 0xf0 }, { 0x0f, 0xff }};
			for(Bit8u i = 0 ; i < 2 ; i++) {
				dummy = real_readb(0xa000, off);
				real_writeb(0xa000, off, cursor_data[x & 1][i]);
				off++;
				select = CheckBankSelect(select, off);
			}
			off += width - 2;
		}
		select = CheckBankSelect(select, off);
		start++;
	}
	IO_Write(0x3ce, 0x03); IO_Write(0x3cf, 0x00);
	if(svgaCard == SVGA_TsengET4K) {
		IO_Write(0x3cd, 0);
	} else {
		IO_Write(0x3d4, 0x6a); IO_Write(0x3d5, 0);
	}
}

void DOSV_CursorXor(Bitu x, Bitu y)
{
	Bitu end = real_readb(BIOSMEM_SEG, BIOSMEM_CURSOR_TYPE);
	Bitu start = real_readb(BIOSMEM_SEG, BIOSMEM_CURSOR_TYPE + 1);

	if(start != 0x20 && start <= end) {
		Bitu width = real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS);
		Bit16u height = real_readw(BIOSMEM_SEG, BIOSMEM_CHAR_HEIGHT);
		if(height == 24) {
			if(start < 8) start = dosv_cursor_24[start];
			if(end < 8) end = dosv_cursor_24[end];
			DOSV_CursorXor24(x, y, start, end);
			return;
		} else if(height == 19) {
			if(end == 28) {
				end = 18;
				if(start >= 26) {
					start -= 11;
					end--;
				} else if(start >= 3) {
					start = 10;
				}
			} else {
				if(start < 8) start = dosv_cursor_19[start];
				if(end < 8) end = dosv_cursor_19[end];
			}
		} else if(height == 16) {
			if(end == 28) {
				end = 15;
				if(start >= 26) {
					start -= 14;
					end--;
				} else if(start >= 3) {
					start = 8;
				}
			} else {
				if(start < 8) start = dosv_cursor_16[start];
				if(end < 8) end = dosv_cursor_16[end];
			}
		}
		IO_Write(0x3ce, 0x05); IO_Write(0x3cf, 0x03);
		IO_Write(0x3ce, 0x00); IO_Write(0x3cf, 0x0f);
		IO_Write(0x3ce, 0x03); IO_Write(0x3cf, 0x18);

		volatile Bit8u dummy;
		Bitu off = (y + start) * width + x;
		Bit8u select = StartBankSelect(off);
		while(start <= end) {
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);
			if(dosv_cursor_stat == 2) {
				off++;
				select = CheckBankSelect(select, off);
				dummy = real_readb(0xa000, off);
				real_writeb(0xa000, off, 0xff);
				off += width - 1;
			} else {
				off += width;
			}
			select = CheckBankSelect(select, off);
			start++;
		}
		IO_Write(0x3ce, 0x03); IO_Write(0x3cf, 0x00);
		if(svgaCard == SVGA_TsengET4K) {
			IO_Write(0x3cd, 0);
		} else if(svgaCard == SVGA_S3Trio) {
			IO_Write(0x3d4, 0x6a); IO_Write(0x3d5, 0);
		}
	}
}

void DOSV_OffCursor()
{
	if(dosv_cursor_stat) {
		DOSV_CursorXor(dosv_cursor_x, dosv_cursor_y);
		dosv_cursor_stat = 0;
	}
}

extern void SetIMPosition();

void INT8_DOSV()
{
	SetIMPosition();

	if(!CheckAnotherDisplayDriver() && real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_MODE) != 0x72 && real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_MODE) != 0x12) {
		if(dosv_cursor_stat == 0) {
			Bitu x = real_readb(BIOSMEM_SEG, BIOSMEM_CURSOR_POS);
			Bitu y = real_readb(BIOSMEM_SEG, BIOSMEM_CURSOR_POS + 1) * real_readb(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT);
			Bit8u attr = GetKanjiAttr();
			if(attr == 0) {
				dosv_cursor_stat = 1;
			} else {
				dosv_cursor_stat = 1;
				if(attr == 1) {
					dosv_cursor_stat = 2;
				}
			}
			dosv_cursor_x = x;
			dosv_cursor_y = y;
			DOSV_CursorXor(x, y);
		}
	}
}

enum DOSV_VTEXT_MODE DOSV_GetVtextMode(Bitu no)
{
	if(no < VTEXT_MODE_COUNT) {
		return dosv_vtext_mode[no];
	}
	return dosv_vtext_mode[0];
}

enum DOSV_VTEXT_MODE DOSV_StringVtextMode(std::string vtext)
{
	if(vtext == "vga") {
		return DOSV_VTEXT_VGA;
	} else if(svgaCard == SVGA_TsengET4K || svgaCard == SVGA_S3Trio) {
		if(vtext == "xga") {
			return DOSV_VTEXT_XGA;
		} else if(vtext == "xga24") {
			return DOSV_VTEXT_XGA_24;
		} else if(vtext == "sxga") {
			return DOSV_VTEXT_SXGA;
		} else if(vtext == "sxga24") {
			return DOSV_VTEXT_SXGA_24;
		}
	}
	return DOSV_VTEXT_SVGA;
}

static enum DOSV_VTEXT_MODE rows_vtext_mode = DOSV_VGA;
static int rows_vtext_no;

void DOSV_SetVTextRows(Bitu no, int rows)
{
	if(no < VTEXT_MODE_COUNT) {
		rows_vtext_no = no;
		rows_vtext_mode = dosv_vtext_mode[no];
		INT10_SetDOSVVtextRows(no, rows_vtext_mode, rows);
	}
}

void DOSV_ResetVTextRows()
{
	if(rows_vtext_mode != DOSV_VGA) {
		INT10_SetDOSVVtextRows(rows_vtext_no, rows_vtext_mode, 0);
		rows_vtext_mode = DOSV_VGA;
	}
}

enum DOSV_FEP_CTRL DOSV_GetFepCtrl()
{
	return dosv_fep_ctrl;
}

void DOSV_SetConfig(Section_prop *section)
{
	const char *param = section->Get_string("fepcontrol");
	if(!strcmp(param, "ias")) {
		dosv_fep_ctrl = DOSV_FEP_CTRL_IAS;
	} else if(!strcmp(param, "mskanji")) {
		dosv_fep_ctrl = DOSV_FEP_CTRL_MSKANJI;
	} else {
		dosv_fep_ctrl = DOSV_FEP_CTRL_BOTH;
	}
	char name[16];
	for(int no = 0 ; no < VTEXT_MODE_COUNT ; no++) {
		sprintf(name, "vtext%d", no + 1);
		Prop_multival *p = section->Get_multival(name);
		dosv_vtext_mode[no] = DOSV_StringVtextMode(p->GetSection()->Get_string("type"));
		INT10_SetDOSVVtextRowsDefault(no, dosv_vtext_mode[no], p->GetSection()->Get_int("rows"));
	}
}

void DOSV_Setup()
{
	SetTextSeg();
	for(Bitu ct = 0 ; ct < DOSV_FONT_MAX ; ct++) {
		dosv_font_handler[ct] = CALLBACK_Allocate();
		CallBack_Handlers[dosv_font_handler[ct]] = font_handler_list[ct];
		dosv_font_handler_offset[ct] = CALLBACK_PhysPointer(dosv_font_handler[ct]) & 0xffff;
		phys_writeb(CALLBACK_PhysPointer(dosv_font_handler[ct]) + 0, 0xFE);
		phys_writeb(CALLBACK_PhysPointer(dosv_font_handler[ct]) + 1, 0x38);
		phys_writew(CALLBACK_PhysPointer(dosv_font_handler[ct]) + 2, (Bit16u)dosv_font_handler[ct]);
		if(ct == DOSV_MSKANJI_API) {
			phys_writeb(CALLBACK_PhysPointer(dosv_font_handler[ct]) + 4, 0xca);
			phys_writew(CALLBACK_PhysPointer(dosv_font_handler[ct]) + 5, 0x0004);
		} else {
			phys_writeb(CALLBACK_PhysPointer(dosv_font_handler[ct]) + 4, 0xcb);
		}
	}
}
