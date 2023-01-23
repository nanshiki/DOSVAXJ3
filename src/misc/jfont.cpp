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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dosbox.h"
#include "control.h"
#include "jega.h"
#include "jfont.h"
#include "jfontdata.h"
#include "../ints/int10.h"
#include "SDL_events.h"
#if defined(LINUX)
#include <X11/Xlib.h>
#include <X11/Xlocale.h>
#include <X11/Xutil.h>
#endif

#define	GAIJI_MAX		100

extern Bit8u jfont_sbcs_19[];
extern Bit8u jfont_dbcs_16[];
extern Bit8u jfont_sbcs_16[];
extern Bit8u jfont_dbcs_24[];
extern Bit8u jfont_sbcs_24[];
extern Bit8u jfont_cache_dbcs_16[];
extern Bit8u jfont_cache_dbcs_24[];

static Bit16u jtext_seg;
#if defined(WIN32)
static HFONT jfont_16;
static HFONT jfont_24;
static bool usefont20_flag;
#endif
static std::string jfont_name;
static Bit8u jfont_dbcs[96];
static Bitu gaiji_start;
static Bitu gaiji_end;
static Bit16u gaiji_seg;

#if defined(LINUX)
static Display *font_display;
static Window font_window;
static Pixmap font_pixmap;
static GC font_gc;
static XFontSet font_set16;
static XFontSet font_set24;
#endif

void QuitFont()
{
#if defined(LINUX)
	if(font_display) {
		if(font_gc) {
			XFreeGC(font_display, font_gc);
		}
		if(font_pixmap) {
			XFreePixmap(font_display, font_pixmap);
		}
		if(font_set16) {
			XFreeFontSet(font_display, font_set16);
		}
		if(font_set24) {
			XFreeFontSet(font_display, font_set24);
		}
		if(font_window) {
			XDestroyWindow(font_display, font_window);
		}
		XCloseDisplay(font_display);
	}
#endif
#if defined(WIN32)
	if(jfont_16) {
		DeleteObject(jfont_16);
	}
	if(jfont_24) {
		DeleteObject(jfont_24);
	}
#endif
}

void InitFontHandle()
{
#if defined(LINUX)
	int missing_count;
	char **missing_list;
	char *def_string;

	if(!font_display) {
		font_display = XOpenDisplay("");
	}
	if(font_display) {
		if(!font_set16) {
			font_set16 = XCreateFontSet(font_display, "-*-fixed-medium-r-normal--16-*-*-*", &missing_list, &missing_count, &def_string);
			XFreeStringList(missing_list);
		}
		if(!font_set24) {
			font_set24 = XCreateFontSet(font_display, "-*-fixed-medium-r-normal--24-*-*-*", &missing_list, &missing_count, &def_string);
			XFreeStringList(missing_list);
		}
		if(!font_window) {
			font_window = XCreateSimpleWindow(font_display, DefaultRootWindow(font_display), 0, 0, 32, 32, 0, BlackPixel(font_display, DefaultScreen(font_display)), WhitePixel(font_display, DefaultScreen(font_display)));
			font_pixmap = XCreatePixmap(font_display, font_window, 32, 32, DefaultDepth(font_display, 0));
			font_gc = XCreateGC(font_display, font_pixmap, 0, 0);
		}
	}
#endif
#if defined(WIN32)	
	if(jfont_name.empty()) {
		// MS Gothic
		SetFontName("\x082\x06c\x082\x072\x020\x083\x053\x083\x056\x083\x062\x083\x04e");
	}
	if(jfont_16 == NULL || jfont_24 == NULL) {
		LOGFONT lf = { 0 };
		lf.lfHeight = 16;
		lf.lfCharSet = SHIFTJIS_CHARSET;
		lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
		lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
		lf.lfQuality = DEFAULT_QUALITY;
		lf.lfPitchAndFamily = FIXED_PITCH;
		strcpy(lf.lfFaceName, jfont_name.c_str());
		jfont_16 = CreateFontIndirect(&lf);
		lf.lfHeight = usefont20_flag ? 20 : 24;
		jfont_24 = CreateFontIndirect(&lf);
	}
#endif
}

