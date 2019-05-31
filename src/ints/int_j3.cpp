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
#include "timer.h"
#include "j3.h"
#include "jfont.h"

#define CHANGE_IM_POSITION_TIME		100

#define KANJI_ROM_PAGE		(0xE0000/4096)

static Bitu j3_timer;
static Bit8u j3_cursor_stat;
static Bitu j3_cursor_x;
static Bitu j3_cursor_y;
static Bitu j3_text_color;
static Bitu j3_back_color;
static Bit16u j3_machine_code;
static Bit16u j3_font_seg;

static Bit8u jfont_yen[32];
static Bit8u jfont_kana[32*64];
static Bit8u jfont_kanji[96];

bool INT10_J3_SetCRTBIOSMode(Bitu mode)
{
	if (!IS_J3_ARCH) return false;
	if (mode == 0x74) {
		LOG(LOG_INT10, LOG_NORMAL)("J-3100 CRT BIOS has been set to JP mode.");
		INT10_SetVideoMode(0x74);
		return true;
	}
	return false;
}

static Bit16u jis2shift(Bit16u jis)
{
	Bit16u high, low;
	high = jis >> 8;
	low = jis & 0xff;
	if(high & 0x01) {
		low += 0x1f;
	} else {
		low += 0x7d;
	}
	if(low >= 0x7f) {
		low++;
	}
	high = ((high - 0x21) >> 1) + 0x81;
	if(high >= 0xa0) {
		high += 0x40;
	}
	return (high << 8) + low;
}

static Bit16u shift2jis(Bit16u sjis)
{
	Bit16u high, low;
	high = sjis >> 8;
	low = sjis & 0xff;
	if(high > 0x9f) {
		high -= 0xb1;
	} else {
		high -= 0x71;
	}
	high <<= 1;
	high++;
	if(low > 0x7f) {
		low--;
	}
	if(low >= 0x9e) {
		low -= 0x7d;
		high++;
	} else {
		low -= 0x1f;
	}
	return (high << 8) + low;
}

Bitu INT60_Handler(void)
{
	switch (reg_ah) {
	case 0x01:
		reg_dx = jis2shift(reg_dx);
		break;
	case 0x02:
		reg_dx = shift2jis(reg_dx);
		break;
	case 0x03:
		{
			Bit16u code = (reg_al & 0x01) ? jis2shift(reg_dx) : reg_dx;
			SegSet16(es, 0xe000);
			if(reg_al & 0x02) {
				Bit8u *src = GetDbcs24Font(code);
				Bit8u *dest = jfont_kanji;
				for(Bitu y = 0 ; y < 24 ; y++) {
					*dest++ = *src++;
					*dest++ = *src++;
					*dest++ = *src++;
					dest++;
				}
				reg_si = 0x8000;
			} else {
				// yen
				if(code == 0x80da) {
					reg_si = 0x0780;
				} else if(code >= 0x8540 && code <= 0x857E) {
					reg_si = 0x6c20 + (code - 0x8540) * 32;
				} else {
					Bit16u *src = (Bit16u *)GetDbcsFont(code);
					Bit16u *dest = (Bit16u *)jfont_kanji;
					for(Bitu y = 0 ; y < 16 ; y++) {
						*dest++ = *src++;
					}
					reg_si = 0x8000;
				}
			}
			reg_al = 0;
		}
		break;
	case 0x05:
		break;
	case 0x0c:
		if(reg_al == 0xff) {
			reg_al = 25 - real_readb(BIOSMEM_J3_SEG, BIOSMEM_J3_LINE_COUNT);
		} else {
			Bit8u line = 25 - reg_al;
			real_writeb(BIOSMEM_J3_SEG, BIOSMEM_J3_LINE_COUNT, line);
			line--;
			real_writeb(BIOSMEM_SEG, BIOSMEM_NB_ROWS, line);
		}
		break;
	case 0x0e:
		SegSet16(es, GetTextSeg());
		reg_bx = 0;
		break;
	case 0x0f:
		if(reg_al == 0x00) {
			reg_ax = 0;
		} else if(reg_al == 0x01) {
			DOS_ClearKeyMap();
		}
		break;
	case 0x10:
		if(reg_al == 0x00) {
			SegSet16(es, j3_font_seg);
			reg_bx = 0;
		}
		break;
	default:
		LOG(LOG_BIOS,LOG_ERROR)("INT60:Unknown call %4X",reg_ax);
	}
	return CBRET_NONE;
}

