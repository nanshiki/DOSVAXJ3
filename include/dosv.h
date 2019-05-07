
#ifndef DOSBOX_DOSV
#define DOSBOX_DOSV

bool INT10_DOSV_SetCRTBIOSMode(Bitu mode);
void DOSV_SetConfig(Section_prop *section);
void DOSV_Setup();
void DOSV_OffCursor();
void INT8_DOSV();
Bit16u DOSV_GetFontHandlerOffset(enum DOSV_FONT font);
bool DOSV_CheckSVGA();

enum DOSV_FONT {
	DOSV_FONT_8X16,
	DOSV_FONT_8X19,
	DOSV_FONT_16X16,
	DOSV_FONT_12X24,
	DOSV_FONT_24X24,

	DOSV_FONT_MAX
};

#endif
