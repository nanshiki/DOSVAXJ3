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

static Bit16u dosv_text_seg;
static Bitu dosv_font_handler[DOSV_FONT_MAX];
static Bit16u dosv_font_handler_offset[DOSV_FONT_MAX];
static Bitu dosv_timer;
static Bit8u dosv_cursor_stat;
static Bitu dosv_cursor_x;
static Bitu dosv_cursor_y;
static bool dosv_svga_flag = false;

bool INT10_DOSV_SetCRTBIOSMode(Bitu mode)
{
	if (!IS_DOSV) return false;
	if (mode == 0x03 || mode == 0x70) {
		LOG(LOG_INT10, LOG_NORMAL)("DOS/V CRT BIOS has been set to JP mode.");
		INT10_SetVideoMode(0x12);
		INT10_SetDOSVMode(mode);
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

static CallBack_Handler font_handler_list[] = {
	font8x16,
	font8x19,
	font16x16,
	font12x24,
	font24x24,
};

Bit16u DOSV_GetFontHandlerOffset(enum DOSV_FONT font)
{
	if(font >= DOSV_FONT_8X16 && font < DOSV_FONT_MAX) {
		return dosv_font_handler_offset[font];
	}
	return 0;
}



static Bitu dosv_cursor_19[] = {
	0, 3, 6, 9, 11, 13, 16, 18
};

static Bitu dosv_cursor_16[] = {
	0, 3, 5, 7, 9, 11, 13, 15
};

void DOSV_CursorXor(Bitu x, Bitu y)
{
	Bitu end = real_readb(BIOSMEM_SEG, BIOSMEM_CURSOR_TYPE);
	Bitu start = real_readb(BIOSMEM_SEG, BIOSMEM_CURSOR_TYPE + 1);

	if(start != 0x20 && start <= end) {
		Bit16u height = real_readw(BIOSMEM_SEG, BIOSMEM_CHAR_HEIGHT);
		if(height == 19) {
			if(start < 8) start = dosv_cursor_19[start];
			if(end < 8) end = dosv_cursor_19[end];
		} else if(height == 16) {
			if(start < 8) start = dosv_cursor_16[start];
			if(end < 8) end = dosv_cursor_16[end];
		}
		IO_Write(0x3ce, 0x05); IO_Write(0x3cf, 0x03);
		IO_Write(0x3ce, 0x00); IO_Write(0x3cf, 0x0f);
		IO_Write(0x3ce, 0x03); IO_Write(0x3cf, 0x18);

		volatile Bit8u dummy;
		Bitu width = real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS);
		Bitu off = (y + start) * width + x;
		while(start <= end) {
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);
			if(dosv_cursor_stat == 2) {
				dummy = real_readb(0xa000, off + 1);
				real_writeb(0xa000, off + 1, 0xff);
			}
			off += width;
			start++;
		}
		IO_Write(0x3ce, 0x03); IO_Write(0x3cf, 0x00);
	}
}

void DOSV_OffCursor()
{
	if(dosv_cursor_stat) {
		DOSV_CursorXor(dosv_cursor_x, dosv_cursor_y);
		dosv_cursor_stat = 0;
	}
}

void INT8_DOSV()
{
	if(!CheckAnotherDisplayDriver() && real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_MODE) != 0x72) {
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

bool DOSV_CheckSVGA()
{
	return dosv_svga_flag;
}

void DOSV_SetConfig(Section_prop *section)
{
	std::string vtext = section->Get_string("vtext");
	if(vtext == "svga") {
		dosv_svga_flag = true;
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
		phys_writeb(CALLBACK_PhysPointer(dosv_font_handler[ct]) + 4, 0xcb);
	}
}