Bitu INT6F_Handler(void)
{
	switch(reg_ah) {
	case 0x01:
	case 0x02:
	case 0x03:
	case 0x04:
	case 0x05:
		SDL_SetIMValues(SDL_IM_ONOFF, 1, NULL);
		break;
	case 0x0b:
		SDL_SetIMValues(SDL_IM_ONOFF, 0, NULL);
		break;
	case 0x66:
		{
			int onoff;
			reg_al = 0x00;
			if(SDL_GetIMValues(SDL_IM_ONOFF, &onoff, NULL) == NULL) {
				if(onoff) {
					reg_al = 0x01;
				}
			}
		}
		break;
	}
	return CBRET_NONE;
}

class KanjiRomPageHandler : public PageHandler {
public:
	KanjiRomPageHandler() {
		flags=PFLAG_HASROM;
	}
	Bitu readb(PhysPt addr) {
		if(addr >= 0xe0780 && addr < 0xe07a0) {
			return jfont_yen[addr - 0xe0780];
		} else if(addr >= 0xe6c20 && addr < 0xe7400) {
			return jfont_kana[addr - 0xe6c20];
		} else if(addr >= 0xe8000 && addr < 0xe8060) {
			return jfont_kanji[addr - 0xe8000];
		}
		return 0;
	}
	Bitu readw(PhysPt addr) {
		if(addr >= 0xe0780 && addr < 0xe07a0) {
			return *(Bit16u *)&jfont_yen[addr - 0xe0780];
		} else if(addr >= 0xe6c20 && addr < 0xe7400) {
			return *(Bit16u *)&jfont_kana[addr - 0xe6c20];
		} else if(addr >= 0xe8000 && addr < 0xe8060) {
			return *(Bit16u *)&jfont_kanji[addr - 0xe8000];
		}
		return 0;
	}
	Bitu readd(PhysPt addr) {
		return 0;
	}
	void writeb(PhysPt addr,Bitu val){
	}
	void writew(PhysPt addr,Bitu val){
	}
	void writed(PhysPt addr,Bitu val){
	}
};
KanjiRomPageHandler kanji_rom_handler;

void INT60_J3_Setup()
{
	Bitu code;

	SetTextSeg();
	j3_font_seg = DOS_GetMemory(0x100);

	PhysPt fontdata = Real2Phys(int10.rom.font_16);
	for(code = 0 ; code < 256 ; code++) {
		for(int y = 0 ; y < 16 ; y++) {
			if(code >= 0x20 && code < 0x80) {
				real_writeb(j3_font_seg, code * 16 + y, jfont_sbcs_16[code * 16 + y]);
				if(code == 0x5c) {
					jfont_yen[y * 2] = jfont_sbcs_16[code * 16 + y];
					jfont_yen[y * 2 + 1] = jfont_sbcs_16[code * 16 + y];
				}
			} else {
				real_writeb(j3_font_seg, code * 16 + y, mem_readb(fontdata + code * 16 + y));
				if(code >= 0xa1 && code <= 0xdf) {
					jfont_kana[(code - 0xa1) * 32 + y * 2] = jfont_sbcs_16[code * 16 + y];
					jfont_kana[(code - 0xa1) * 32 + y * 2 + 1] = jfont_sbcs_16[code * 16 + y];
				}
			}
		}
	}
	MEM_SetPageHandler(KANJI_ROM_PAGE, 16, &kanji_rom_handler);
}

static void J3_CursorXor(Bitu x, Bitu y)
{
	Bitu end = real_readb(BIOSMEM_SEG, BIOSMEM_CURSOR_TYPE);
	Bitu start = real_readb(BIOSMEM_SEG, BIOSMEM_CURSOR_TYPE + 1);
	Bitu off;

	if(start != 0x20 && start <= end) {
		y += start;
		off = (y >> 2) * 80 + 8 * 1024 * (y & 3) + x;
		while(start <= end) {
			real_writeb(GRAPH_J3_SEG, off, real_readb(GRAPH_J3_SEG, off) ^ 0xff);
			if(j3_cursor_stat == 2) {
				real_writeb(GRAPH_J3_SEG, off + 1, real_readb(GRAPH_J3_SEG, off + 1) ^ 0xff);
			}
			off += 0x2000;
			if(off >= 0x8000) {
				off -= 0x8000;
				off += 80;
			}
			start++;
		}
	}
}

void J3_OffCursor()
{
	if(j3_cursor_stat) {
		J3_CursorXor(j3_cursor_x, j3_cursor_y);
		j3_cursor_stat = 0;
	}
}

static Bitu im_x, im_y;
static Bit32u last_ticks;

