/*
 *  Copyright (C) 2002-2021  The DOSBox Team
 *  Copyright (C) 2016 akm
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */
#include <string.h>
#include "jega.h"//for AX

#include "setup.h"
#include "vga.h"
#include "inout.h"
#include "mem.h"

JEGA_DATA jega;

// Store font
void JEGA_writeFont() {
	jega.RSTAT &= ~0x02;
	Bitu chr = jega.RDFFB;
	Bitu chr_2 = jega.RDFSB;
	// Check the char code is in Wide charset of Shift-JIS
	if ((chr >= 0x40 && chr <= 0x7e) || (chr >= 0x80 && chr <= 0xfc)) {
		if (jega.fontIndex >= 32) jega.fontIndex = 0;
		chr <<= 8;
		//fix vertical char position
		chr |= chr_2;
		if (jega.fontIndex < 16)
			// read font from register and store it
			jfont_dbcs_16[chr * 32 + jega.fontIndex * 2] = jega.RDFAP;// w16xh16 font
		else
			jfont_dbcs_16[chr * 32 + (jega.fontIndex - 16) * 2 + 1] = jega.RDFAP;// w16xh16 font
	}
	else
	{
		if (jega.fontIndex >= 19) jega.fontIndex = 0;
		jfont_sbcs_19[chr * 19 + jega.fontIndex] = jega.RDFAP;// w16xh16 font
	}
	jega.fontIndex++;
	jega.RSTAT |= 0x02;
}

// Read font
void JEGA_readFont() {
	jega.RSTAT &= ~0x02;
	Bitu chr = jega.RDFFB;
	Bitu chr_2 = jega.RDFSB;
	// Check the char code is in Wide charset of Shift-JIS
	if ((chr >= 0x40 && chr <= 0x7e) || (chr >= 0x80 && chr <= 0xfc)) {
		if (jega.fontIndex >= 32) jega.fontIndex = 0;
		chr <<= 8;
		//fix vertical char position
		chr |= chr_2;
		if (jega.fontIndex < 16)
			// get font and set to register
			jega.RDFAP = jfont_dbcs_16[chr * 32 + jega.fontIndex * 2];// w16xh16 font
		else
			jega.RDFAP = jfont_dbcs_16[chr * 32 + (jega.fontIndex - 16) * 2 + 1];
	}
	else
	{
		if (jega.fontIndex >= 19) jega.fontIndex = 0;
		jega.RDFAP = jfont_sbcs_19[chr * 19 + jega.fontIndex];// w16xh16 font
	}
	jega.fontIndex++;
	jega.RSTAT |= 0x02;
}

void write_p3d5_jega(Bitu reg, Bitu val, Bitu iolen) {
	switch (reg) {
	case 0xb9://Mode register 1
		jega.RMOD1 = val;
		break;
	case 0xba://Mode register 2
		jega.RMOD2 = val;
		break;
	case 0xbb://ANK Group sel
		jega.RDAGS = val;
		break;
	case 0xbc:// Font access first byte
		if (jega.RDFFB != val) {
			jega.RDFFB = val;
			jega.fontIndex = 0;
		}
		break;
	case 0xbd:// Font access Second Byte
		if (jega.RDFSB != val) {
			jega.RDFSB = val;
			jega.fontIndex = 0;
		}
		break;
	case 0xbe:// Font Access Pattern
		jega.RDFAP = val;
		JEGA_writeFont();
		break;
	//case 0x09:// end scan line
	//	jega.RPESL = val;
	//	break;
	//case 0x14:// under scan line
	//	jega.RPULP = val;
	//	break;
	case 0xdb:
		jega.RPSSC = val;
		break;
	case 0xd9:
		jega.RPSSU = val;
		break;
	case 0xda:
		jega.RPSSL = val;
		break;
	case 0xdc://super imposed (only AX-2 system, not implemented)
		jega.RPPAJ = val;
		break;
	case 0xdd:
		jega.RCMOD = val;
		break;
	//case 0x0e://Cursor location Upper bits
	//	jega.RCCLH = val;
	//	break;
	//case 0x0f://Cursor location Lower bits
	//	jega.RCCLL = val;
	//	break;
	//case 0x0a://Cursor Start Line
	//	jega.RCCSL = val;
	//	break;
	//case 0x0b://Cursor End Line
	//	jega.RCCEL = val;
	//	break;
	case 0xde://Cursor Skew control
		jega.RCSKW = val;
		break;
	case 0xdf://Unused?
		jega.ROMSL = val;
		break;
	case 0xbf://font r/w register
		jega.RSTAT = val;
		break;
	default:
		LOG(LOG_VGAMISC, LOG_NORMAL)("VGA:GFX:JEGA:Write to illegal index %2X", reg);
		break;
	}
}
//CRT Control Register can be read from I/O 3D5h, after setting index at I/O 3D4h
Bitu read_p3d5_jega(Bitu reg, Bitu iolen) {
	switch (reg) {
	case 0xb9:
		return jega.RMOD1;
	case 0xba:
		return jega.RMOD2;
	case 0xbb:
		return jega.RDAGS;
	case 0xbc:// BCh RDFFB Font access First Byte
		return jega.RDFFB;
	case 0xbd:// BDh RDFFB Font access Second Byte
		return jega.RDFSB;
	case 0xbe:// BEh RDFAP Font Access Pattern
		JEGA_readFont();
		return jega.RDFAP;
	//case 0x09:
	//	return jega.RPESL;
	//case 0x14:
	//	return jega.RPULP;
	case 0xdb:
		return jega.RPSSC;
	case 0xd9:
		return jega.RPSSU;
	case 0xda:
		return jega.RPSSL;
	case 0xdc:
		return jega.RPPAJ;
	case 0xdd:
		return jega.RCMOD;
	//case 0x0e:
	//	return jega.RCCLH;
	//case 0x0f:
	//	return jega.RCCLL;
	//case 0x0a:
	//	return jega.RCCSL;
	//case 0x0b:
	//	return jega.RCCEL;
	case 0xde:
		return jega.RCSKW;
	case 0xdf:
		return jega.ROMSL;
	case 0xbf:
		return 0x03;//Font always read/writeable
	default:
		LOG(LOG_VGAMISC, LOG_NORMAL)("VGA:GFX:JEGA:Read from illegal index %2X", reg);
		break;
	}
	return 0x0;
}

