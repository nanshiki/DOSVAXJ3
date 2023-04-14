
#ifndef DOSBOX_DOSV
#define DOSBOX_DOSV

#include "setup.h"

#define	VTEXT_MODE_COUNT	4

enum DOSV_VTEXT_MODE {
	DOSV_VGA,				// 80x25
	DOSV_VTEXT_VGA,			// 80x30
	DOSV_VTEXT_SVGA,		// 100x37
	DOSV_VTEXT_XGA,			// 128x48
	DOSV_VTEXT_XGA_24,		// 85x32
	DOSV_VTEXT_SXGA,		// 160x64
	DOSV_VTEXT_SXGA_24,		// 106x42
};

enum DOSV_FONT {
	DOSV_FONT_8X16,
	DOSV_FONT_8X19,
	DOSV_FONT_16X16,
	DOSV_FONT_12X24,
	DOSV_FONT_24X24,

	DOSV_FONT_16X16_WRITE,
	DOSV_FONT_24X24_WRITE,

	DOSV_MSKANJI_API,

	DOSV_FONT_MAX
};

enum DOSV_FEP_CTRL {
	DOSV_FEP_CTRL_IAS = 1,
	DOSV_FEP_CTRL_MSKANJI = 2,
	DOSV_FEP_CTRL_BOTH = 3
};

bool INT10_DOSV_SetCRTBIOSMode(Bitu mode);
void DOSV_SetConfig(Section_prop *section);
void DOSV_Setup();
void DOSV_OffCursor();
void INT8_DOSV();
Bit16u DOSV_GetFontHandlerOffset(enum DOSV_FONT font);
enum DOSV_VTEXT_MODE DOSV_GetVtextMode(Bitu no = 0);
void DOSV_SetVTextRows(Bitu no, int row);
void DOSV_ResetVTextRows();
enum DOSV_FEP_CTRL DOSV_GetFepCtrl();
void SetTrueVideoMode(Bit8u mode);
Bit8u GetTrueVideoMode();
bool DOSV_CheckJapaneseVideoMode();

#endif