void SetIMPosition()
{
	Bitu x = real_readb(BIOSMEM_SEG, BIOSMEM_CURSOR_POS);
	Bitu y = real_readb(BIOSMEM_SEG, BIOSMEM_CURSOR_POS + 1);

	if((im_x != x || im_y != y) && GetTicks() - last_ticks > CHANGE_IM_POSITION_TIME) {
		last_ticks = GetTicks();
		im_x = x;
		im_y = y;
#if defined(LINUX)
		y++;
#endif
		Bit8u height = real_readb(BIOSMEM_SEG, BIOSMEM_CHAR_HEIGHT);
		if(height == 24) {
			SDL_SetIMPosition(x * 12, y * 24);
		} else {
			SDL_SetIMPosition(x * 8, y * height + ((height == 16) ? 0 : 1));
		}
	}
}

void INT8_J3()
{
	SetIMPosition();

	j3_timer++;
	if((j3_timer & 0x03) == 0) {
		if((real_readb(BIOSMEM_J3_SEG, BIOSMEM_J3_BLINK) & 0x01) == 0 || j3_cursor_stat == 0) {
			Bitu x = real_readb(BIOSMEM_SEG, BIOSMEM_CURSOR_POS);
			Bitu y = real_readb(BIOSMEM_SEG, BIOSMEM_CURSOR_POS + 1) * 16;
			if(j3_cursor_stat == 0) {
				Bit8u attr = GetKanjiAttr();
				if(attr == 0) {
					j3_cursor_stat = 1;
				} else {
					j3_cursor_stat = 1;
					if(attr == 1) {
						j3_cursor_stat = 2;
					}
				}
				j3_cursor_x = x;
				j3_cursor_y = y;
				J3_CursorXor(x, y);
			} else {
				J3_CursorXor(x, y);
				j3_cursor_stat = 0;
			}
		}
	}
}

enum J3_COLOR {
	colorLcdBlue,
	colorLcdWhite,
	colorPlasma,
	colorNormal,
	colorMax
};

static Bitu text_color_list[colorMax] = {
	0x3963f7, 0x3a4b51, 0xff321b, 0xffffff
};

static Bitu back_color_list[colorMax] = {
	0xbed7d4, 0xeaf3f2, 0x6a1d22, 0x000000
};


static struct J3_MACHINE_LIST {
	char *name;
	Bit16u code;
	enum J3_COLOR color;
} j3_machine_list[] = {
	{ "gt", 0x3130, colorPlasma },
	{ "sgt", 0xfc27, colorPlasma },
	{ "gx", 0xfc2d, colorPlasma },
	{ "gl", 0xfc2b, colorLcdBlue },
	{ "sl", 0xfc2c, colorLcdBlue },
	{ "sgx", 0xfc26, colorPlasma },
	{ "ss", 0x3131, colorLcdBlue },
	{ "gs", 0xfc2a, colorLcdBlue },
	{ "sx", 0xfc36, colorLcdBlue },
	{ "sxb", 0xfc36, colorLcdBlue },
	{ "sxw", 0xfc36, colorLcdWhite },
	{ "sxp", 0xfc36, colorPlasma },
	{ "ez", 0xfc87, colorLcdWhite },
	{ "zs", 0xfc25, colorNormal },
	{ "zx", 0xfc4e, colorNormal },
	{ NULL, 0, colorMax }
};

Bit16u J3_GetMachineCode()
{
	return j3_machine_code;
}

void J3_SetConfig(Section_prop *section)
{
	Bitu back_color = section->Get_hex("j3backcolor");
	Bitu text_color = section->Get_hex("j3textcolor");
	std::string j3100 = section->Get_string("j3100");
	j3_machine_code = ConvHexWord((char *)j3100.c_str());
	if(j3_machine_code == 0) {
		j3_machine_code = 0x6a74;
	}
	enum J3_COLOR j3_color = colorNormal;
	for(Bitu count = 0 ; j3_machine_list[count].name != NULL ; count++) {
		if(j3100 == j3_machine_list[count].name) {
			j3_machine_code = j3_machine_list[count].code;
			j3_color = j3_machine_list[count].color;
			break;
		}
	}
	j3_back_color = back_color_list[j3_color];
	j3_text_color = text_color_list[j3_color];

	if(back_color < 0x1000000) {
		j3_back_color = back_color;
	}
	if(text_color < 0x1000000) {
		j3_text_color = text_color;
	}
}

void J3_GetPalette(Bit8u no, Bit8u &r, Bit8u &g, Bit8u &b)
{
	if(no == 0) {
		r = (j3_back_color >> 16) & 0xff;
		g = (j3_back_color >> 8) & 0xff;
		b = j3_back_color & 0xff;
	} else {
		r = (j3_text_color >> 16) & 0xff;
		g = (j3_text_color >> 8) & 0xff;
		b = j3_text_color & 0xff;
	}
}

bool J3_IsJapanese()
{
	if(real_readb(BIOSMEM_SEG, BIOSMEM_CURRENT_MODE) == 0x74) {
		return true;
	}
	return false;
}


