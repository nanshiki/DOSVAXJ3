
#ifndef DOSBOX_J3_H
#define DOSBOX_J3_H

#ifndef DOSBOX_DOSBOX_H
#include "dosbox.h"
#endif

#include "control.h"

/* AX Global Area */
#define BIOSMEM_J3_SEG			0x40

#define BIOSMEM_J3_MODE			0xD0
#define BIOSMEM_J3_LINE_COUNT	0xD4
#define BIOSMEM_J3_GRAPH_ADDR	0xD6
#define BIOSMEM_J3_CODE_SEG		0xDA
#define BIOSMEM_J3_CODE_OFFSET	0xD8
#define BIOSMEM_J3_SCROLL		0xE2
#define BIOSMEM_J3_BLINK		0xE9

#define GRAPH_J3_SEG			0xb800

//int10_j3.cpp
bool INT10_J3_SetCRTBIOSMode(Bitu mode);
Bitu INT60_Handler(void);
Bitu INT6F_Handler(void);
void INT60_J3_Setup();
void INT8_J3();
void J3_OffCursor();
void J3_SetConfig(Section_prop *section);
void J3_GetPalette(Bit8u no, Bit8u &r, Bit8u &g, Bit8u &b);
Bit16u J3_GetMachineCode();
bool J3_IsJapanese();
void SetIMPosition();

#endif