void JEGA_setupAX(void) {
	memset(&jega, 0, sizeof(JEGA_DATA));
	jega.RMOD1 = 0xC8;//b9: Mode register 1
	jega.RMOD2 = 0x00;//ba: Mode register 2
	jega.RDAGS = 0x00;//bb: ANK Group sel (not implemented)
	jega.RDFFB = 0x00;//bc: Font access first byte
	jega.RDFSB = 0x00;//bd: second
	jega.RDFAP = 0x00;//be: Font Access Pattern
	jega.RPESL = 0x00;//09: end scan line (superceded by EGA)
	jega.RPULP = 0x00;//14: under scan line (superceded by EGA)
	jega.RPSSC = 1;//db: DBCS start scan line
	jega.RPSSU = 3;//d9: 2x DBCS upper start scan
	jega.RPSSL = 0;//da: 2x DBCS lower start scan
	jega.RPPAJ = 0x00;//dc: super imposed (only AX-2 system, not implemented)
	jega.RCMOD = 0x00;//dd: Cursor Mode (not implemented)
	jega.RCCLH = 0x00;//0e: Cursor location Upper bits (superceded by EGA)
	jega.RCCLL = 0x00;//0f: Cursor location Lower bits (superceded by EGA)
	jega.RCCSL = 0x00;//0a: Cursor Start Line (superceded by EGA)
	jega.RCCEL = 0x00;//0b: Cursor End Line (superceded by EGA)
	jega.RCSKW = 0x20;//de: Cursor Skew control (not implemented)
	jega.ROMSL = 0x00;//df: Unused?
	jega.RSTAT = 0x03;//bf: Font register accessible status
	real_writeb(BIOSMEM_AX_SEG, BIOSMEM_AX_JPNSTATUS, 0);
	real_writeb(BIOSMEM_AX_SEG, BIOSMEM_AX_JEGA_RMOD1, jega.RMOD1);
	real_writeb(BIOSMEM_AX_SEG, BIOSMEM_AX_JEGA_RMOD2, jega.RMOD2);
	real_writeb(BIOSMEM_AX_SEG, BIOSMEM_AX_GRAPH_ATTR, 0);
	real_writeb(BIOSMEM_AX_SEG, BIOSMEM_AX_GRAPH_CHAR, 0);
	real_writeb(BIOSMEM_AX_SEG, BIOSMEM_AX_VTRAM_SEGADDR, 0);
	real_writeb(BIOSMEM_AX_SEG, BIOSMEM_AX_KBDSTATUS, 0x00);
}

void SVGA_Setup_JEGA(void) {
	JEGA_setupAX();
	svga.write_p3d5 = &write_p3d5_jega;
	svga.read_p3d5 = &read_p3d5_jega;

	// Adjust memory to 256K
	vga.vmemsize = vga.vmemwrap = 256 * 1024;

	/* JEGA BIOS ROM signature for AX architecture
	To run MS-DOS, signature ("JA") must be 
	put at C000h:N*512-18+2 ;N=Number of ROM blocks (rom_base+2)
	*/
	PhysPt rom_base = PhysMake(0xc000, 0);
	phys_writeb(rom_base + 0x40 * 512 - 18 + 2, 'J');
	phys_writeb(rom_base + 0x40 * 512 - 18 + 3, 'A');
}