#if defined(LINUX)
static Bit8u linux_symbol_16[] = {
// 0x815f
  0x80, 0x00, 0x40, 0x00, 0x20, 0x00, 0x10, 0x00, 0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x40, 0x00, 0x20, 0x00, 0x10, 0x00, 0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x01,
// 0x8191
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x03, 0xe0, 0x04, 0x90,
  0x08, 0x80, 0x08, 0x80, 0x08, 0x80, 0x08, 0x80, 0x04, 0x90, 0x03, 0xe0, 0x00, 0x80, 0x00, 0x00,
// 0x8192
  0x00, 0x00, 0x01, 0xe0, 0x02, 0x10, 0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x04, 0x00,
  0x3f, 0xe0, 0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x08, 0x00, 0x1f, 0xf8, 0x00, 0x00,
// 0x81ca
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xfc, 0x00, 0x04,
  0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static Bit8u linux_symbol_24[] = {
// 0x815f
  0x80, 0x00, 0x00, 0x40, 0x00, 0x00, 0x20, 0x00, 0x00, 0x10, 0x00, 0x00, 0x08, 0x00, 0x00, 0x04, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x40, 0x00, 0x00, 0x20, 0x00, 0x00, 0x10, 0x00, 0x00, 0x08, 0x00, 0x00, 0x04, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x00, 0x80, 0x00, 0x00, 0x40, 0x00, 0x00, 0x20, 0x00, 0x00, 0x10, 0x00, 0x00, 0x08, 0x00, 0x00, 0x04, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01,
// 0x8191
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00,
  0x00, 0x08, 0x00, 0x00, 0x7f, 0x00, 0x00, 0xc8, 0x80, 0x01, 0x88, 0xc0, 0x01, 0x88, 0x40, 0x03, 0x08, 0x00, 0x03, 0x08, 0x00, 0x03, 0x08, 0x00,
  0x03, 0x08, 0x00, 0x01, 0x88, 0x40, 0x01, 0x88, 0xc0, 0x00, 0xc9, 0x80, 0x00, 0x7f, 0x00, 0x00, 0x08, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
// 0x8192
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0x00, 0x00, 0x40, 0x80, 0x00, 0x80, 0x40, 0x01, 0x80, 0x00, 0x01, 0x80, 0x00, 0x01, 0x80, 0x00,
  0x01, 0x80, 0x00, 0x01, 0x80, 0x00, 0x01, 0x80, 0x00, 0x01, 0x80, 0x00, 0x01, 0x80, 0x00, 0x1f, 0xff, 0x00, 0x01, 0x80, 0x00, 0x01, 0x80, 0x00,
  0x01, 0x80, 0x00, 0x01, 0x80, 0x00, 0x03, 0x80, 0x00, 0x03, 0x00, 0x00, 0x07, 0x00, 0x00, 0x0f, 0xff, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
// 0x81ca
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0xf8, 0x00, 0x00, 0x08, 0x00, 0x00, 0x08, 0x00, 0x00, 0x08, 0x00, 0x00, 0x08, 0x00, 0x00, 0x08,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};


bool CheckLinuxSymbol(Bitu code, Bit8u *buff, int width, int height)
{
	Bit8u *src;
	int len, offset;
	if(width == 16 && height == 16) {
		src = linux_symbol_16;
		len = 32;
	} else if(width == 24 && height == 24) {
		src = linux_symbol_24;
		len = 72;
	} else {
		return false;
	}
	offset = -1;
	if(code == 0x815f) {
		offset = 0;
	} else if(code == 0x8191) {
		offset = len;
	} else if(code == 0x8192) {
		offset = len * 2;
	} else if(code == 0x81ca) {
		offset = len * 3;
	}
	if(offset != -1) {
		memcpy(buff, src + offset, len);
		return true;
	}
	return false;
}
#endif

#if defined(MACOSX) || defined(LINUX)
bool GetFontX16x16Data(Bitu code, Bit8u *buff)
{
	int size = JPNZN16X[17];
	int start, end;
	int table_pos, data_pos;
	table_pos = 18;
	data_pos = table_pos + size * 4;
	for(int no = 0 ; no < size ; no++) {
		start = JPNZN16X[table_pos] | (JPNZN16X[table_pos + 1] << 8);
		end = JPNZN16X[table_pos + 2] | (JPNZN16X[table_pos + 3] << 8);
		table_pos += 4;
		for(unsigned short c = start ; c <= end ; c++) {
			if(c == code) {
				memcpy(buff, &JPNZN16X[data_pos], 32);
				return true;
			}
			data_pos += 32;
		}
	}
	return false;
}

bool GetFontX8x16Data(Bitu code, Bit8u *buff)
{
	if(code == 0x5c) {
		memcpy(buff, JPNHN16YEN, 16);
	} else {
		memcpy(buff, &JPNHN16X[17 + code * 16], 16);
	}
	return true;
}
#endif

bool GetWindowsFont(Bitu code, Bit8u *buff, int width, int height)
{
#if defined(LINUX)
	XRectangle ir, lr;
	wchar_t text[4];

	if(font_set16 == NULL) {
		if(height == 16) {
			if(width == 16) {
				return GetFontX16x16Data(code, buff);
			} else if(width == 8) {
				return GetFontX8x16Data(code, buff);
			}
			return false;
		}
	}

	if(code < 0x100) {
		if(code == 0x5c) {
			// yen
			text[0] = 0xa5;
		} else if(code >= 0xa1 && code <= 0xdf) {
			// half kana
			text[0] = 0xff61 + (code - 0xa1);
		} else {
			text[0] = code;
		}
	} else if(code == 0x8160) {
		text[0] = 0x301c;
	} else if(code == 0x8161) {
		text[0] = 0x2016;
	} else if(code == 0x817c) {
		text[0] = 0x2212;
	} else if(CheckLinuxSymbol(code, buff, width, height)) {
		return true;
	} else {
		char src[4];
		src[0] = code >> 8;
		src[1] = code & 0xff;
		src[2] = 0;
		sjis_to_utf16_copy((char *)text, src, 2);
		text[0] &= 0xffff;
	}
	text[1] = ']';
	text[2] = 0;

	memset(buff, 0, (width / 8) * height);

	if(height == 24) {
		if(font_set24 == NULL) {
			return false;
		}
		XwcTextExtents(font_set24, text, 2, &ir, &lr);
	} else {
		XwcTextExtents(font_set16, text, 2, &ir, &lr);
	}
	XSetForeground(font_display, font_gc, BlackPixel(font_display, 0));
	XFillRectangle(font_display, font_pixmap, font_gc, 0, 0, 32, 32);
	XSetForeground(font_display, font_gc, WhitePixel(font_display, 0));
	if(height == 24) {
		XwcDrawString(font_display, font_pixmap, font_set24, font_gc, 0, lr.height - (ir.height + ir.y), text, 2);
	} else {
		XwcDrawString(font_display, font_pixmap, font_set16, font_gc, 0, lr.height - (ir.height + ir.y), text, 2);
	}
	XImage *image = XGetImage(font_display, font_pixmap, 0, 0, width, lr.height, ~0, XYPixmap);
	if(image != NULL) {
		int x, y;
		for(y = 0 ; y < height ; y++) {
			Bit8u data = 0;
			Bit8u mask = 0x01;
			Bit8u font_mask = 0x80;
			Bit8u *pt = (unsigned char *)image->data + y * image->bytes_per_line;
			for(x = 0 ; x < width ; x++) {
				if(*pt & mask) {
					data |= font_mask;
				}
				mask <<= 1;
				font_mask >>= 1;
				if(font_mask == 0) {
					pt++;
					*buff++ = data;
					data = 0;
					mask = 0x01;
					font_mask = 0x80;
				}
			}
			if(width == 12) {
				pt++;
				*buff++ = data;
				data = 0;
				mask = 0x01;
				font_mask = 0x80;
			}
		}
		XDestroyImage(image);
		return true;
	}
#endif
#if defined(WIN32)
	HFONT font = (height == 16) ? jfont_16 : jfont_24;
	if(font != NULL) {
		HDC hdc = GetDC(NULL);
		HFONT old_font = (HFONT)SelectObject(hdc, font);

		TEXTMETRIC tm;
		GetTextMetrics(hdc, &tm);
		GLYPHMETRICS gm;
		CONST MAT2 mat = { {0,1},{0,0},{0,0},{0,1} };
		DWORD size = GetGlyphOutline(hdc, code, GGO_BITMAP, &gm, 0, NULL, &mat);
		if(size > 0) {
			char *fontbuff = new char[size];
			memset(fontbuff, 0, size);
			GetGlyphOutline(hdc, code, GGO_BITMAP, &gm, size, fontbuff, &mat);

			Bitu off_y = tm.tmAscent - gm.gmptGlyphOrigin.y;
			Bitu pos = off_y;
			Bitu count = (1 + (gm.gmBlackBoxX / 32)) * 4;
			if(width >= 16 || (width == 12 && height == 24)) {
				pos += off_y;
				if(width == 24) {
					pos += off_y;
				}
				if(usefont20_flag) {
					if(height == 24) {
						pos += 4;
						if(width == 24) {
							pos += 2;
						}
					}
				}
			}
			for(Bitu y = off_y ; y < off_y + gm.gmBlackBoxY; y++) {
				Bit32u data = 0;
				Bit32u bit = 0x800000 >> ((width - gm.gmBlackBoxX) / 2);
				for (Bitu x = gm.gmptGlyphOrigin.x; x < gm.gmptGlyphOrigin.x + gm.gmBlackBoxX; x++) {
					Bit8u src = *((Bit8u *)fontbuff + count * (y - off_y) + ((x - gm.gmptGlyphOrigin.x) / 8));
					if(src & (1 << (7 - ((x - gm.gmptGlyphOrigin.x) % 8)))) {
						data |= bit;
					}
					bit >>= 1;
				}
				buff[pos++] = (data >> 16) & 0xff;
				if(width >= 16 || (width == 12 && height == 24)) {
					buff[pos++] = (data >> 8) & 0xff;
					if(width == 24) {
						buff[pos++] = data & 0xff;
					}
				}
			}
			delete [] fontbuff;
		}
		SelectObject(hdc, old_font);
		ReleaseDC(NULL, hdc);

		return true;
	}
#endif
#if defined(MACOSX)
	if(height == 16) {
		if(width == 16) {
			return GetFontX16x16Data(code, buff);
		} else if(width == 8) {
			return GetFontX8x16Data(code, buff);
		}
	}
#endif
	return false;
}


Bit16u GetTextSeg()
{
	return jtext_seg;
}

void SetTextSeg()
{
	if(jtext_seg == 0) {
		jtext_seg = DOS_GetMemory(VIRTUAL_TEXT_SIZE);
	}
}

bool MakeSbcs19Font()
{
	InitFontHandle();
	for(Bitu code = 0 ; code < 256 ; code++) {
		if(!GetWindowsFont(code, &jfont_sbcs_19[code * 19 + 1], 8, 16)) {
			return false;
		}
	}
	if(IS_J3_ARCH || IS_DOSV) {
		memcpy(jfont_sbcs_19, dosv_font19_data, sizeof(dosv_font19_data));
	} else if(IS_AX_ARCH) {
		memcpy(jfont_sbcs_19, ax_font19_data, sizeof(ax_font19_data));
	}
	return true;
}

bool MakeSbcs16Font()
{
	InitFontHandle();
	for(Bitu code = 0 ; code < 256 ; code++) {
		if(!GetWindowsFont(code, &jfont_sbcs_16[code * 16], 8, 16)) {
			return false;
		}
	}
	if(IS_J3_ARCH || IS_DOSV) {
		memcpy(jfont_sbcs_16, dosv_font16_data, sizeof(dosv_font16_data));
	}
	return true;
}

bool MakeSbcs24Font()
{
	InitFontHandle();
	for(Bitu code = 0 ; code < 256 ; code++) {
		if(!GetWindowsFont(code, &jfont_sbcs_24[code * 24 * 2], 12, 24)) {
			return false;
		}
	}
	if(IS_J3_ARCH || IS_DOSV) {
		memcpy(jfont_sbcs_24, dosv_font24_data, sizeof(dosv_font24_data));
	}
	return true;
}

Bit8u GetKanjiAttr(Bitu x, Bitu y)
{
	Bitu cx, pos;
	Bit8u flag;
	Bit16u seg = (IS_AX_ARCH) ? 0xb800 : jtext_seg;
	pos = y * real_readw(BIOSMEM_SEG, BIOSMEM_NB_COLS) * 2;
	cx = 0;
	flag = 0x00;
	do {
		if(flag == 0x01) {
			flag = 0x02;
		} else {
			flag = 0x00;
			if(isKanji1(real_readb(seg, pos))) {
				flag = 0x01;
			}
		}
		pos += 2;
		cx++;
	} while(cx <= x);
	return flag;
}

Bit8u GetKanjiAttr()
{
	return GetKanjiAttr(real_readb(BIOSMEM_SEG, BIOSMEM_CURSOR_POS), real_readb(BIOSMEM_SEG, BIOSMEM_CURSOR_POS + 1));
}

Bit8u *GetSbcsFont(Bitu code)
{
	return &jfont_sbcs_16[code * 16];
}

Bit8u *GetSbcs19Font(Bitu code)
{
	return &jfont_sbcs_19[code * 19];
}

Bit8u *GetSbcs24Font(Bitu code)
{
	return &jfont_sbcs_24[code * 2 * 24];
}

#if defined(WIN32)
void SetFontUse20(bool flag)
{
	usefont20_flag = flag;
}
#endif

void SetFontName(const char *name)
{
	jfont_name = name;
#if defined(WIN32)
	SDL_SetCompositionFontName(jfont_name.c_str());
#endif
}

void GetDbcsFrameFont(Bitu code, Bit8u *buff)
{
	if(code >= 0x849f && code <= 0x84be) {
		memcpy(buff, &frame_font_data[(code - 0x849f) * 32], 32);
	}
}

void GetDbcs24FrameFont(Bitu code, Bit8u *buff)
{
	if(code >= 0x849f && code <= 0x84be) {
		memcpy(buff, &frame_font24_data[(code - 0x849f) * 72], 72);
	}
}

Bit8u *GetDbcsFont(Bitu code)
{
	memset(jfont_dbcs, 0, sizeof(jfont_dbcs));
	if(code >= gaiji_start && code <= gaiji_end) {
		Bitu count;
		code = (code - gaiji_start) * 32;
		for(count = 0 ; count < 32 ; count++) {
			jfont_dbcs[count] = real_readb(gaiji_seg, code + count);
		}
		return jfont_dbcs;
	}
	if(jfont_cache_dbcs_16[code] == 0) {
		if(code >= 0x849f && code <= 0x84be) {
			GetDbcsFrameFont(code, jfont_dbcs);
			memcpy(&jfont_dbcs_16[code * 32], jfont_dbcs, 32);
			jfont_cache_dbcs_16[code] = 1;
		} else if(GetWindowsFont(code, jfont_dbcs, 16, 16)) {
			memcpy(&jfont_dbcs_16[code * 32], jfont_dbcs, 32);
			jfont_cache_dbcs_16[code] = 1;
		}
		return jfont_dbcs;
	}
	return &jfont_dbcs_16[code * 32];
}

Bit8u *GetDbcs24Font(Bitu code)
{
	memset(jfont_dbcs, 0, sizeof(jfont_dbcs));
	if(jfont_cache_dbcs_24[code] == 0) {
		if(code >= 0x809e && code < 0x80fe) {
			if(GetWindowsFont(code - 0x807e, jfont_dbcs, 12, 24)) {
				Bitu no, pos;
				pos = code * 72;
				for(no = 0 ; no < 24 ; no++) {
					jfont_dbcs_24[pos + no * 3] = jfont_dbcs[no * 2];
					jfont_dbcs_24[pos + no * 3 + 1] = jfont_dbcs[no * 2 + 1] | (jfont_dbcs[no * 2] >> 4);
					jfont_dbcs_24[pos + no * 3 + 2] = (jfont_dbcs[no * 2] << 4) | (jfont_dbcs[no * 2 + 1] >> 4);
				}
				jfont_cache_dbcs_24[code] = 1;
				return &jfont_dbcs_24[pos];
			}
		} else if(code >= 0x8540 && code <= 0x857e) {
			if(GetWindowsFont(code - 0x8540 + 0xa1, jfont_dbcs, 12, 24)) {
				Bitu no, pos;
				pos = code * 72;
				for(no = 0 ; no < 24 ; no++) {
					jfont_dbcs_24[pos + no * 3] = jfont_dbcs[no * 2];
					jfont_dbcs_24[pos + no * 3 + 1] = jfont_dbcs[no * 2 + 1] | (jfont_dbcs[no * 2] >> 4);
					jfont_dbcs_24[pos + no * 3 + 2] = (jfont_dbcs[no * 2] << 4) | (jfont_dbcs[no * 2 + 1] >> 4);
				}
				jfont_cache_dbcs_24[code] = 1;
				return &jfont_dbcs_24[pos];
			}
		} else if(code >= 0x849f && code <= 0x84be) {
			GetDbcs24FrameFont(code, jfont_dbcs);
			memcpy(&jfont_dbcs_24[code * 72], jfont_dbcs, 72);
			jfont_cache_dbcs_24[code] = 1;
		} else {
			if(GetWindowsFont(code, jfont_dbcs, 24, 24)) {
				memcpy(&jfont_dbcs_24[code * 72], jfont_dbcs, 72);
				jfont_cache_dbcs_24[code] = 1;
			}
		}
		return jfont_dbcs;
	}
	return &jfont_dbcs_24[code * 72];
}

bool CheckStayVz()
{
	if(mem_readw(0x0086) != 0xf000) {
		Bitu addr = mem_readw(0x0086) << 4;
		char text[4];
		MEM_BlockRead(addr - 8, text, 2);
		text[2] = '\0';
		if(!strcasecmp(text, "VZ")) {
			return true;
		}
	}
	return false;
}

bool CheckAnotherDisplayDriver()
{
	if(mem_readw(0x0042) != 0xf000) {
		Bitu addr = mem_readw(0x0042) << 4;
		char text[10];

		MEM_BlockRead(addr + 10, text, 8);
		text[8] = '\0';
		if(!strcmp(text, "$IBMADSP")) {
			return true;
		}
		MEM_BlockRead(addr - 8, text, 4);
		text[4] = '\0';
		if(!strcmp(text, "DSP4")) {
			if(mem_readb(0x0449) == 0x70) {
				return true;
			}
		}
	}
	return false;
}

bool SetGaijiData(Bit16u code, PhysPt data)
{
	if(code >= gaiji_start && code <= gaiji_end) {
		Bit16u offset = (code - gaiji_start) * 32;
		for(Bitu ct = 0 ; ct < 16 ; ct++) {
			real_writew(gaiji_seg, offset, mem_readw(data));
			offset += 2;
			data += 2;
		}
		return true;
	}
	return false;
}

bool SetGaijiData24(Bit16u code, PhysPt data)
{
	if(code >= gaiji_start && code <= gaiji_end) {
		Bitu offset = code * 72;
		for(Bitu ct = 0 ; ct < 72 ; ct++) {
			jfont_dbcs_24[offset++] = mem_readb(data++);
		}
		jfont_cache_dbcs_24[code] = 1;
		return true;
	}
	return false;
}

Bit16u GetGaijiSeg()
{
	return gaiji_seg;
}

void SetGaijiConfig(Section_prop *section)
{
	Bitu count;

	gaiji_start = section->Get_hex("gaijistart");
	gaiji_end = section->Get_hex("gaijiend");

	count = gaiji_end - gaiji_start + 1;
	if(count >= GAIJI_MAX) {
		count = GAIJI_MAX;
		gaiji_end = gaiji_start + GAIJI_MAX - 1;
	}
	gaiji_seg = DOS_GetMemory(count * 2);
}

#ifndef NDEBUG
#include <stdarg.h>
void JTrace(const char *form , ...)
{
	va_list	ap;
	static char work[1000];

	va_start(ap, form);
	vsprintf(work, form, ap);
	va_end(ap);
#if defined(WIN32)
	OutputDebugString(work);
#endif
}
#endif

#if defined(WIN32)
extern "C" FILE * __cdecl __iob_func(void)
{
	return NULL;
}
#endif
