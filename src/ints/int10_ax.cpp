/*
Copyright (c) 2016, akm
All rights reserved.
This content is under the MIT License.
*/
#include "dosbox.h"
#include "mem.h"
#include "inout.h"
#include "int10.h"
//#include "vga.h"
#include "jega.h"//for AX

Bitu INT10_AX_GetCRTBIOSMode(void) {
	if (!IS_AX_ARCH) return 0x01;
	if (real_readb(BIOSMEM_AX_SEG, BIOSMEM_AX_JPNSTATUS) & 0x80) return 0x51;//if in US mode
	else return 0x01;
}
bool INT10_AX_SetCRTBIOSMode(Bitu mode) {
	if (!IS_AX_ARCH) return false;
	Bit8u tmp = real_readb(BIOSMEM_AX_SEG, BIOSMEM_AX_JPNSTATUS);
	switch (mode) {
		//Todo: verify written value
	case 0x01:
		real_writeb(BIOSMEM_AX_SEG, BIOSMEM_AX_JPNSTATUS, tmp & 0x7F);
		LOG(LOG_INT10, LOG_NORMAL)("AX CRT BIOS has been set to US mode.");
		INT10_SetVideoMode(0x03);
		return true;
		/* -------------------SET to JP mode in CRT BIOS -------------------- */
	case 0x51:
		real_writeb(BIOSMEM_AX_SEG, BIOSMEM_AX_JPNSTATUS, tmp | 0x80);
		LOG(LOG_INT10, LOG_NORMAL)("AX CRT BIOS has been set to JP mode.");
		//		Mouse_BeforeNewVideoMode(true);
		// change to the default video mode (03h) with vram cleared
		INT10_SetVideoMode(0x03);
		//		Mouse_AfterNewVideoMode(true);;
		return true;
	default:
		return false;
	}
}
Bitu INT16_AX_GetKBDBIOSMode(void) {
	if (!IS_AX_ARCH) return 0x01;
	if (real_readb(BIOSMEM_AX_SEG, BIOSMEM_AX_JPNSTATUS) & 0x40) return 0x51;//if in US mode
	else return 0x01;
}

bool INT16_AX_SetKBDBIOSMode(Bitu mode) {
	if (!IS_AX_ARCH) return false;
	Bit8u tmp = real_readb(BIOSMEM_AX_SEG, BIOSMEM_AX_JPNSTATUS);
	switch (mode) {
		//Todo: verify written value
	case 0x01:
		real_writeb(BIOSMEM_AX_SEG, BIOSMEM_AX_JPNSTATUS, tmp & 0xBF);
		LOG(LOG_INT10, LOG_NORMAL)("AX KBD BIOS has been set to US mode.");
		return true;
	case 0x51:
		real_writeb(BIOSMEM_AX_SEG, BIOSMEM_AX_JPNSTATUS, tmp | 0x40);
		LOG(LOG_INT10, LOG_NORMAL)("AX KBD BIOS has been set to JP mode.");
		return true;
	default:
		return false;
	}
}