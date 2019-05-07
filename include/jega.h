
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

/* JEGA internal registers */
typedef struct {
	Bitu RMOD1
		, RMOD2
		, RDAGS
		, RDFFB
		, RDFSB
		, RDFAP
		, RPESL
		, RPULP
		, RPSSC
		, RPSSU
		, RPSSL
		, RPPAJ
		, RCMOD
		, RCCLH
		, RCCLL
		, RCCSL
		, RCCEL
		, RCSKW
		, ROMSL
		, RSTAT
		;
	Bitu fontIndex = 0;
} JEGA_DATA;

extern JEGA_DATA jega;

//jfontload.cpp
extern Bit8u jfont_sbcs_19[256 * 19];//SBCS font 256 * 19( * 8)
extern Bit8u jfont_dbcs_16[65536 * 32];//DBCS font 65536 * 16 * 2 (* 8)
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
extern Bit8u prevchr;
void ReadVTRAMChar(Bit16u col, Bit16u row, Bit16u * result);
void SetVTRAMChar(Bit16u col, Bit16u row, Bit8u chr, Bit8u attr);
void WriteCharJ(Bit16u col, Bit16u row, Bit8u page, Bit8u chr, Bit8u attr, bool useattr);

//inline functions
inline bool isKanji1(Bit8u chr) { return (chr >= 0x81 && chr <= 0x9f) || (chr >= 0xe0 && chr <= 0xfc); }
inline bool isKanji2(Bit8u chr) { return (chr >= 0x40 && chr <= 0x7e) || (chr >= 0x80 && chr <= 0xfc); }
inline bool isJEGAEnabled() {
	if (!IS_AX_ARCH) return false;
	return !(jega.RMOD1 & 0x40);
}

#endif
