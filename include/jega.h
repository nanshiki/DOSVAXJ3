
#ifndef DOSBOX_JEGA
#define DOSBOX_JEGA

#ifndef DOSBOX_DOSBOX_H
#include "dosbox.h"
#endif

/* AX Global Area */
#define BIOSMEM_AX_SEG		0x40

#define BIOSMEM_AX_VTRAM_SEGADDR 0xE0
#define BIOSMEM_AX_GRAPH_CHAR 0xE2
#define BIOSMEM_AX_GRAPH_ATTR 0xE3
#define BIOSMEM_AX_JPNSTATUS 0xE4
#define BIOSMEM_AX_JEGA_RMOD1 0xE9
#define BIOSMEM_AX_JEGA_RMOD2 0xEA
#define BIOSMEM_AX_KBDSTATUS 0xEB

#define BIOS_KEYBOARD_AX_KBDSTATUS 0x4EB
/* 40h:EBh Keyboard Additional Status bits
	bit 2 : Kana holding
	bit 1 : Kana Lock key
	bit 0 : Kana LED indicating
*/

/* JEGA internal registers */
typedef struct JEGA_DATA_t {
	Bitu RMOD1//b9: Mode register 1
		, RMOD2//ba: Mode register 2
		, RDAGS//bb: ANK Group sel (not implemented)
		, RDFFB//bc: Font access first byte
		, RDFSB//bd: Font access second byte
		, RDFAP//be: Font Access Pattern
		, RPESL//09: end scan line (superceded by EGA)
		, RPULP//14: under scan line (superceded by EGA)
		, RPSSC//db: DBCS start scan line
		, RPSSU//d9: 2x DBCS upper start scan
		, RPSSL//da: 2x DBCS lower start scan
		, RPPAJ//dc: super imposed (only AX-2 system, not implemented)
		, RCMOD//dd: Cursor Mode (not implemented)
		, RCCLH//0e: Cursor location Upper bits (superceded by EGA)
		, RCCLL//0f: Cursor location Lower bits (superceded by EGA)
		, RCCSL//0a: Cursor Start Line (superceded by EGA)
		, RCCEL//0b: Cursor End Line (superceded by EGA)
		, RCSKW//de: Cursor Skew control (not implemented)
		, ROMSL//df: Unused?
		, RSTAT//bf: Font register accessible status
		;
	Bitu fontIndex = 0;
} JEGA_DATA;

extern JEGA_DATA jega;

//jfontload.cpp
extern Bit8u jfont_sbcs_19[];
extern Bit8u jfont_dbcs_16[];
extern Bit8u jfont_cache_dbcs_16[];
// for J-3100
extern Bit8u jfont_sbcs_16[];
extern Bit8u jfont_dbcs_24[];
extern Bit8u jfont_sbcs_24[];

//vga_jega.cpp
void SVGA_Setup_JEGA(void);//Init JEGA and AX system area

//int10_ax.cpp
bool INT10_AX_SetCRTBIOSMode(Bitu mode);
Bitu INT10_AX_GetCRTBIOSMode(void);
bool INT16_AX_SetKBDBIOSMode(Bitu mode);
Bitu INT16_AX_GetKBDBIOSMode(void);

//int10_char.cpp
void ReadVTRAMChar(Bit16u col, Bit16u row, Bit16u * result);
void SetVTRAMChar(Bit16u col, Bit16u row, Bit8u chr, Bit8u attr);
void WriteCharJ(Bit16u col, Bit16u row, Bit8u page, Bit8u chr, Bit8u attr, bool useattr);

bool isKanji1(Bit8u chr);
bool isKanji2(Bit8u chr);

//inline functions
inline bool isJEGAEnabled() {
	if (!IS_AX_ARCH) return false;
	return !(jega.RMOD1 & 0x40);
}

#endif
