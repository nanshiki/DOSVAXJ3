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

#include "jega.h"//for AX
#include "j3.h"
#include "jfont.h"
#include "dosv.h"

#include "dosbox.h"
#include "mem.h"
#include "callback.h"
#include "regs.h"
#include "inout.h"
#include "int10.h"
#include "mouse.h"
#include "setup.h"

Int10Data int10;
static Bitu call_10;
static bool warned_ff=false;

static Bitu INT10_Handler(void) {
#if 0
	switch (reg_ah) {
	case 0x02:
	case 0x03:
	case 0x09:
	case 0xc:
	case 0xd:
	case 0x0e:
	case 0x10:
	case 0x4f:

		break;
	default:
		LOG(LOG_INT10,LOG_NORMAL)("Function AX:%04X , BX %04X DX %04X",reg_ax,reg_bx,reg_dx);
		break;
	}
#endif
	if(IS_J3_ARCH && J3_IsJapanese()) {
		J3_OffCursor();
	} else if((IS_J3_ARCH || IS_DOSV) && DOSV_CheckJapaneseVideoMode()) {
		if(reg_ah != 0x03) {
			DOSV_OffCursor();
		}
	}
	INT10_SetCurMode();

	switch (reg_ah) {
	case 0x00:								/* Set VideoMode */
		Mouse_BeforeNewVideoMode(true);
		SetTrueVideoMode(reg_al);
		if(!IS_AX_ARCH && IS_DOS_JAPANESE && (reg_al == 0x03 || reg_al == 0x70 || reg_al == 0x72 || (reg_al >= 0x78 && reg_al <= 0x78 + VTEXT_MODE_COUNT - 1))) {
			Bit8u mode = reg_al;
			if(reg_al == 0x03 || reg_al == 0x72) {
				INT10_SetDOSVVtextRows(0, DOSV_VGA, 0);
				INT10_SetVideoMode(0x12);
				INT10_SetDOSVModeVtext(mode, DOSV_VGA);
			} else if(reg_al == 0x70 || (reg_al >= 0x78 && reg_al <= 0x78 + VTEXT_MODE_COUNT - 1)) {
				mode = 0x70;
				enum DOSV_VTEXT_MODE vtext_mode = DOSV_GetVtextMode((reg_al >= 0x78) ? (reg_al - 0x77) : 0);
				if(vtext_mode == DOSV_VTEXT_XGA || vtext_mode == DOSV_VTEXT_XGA_24) {
					if(svgaCard == SVGA_TsengET4K) {
						INT10_SetVideoMode(0x37);
					} else {
						INT10_SetVideoMode(0x104);
					}
					INT10_SetDOSVModeVtext(mode, vtext_mode);
				} else if(vtext_mode == DOSV_VTEXT_SXGA || vtext_mode == DOSV_VTEXT_SXGA_24) {
					if(svgaCard == SVGA_TsengET4K) {
						INT10_SetVideoMode(0x3d);
					} else {
						INT10_SetVideoMode(0x106);
					}
					INT10_SetDOSVModeVtext(mode, vtext_mode);
				} else if(vtext_mode == DOSV_VTEXT_SVGA) {
					INT10_SetVideoMode(0x6a);
					INT10_SetDOSVModeVtext(mode, vtext_mode);
				} else {
					INT10_SetVideoMode(0x12);
					INT10_SetDOSVModeVtext(mode, DOSV_VTEXT_VGA);
				}
			}
		} else {
			if(!IS_J3_ARCH && reg_al == 0x74) {
				break;
			}
			if(IS_J3_ARCH && (reg_al == 0x04 || reg_al == 0x05)) {
				INT10_SetVideoMode(0x74);
				INT10_SetJ3ModeCGA4(reg_al);
				break;
			}
			DOSV_ResetVTextRows();
			INT10_SetVideoMode(reg_al);
		}
		Mouse_AfterNewVideoMode(true);
		break;
	case 0x01:								/* Set TextMode Cursor Shape */
		INT10_SetCursorShape(reg_ch,reg_cl);
		break;
	case 0x02:								/* Set Cursor Pos */
		{
			Bit8u page = reg_bh;
			if(page == 0xff) {
				page = real_readb(BIOSMEM_SEG, BIOSMEM_CURRENT_PAGE);
			}
			INT10_SetCursorPos(reg_dh,reg_dl,page);
		}
		break;
	case 0x03:								/* get Cursor Pos and Cursor Shape*/
		{
			Bit8u page = reg_bh;
			if(page == 0xff) {
				page = real_readb(BIOSMEM_SEG, BIOSMEM_CURRENT_PAGE);
			}
			reg_dl=CURSOR_POS_COL(page);
			reg_dh=CURSOR_POS_ROW(page);
			reg_cx=real_readw(BIOSMEM_SEG,BIOSMEM_CURSOR_TYPE);
		}
		break;
	case 0x04:								/* read light pen pos YEAH RIGHT */
		/* Light pen is not supported */
		reg_ax=0;
		break;
	case 0x05:								/* Set Active Page */
		if ((reg_al & 0x80) && IS_TANDY_ARCH) {
			Bit8u crtcpu=real_readb(BIOSMEM_SEG, BIOSMEM_CRTCPU_PAGE);		
			switch (reg_al) {
			case 0x80:
				reg_bh=crtcpu & 7;
				reg_bl=(crtcpu >> 3) & 0x7;
				break;
			case 0x81:
				crtcpu=(crtcpu & 0xc7) | ((reg_bl & 7) << 3);
				break;
			case 0x82:
				crtcpu=(crtcpu & 0xf8) | (reg_bh & 7);
				break;
			case 0x83:
				crtcpu=(crtcpu & 0xc0) | (reg_bh & 7) | ((reg_bl & 7) << 3);
				break;
			}
			if (machine==MCH_PCJR) {
				/* always return graphics mapping, even for invalid values of AL */
				reg_bh=crtcpu & 7;
				reg_bl=(crtcpu >> 3) & 0x7;
			}
			IO_WriteB(0x3df,crtcpu);
			real_writeb(BIOSMEM_SEG, BIOSMEM_CRTCPU_PAGE,crtcpu);
		}
		else INT10_SetActivePage(reg_al);
		break;	
	case 0x06:								/* Scroll Up */
		INT10_ScrollWindow(reg_ch,reg_cl,reg_dh,reg_dl,-reg_al,reg_bh,0xFF);
		break;
	case 0x07:								/* Scroll Down */
		INT10_ScrollWindow(reg_ch,reg_cl,reg_dh,reg_dl,reg_al,reg_bh,0xFF);
		break;
	case 0x08:								/* Read character & attribute at cursor */
		//Is this right really?
		//INT10_ReadCharAttr(&reg_ax,reg_bh);
		INT10_ReadCharAttr(&reg_ax,reg_bh);
		break;						
	case 0x09:								/* Write Character & Attribute at cursor CX times */
		if (real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_MODE)==0x11)
			INT10_WriteChar(reg_al,(reg_bl&0x80)|0x3f,reg_bh,reg_cx,true);
		else INT10_WriteChar(reg_al,reg_bl,reg_bh,reg_cx,true);
		break;
	case 0x0A:								/* Write Character at cursor CX times */
		INT10_WriteChar(reg_al,reg_bl,reg_bh,reg_cx,false);
		break;
	case 0x0B:								/* Set Background/Border Colour & Set Palette*/
		switch (reg_bh) {
		case 0x00:		//Background/Border color
			INT10_SetBackgroundBorder(reg_bl);
			break;
		case 0x01:		//Set color Select
		default:
			INT10_SetColorSelect(reg_bl);
			break;
		}
		break;
	case 0x0C:								/* Write Graphics Pixel */
		INT10_PutPixel(reg_cx,reg_dx,reg_bh,reg_al);
		break;
	case 0x0D:								/* Read Graphics Pixel */
		INT10_GetPixel(reg_cx,reg_dx,reg_bh,&reg_al);
		break;
	case 0x0E:								/* Teletype OutPut */
		if(DOSV_CheckJapaneseVideoMode()) {
			Bit16u attr;
			INT10_ReadCharAttr(&attr, 0);
			INT10_TeletypeOutput(reg_al, attr >> 8);
		} else {
			INT10_TeletypeOutput(reg_al,reg_bl);
		}
		break;
	case 0x0F:								/* Get videomode */
		reg_bh=real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE);
		reg_al=real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_MODE);
		if (IS_EGAVGA_ARCH) reg_al|=real_readb(BIOSMEM_SEG,BIOSMEM_VIDEO_CTL)&0x80;
		reg_ah=(Bit8u)real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS);
		break;					
	case 0x10:								/* Palette functions */
		if (!IS_EGAVGA_ARCH && !IS_J3_ARCH && (reg_al>0x02)) break;
		else if (!IS_VGA_ARCH && !IS_J3_ARCH && (reg_al>0x03)) break;
		switch (reg_al) {
		case 0x00:							/* SET SINGLE PALETTE REGISTER */
			INT10_SetSinglePaletteRegister(reg_bl,reg_bh);
			break;
		case 0x01:							/* SET BORDER (OVERSCAN) COLOR*/
			INT10_SetOverscanBorderColor(reg_bh);
			break;
		case 0x02:							/* SET ALL PALETTE REGISTERS */
			INT10_SetAllPaletteRegisters(SegPhys(es)+reg_dx);
			break;
		case 0x03:							/* TOGGLE INTENSITY/BLINKING BIT */
			INT10_ToggleBlinkingBit(reg_bl);
			break;
		case 0x07:							/* GET SINGLE PALETTE REGISTER */
			INT10_GetSinglePaletteRegister(reg_bl,&reg_bh);
			break;
		case 0x08:							/* READ OVERSCAN (BORDER COLOR) REGISTER */
			INT10_GetOverscanBorderColor(&reg_bh);
			break;
		case 0x09:							/* READ ALL PALETTE REGISTERS AND OVERSCAN REGISTER */
			INT10_GetAllPaletteRegisters(SegPhys(es)+reg_dx);
			break;
		case 0x10:							/* SET INDIVIDUAL DAC REGISTER */
			INT10_SetSingleDACRegister(reg_bl,reg_dh,reg_ch,reg_cl);
			break;
		case 0x12:							/* SET BLOCK OF DAC REGISTERS */
			INT10_SetDACBlock(reg_bx,reg_cx,SegPhys(es)+reg_dx);
			break;
		case 0x13:							/* SELECT VIDEO DAC COLOR PAGE */
			INT10_SelectDACPage(reg_bl,reg_bh);
			break;
		case 0x15:							/* GET INDIVIDUAL DAC REGISTER */
			INT10_GetSingleDACRegister(reg_bl,&reg_dh,&reg_ch,&reg_cl);
			break;
		case 0x17:							/* GET BLOCK OF DAC REGISTER */
			INT10_GetDACBlock(reg_bx,reg_cx,SegPhys(es)+reg_dx);
			break;
		case 0x18:							/* undocumented - SET PEL MASK */
			INT10_SetPelMask(reg_bl);
			break;
		case 0x19:							/* undocumented - GET PEL MASK */
			INT10_GetPelMask(reg_bl);
			reg_bh=0;	// bx for get mask
			break;
		case 0x1A:							/* GET VIDEO DAC COLOR PAGE */
			INT10_GetDACPage(&reg_bl,&reg_bh);
			break;
		case 0x1B:							/* PERFORM GRAY-SCALE SUMMING */
			INT10_PerformGrayScaleSumming(reg_bx,reg_cx);
			break;
		case 0xF0:							/* ET4000: SET HiColor GRAPHICS MODE */
		case 0xF1:							/* ET4000: GET DAC TYPE */
		case 0xF2:							/* ET4000: CHECK/SET HiColor MODE */
		default:
			LOG(LOG_INT10,LOG_ERROR)("Function 10:Unhandled EGA/VGA Palette Function %2X",reg_al);
			break;
		}
		break;
	case 0x11:								/* Character generator functions */
		if(!IS_EGAVGA_ARCH && !IS_J3_ARCH) break;
		if(reg_al < 0x20 && DOSV_CheckJapaneseVideoMode()) break;

		if ((reg_al&0xf0)==0x10) Mouse_BeforeNewVideoMode(false);
		switch (reg_al) {
/* Textmode calls */
		case 0x00:			/* Load user font */
		case 0x10:
			INT10_LoadFont(SegPhys(es)+reg_bp,reg_al==0x10,reg_cx,reg_dx,reg_bl&0x7f,reg_bh);
			break;
		case 0x01:			/* Load 8x14 font */
		case 0x11:
			INT10_LoadFont(Real2Phys(int10.rom.font_14),reg_al==0x11,256,0,reg_bl&0x7f,14);
			break;
		case 0x02:			/* Load 8x8 font */
		case 0x12:
			INT10_LoadFont(Real2Phys(int10.rom.font_8_first),reg_al==0x12,256,0,reg_bl&0x7f,8);
			break;
		case 0x03:			/* Set Block Specifier */
			IO_Write(0x3c4,0x3);IO_Write(0x3c5,reg_bl);
			break;
		case 0x04:			/* Load 8x16 font */
		case 0x14:
			if (!IS_VGA_ARCH && !IS_J3_ARCH) break;
			INT10_LoadFont(Real2Phys(int10.rom.font_16),reg_al==0x14,256,0,reg_bl&0x7f,16);
			break;
/* Graphics mode calls */
		case 0x20:			/* Set User 8x8 Graphics characters */
			RealSetVec(0x1f,RealMake(SegValue(es),reg_bp));
			break;
		case 0x21:			/* Set user graphics characters */
			RealSetVec(0x43,RealMake(SegValue(es),reg_bp));
			real_writew(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT,reg_cx);
			goto graphics_chars;
		case 0x22:			/* Rom 8x14 set */
			RealSetVec(0x43,int10.rom.font_14);
			real_writew(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT,14);
			goto graphics_chars;
		case 0x23:			/* Rom 8x8 double dot set */
			RealSetVec(0x43,int10.rom.font_8_first);
			real_writew(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT,8);
			goto graphics_chars;
		case 0x24:			/* Rom 8x16 set */
			if (!IS_VGA_ARCH && !IS_J3_ARCH) break;
			RealSetVec(0x43,int10.rom.font_16);
			real_writew(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT,16);
			goto graphics_chars;
graphics_chars:
			switch (reg_bl) {
			case 0x00:real_writeb(BIOSMEM_SEG,BIOSMEM_NB_ROWS,reg_dl-1);break;
			case 0x01:real_writeb(BIOSMEM_SEG,BIOSMEM_NB_ROWS,13);break;
			case 0x03:real_writeb(BIOSMEM_SEG,BIOSMEM_NB_ROWS,42);break;
			case 0x02:
			default:real_writeb(BIOSMEM_SEG,BIOSMEM_NB_ROWS,24);break;
			}
			break;
/* General */
		case 0x30:/* Get Font Information */
			switch (reg_bh) {
			case 0x00:	/* interupt 0x1f vector */
				{
					RealPt int_1f=RealGetVec(0x1f);
					SegSet16(es,RealSeg(int_1f));
					reg_bp=RealOff(int_1f);
				}
				break;
			case 0x01:	/* interupt 0x43 vector */
				{
					RealPt int_43=RealGetVec(0x43);
					SegSet16(es,RealSeg(int_43));
					reg_bp=RealOff(int_43);
				}
				break;
			case 0x02:	/* font 8x14 */
				SegSet16(es,RealSeg(int10.rom.font_14));
				reg_bp=RealOff(int10.rom.font_14);
				break;
			case 0x03:	/* font 8x8 first 128 */
				SegSet16(es,RealSeg(int10.rom.font_8_first));
				reg_bp=RealOff(int10.rom.font_8_first);
				break;
			case 0x04:	/* font 8x8 second 128 */
				SegSet16(es,RealSeg(int10.rom.font_8_second));
				reg_bp=RealOff(int10.rom.font_8_second);
				break;
			case 0x05:	/* alpha alternate 9x14 */
				SegSet16(es,RealSeg(int10.rom.font_14_alternate));
				reg_bp=RealOff(int10.rom.font_14_alternate);
				break;
			case 0x06:	/* font 8x16 */
				if (!IS_VGA_ARCH && !IS_J3_ARCH) break;
				SegSet16(es,RealSeg(int10.rom.font_16));
				reg_bp=RealOff(int10.rom.font_16);
				break;
			case 0x07:	/* alpha alternate 9x16 */
				if (!IS_VGA_ARCH && !IS_J3_ARCH) break;
				SegSet16(es,RealSeg(int10.rom.font_16_alternate));
				reg_bp=RealOff(int10.rom.font_16_alternate);
				break;
			default:
				LOG(LOG_INT10,LOG_ERROR)("Function 11:30 Request for font %2X",reg_bh);	
				break;
			}
			if ((reg_bh<=7) || (svgaCard==SVGA_TsengET4K)) {
				reg_cx=real_readw(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT);
				reg_dl=real_readb(BIOSMEM_SEG,BIOSMEM_NB_ROWS);
			}
			break;
		default:
			LOG(LOG_INT10,LOG_ERROR)("Function 11:Unsupported character generator call %2X",reg_al);
			break;
		}
		if ((reg_al&0xf0)==0x10) Mouse_AfterNewVideoMode(false);
		break;
	case 0x12:								/* alternate function select */
		if (!IS_EGAVGA_ARCH && !IS_J3_ARCH) 
			break;
		switch (reg_bl) {
		case 0x10:							/* Get EGA Information */
			reg_bh=(real_readw(BIOSMEM_SEG,BIOSMEM_CRTC_ADDRESS)==0x3B4);	
			reg_bl=3;	//256 kb
			reg_cl=real_readb(BIOSMEM_SEG,BIOSMEM_SWITCHES) & 0x0F;
			reg_ch=real_readb(BIOSMEM_SEG,BIOSMEM_SWITCHES) >> 4;
			break;
		case 0x20:							/* Set alternate printscreen */
			break;
		case 0x30:							/* Select vertical resolution */
			{   
				if (!IS_VGA_ARCH && !IS_J3_ARCH) break;
				LOG(LOG_INT10,LOG_WARN)("Function 12:Call %2X (select vertical resolution)",reg_bl);
				if (svgaCard != SVGA_None) {
					if (reg_al > 2) {
						reg_al=0;		// invalid subfunction
						break;
					}
				}
				Bit8u modeset_ctl = real_readb(BIOSMEM_SEG,BIOSMEM_MODESET_CTL);
				Bit8u video_switches = real_readb(BIOSMEM_SEG,BIOSMEM_SWITCHES)&0xf0;
				switch(reg_al) {
				case 0: // 200
					modeset_ctl &= 0xef;
					modeset_ctl |= 0x80;
					video_switches |= 8;	// ega normal/cga emulation
					break;
				case 1: // 350
					modeset_ctl &= 0x6f;
					video_switches |= 9;	// ega enhanced
					break;
				case 2: // 400
					modeset_ctl &= 0x6f;
					modeset_ctl |= 0x10;	// use 400-line mode at next mode set
					video_switches |= 9;	// ega enhanced
					break;
				default:
					modeset_ctl &= 0xef;
					video_switches |= 8;	// ega normal/cga emulation
					break;
				}
				real_writeb(BIOSMEM_SEG,BIOSMEM_MODESET_CTL,modeset_ctl);
				real_writeb(BIOSMEM_SEG,BIOSMEM_SWITCHES,video_switches);
				reg_al=0x12;	// success
				break;
			}
		case 0x31:							/* Palette loading on modeset */
			{   
				if (!IS_VGA_ARCH && !IS_J3_ARCH) break;
				if (svgaCard==SVGA_TsengET4K) reg_al&=1;
				if (reg_al>1) {
					reg_al=0;		//invalid subfunction
					break;
				}
				Bit8u temp = real_readb(BIOSMEM_SEG,BIOSMEM_MODESET_CTL) & 0xf7;
				if (reg_al&1) temp|=8;		// enable if al=0
				real_writeb(BIOSMEM_SEG,BIOSMEM_MODESET_CTL,temp);
				reg_al=0x12;
				break;	
			}		
		case 0x32:							/* Video addressing */
			if (!IS_VGA_ARCH && !IS_J3_ARCH) break;
			LOG(LOG_INT10,LOG_ERROR)("Function 12:Call %2X not handled",reg_bl);
			if (svgaCard==SVGA_TsengET4K) reg_al&=1;
			if (reg_al>1) reg_al=0;		//invalid subfunction
			else reg_al=0x12;			//fake a success call
			break;
		case 0x33: /* SWITCH GRAY-SCALE SUMMING */
			{   
				if (!IS_VGA_ARCH && !IS_J3_ARCH) break;
				if (svgaCard==SVGA_TsengET4K) reg_al&=1;
				if (reg_al>1) {
					reg_al=0;
					break;
				}
				Bit8u temp = real_readb(BIOSMEM_SEG,BIOSMEM_MODESET_CTL) & 0xfd;
				if (!(reg_al&1)) temp|=2;		// enable if al=0
				real_writeb(BIOSMEM_SEG,BIOSMEM_MODESET_CTL,temp);
				reg_al=0x12;
				break;	
			}		
		case 0x34: /* ALTERNATE FUNCTION SELECT (VGA) - CURSOR EMULATION */
			{   
				// bit 0: 0=enable, 1=disable
				if (!IS_VGA_ARCH && !IS_J3_ARCH) break;
				if (svgaCard==SVGA_TsengET4K) reg_al&=1;
				if (reg_al>1) {
					reg_al=0;
					break;
				}
				Bit8u temp = real_readb(BIOSMEM_SEG,BIOSMEM_VIDEO_CTL) & 0xfe;
				real_writeb(BIOSMEM_SEG,BIOSMEM_VIDEO_CTL,temp|reg_al);
				reg_al=0x12;
				break;	
			}		
		case 0x35:
			if (!IS_VGA_ARCH && !IS_J3_ARCH) break;
			LOG(LOG_INT10,LOG_ERROR)("Function 12:Call %2X not handled",reg_bl);
			reg_al=0x12;
			break;
		case 0x36: {						/* VGA Refresh control */
			if (!IS_VGA_ARCH && !IS_J3_ARCH) break;
			if ((svgaCard==SVGA_S3Trio) && (reg_al>1)) {
				reg_al=0;
				break;
			}
			IO_Write(0x3c4,0x1);
			Bit8u clocking = IO_Read(0x3c5);
			
			if (reg_al==0) clocking &= ~0x20;
			else clocking |= 0x20;
			
			IO_Write(0x3c4,0x1);
			IO_Write(0x3c5,clocking);

			reg_al=0x12; // success
			break;
		}
		default:
			LOG(LOG_INT10,LOG_ERROR)("Function 12:Call %2X not handled",reg_bl);
			if (machine!=MCH_EGA) reg_al=0;
			break;
		}
		break;
	case 0x13:								/* Write String */
		if((reg_al & 0x10) != 0 && DOSV_CheckJapaneseVideoMode()) {
			INT10_ReadString(reg_dh,reg_dl,reg_al,reg_bl,SegPhys(es)+reg_bp,reg_cx,reg_bh);
		} else {
			INT10_WriteString(reg_dh,reg_dl,reg_al,reg_bl,SegPhys(es)+reg_bp,reg_cx,reg_bh);
		}
		break;
	case 0x18:
		if((IS_J3_ARCH || IS_DOSV) && DOSV_CheckJapaneseVideoMode()) {
			Bit8u *font;
			Bitu size = 0;
			if(reg_al == 0) {
				reg_al = 1;
				if(reg_bx == 0) {
					if(reg_dh == 8) {
						if(reg_dl == 16) {
							font = GetSbcsFont(reg_cl);
							size = 16;
						} else if(reg_dl == 19) {
							font = GetSbcs19Font(reg_cl);
							size = 19;
						}
					} else if(reg_dh == 16 && reg_dl == 16) {
						font = GetDbcsFont(reg_cx);
						size = 2 * 16;
					} else if(reg_dh == 12 && reg_dl == 24) {
						font = GetSbcs24Font(reg_cl);
						size = 2 * 24;
					} else if(reg_dh == 24 && reg_dl == 24) {
						font = GetDbcs24Font(reg_cx);
						size = 3 * 24;
					}
					if(size > 0) {
						Bit16u seg = SegValue(es);
						Bit16u off = reg_si;
						for(Bitu ct = 0 ; ct < size ; ct++) {
							real_writeb(seg, off++, *font++);
						}
						reg_al = 0;
					}
				}
			} else if(reg_al == 1) {
				if(reg_bx == 0) {
					if(reg_dh == 16 && reg_dl == 16) {
						if(SetGaijiData(reg_cx, PhysMake(SegValue(es), reg_si))) {
							reg_al = 0;
						}
					} else if(reg_dh == 24 && reg_dl == 24) {
						if(SetGaijiData24(reg_cx, PhysMake(SegValue(es), reg_si))) {
							reg_al = 0;
						}
					}
				}
			}
		}
		break;
	case 0x1A:								/* Display Combination */
		if (!IS_VGA_ARCH) break;
		if (reg_al<2) {
			INT10_DisplayCombinationCode(&reg_bx,reg_al==1);
			reg_ax=0x1A;	// high part destroyed or zeroed depending on BIOS
		}
		break;
	case 0x1B:								/* functionality State Information */
		if (!IS_VGA_ARCH) {
			if(IS_J3_ARCH) {
				if(real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_MODE) == 0x74) {
					break;
				}
			} else {
				break;
			}
		}
		switch (reg_bx) {
		case 0x0000:
			INT10_GetFuncStateInformation(SegPhys(es)+reg_di);
			reg_al=0x1B;
			break;
		default:
			LOG(LOG_INT10,LOG_ERROR)("1B:Unhandled call BX %2X",reg_bx);
			reg_al=0;
			break;
		}
		break;
	case 0x1C:	/* Video Save Area */
		if (!IS_VGA_ARCH) {
			if(IS_J3_ARCH) {
				if(real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_MODE) == 0x74) {
					break;
				}
			} else {
				break;
			}
		}
		switch (reg_al) {
			case 0: {
				Bitu ret=INT10_VideoState_GetSize(reg_cx);
				if (ret) {
					reg_al=0x1c;
					reg_bx=(Bit16u)ret;
				} else reg_al=0;
				}
				break;
			case 1:
				if (INT10_VideoState_Save(reg_cx,RealMake(SegValue(es),reg_bx))) reg_al=0x1c;
				else reg_al=0;
				break;
			case 2:
				if (INT10_VideoState_Restore(reg_cx,RealMake(SegValue(es),reg_bx))) reg_al=0x1c;
				else reg_al=0;
				break;
			default:
				if (svgaCard==SVGA_TsengET4K) reg_ax=0;
				else reg_al=0;
				break;
		}
		break;
	case 0x1d:
		if((IS_J3_ARCH || IS_DOSV) && DOSV_CheckJapaneseVideoMode()) {
			if(reg_al == 0x00) {
				real_writeb(BIOSMEM_SEG, BIOSMEM_NB_ROWS, int10.text_row - reg_bl);
			} else if(reg_al == 0x01) {
				real_writeb(BIOSMEM_SEG, BIOSMEM_NB_ROWS, int10.text_row);
			} else if(reg_al == 0x02) {
				reg_bx = int10.text_row - real_readb(BIOSMEM_SEG, BIOSMEM_NB_ROWS);
			}
		}
		break;
	case 0x4f:								/* VESA Calls */
		if ((!IS_VGA_ARCH && !IS_J3_ARCH) || (svgaCard!=SVGA_S3Trio)) break;
		switch (reg_al) {
		case 0x00:							/* Get SVGA Information */
			reg_al=0x4f;
			reg_ah=VESA_GetSVGAInformation(SegValue(es),reg_di);
			break;
		case 0x01:							/* Get SVGA Mode Information */
			reg_al=0x4f;
			reg_ah=VESA_GetSVGAModeInformation(reg_cx,SegValue(es),reg_di);
			break;
		case 0x02:							/* Set videomode */
			Mouse_BeforeNewVideoMode(true);
			reg_al=0x4f;
			reg_ah=VESA_SetSVGAMode(reg_bx);
			Mouse_AfterNewVideoMode(true);
			break;
		case 0x03:							/* Get videomode */
			reg_al=0x4f;
			reg_ah=VESA_GetSVGAMode(reg_bx);
			break;
		case 0x04:							/* Save/restore state */
			reg_al=0x4f;
			switch (reg_dl) {
				case 0: {
					Bitu ret=INT10_VideoState_GetSize(reg_cx);
					if (ret) {
						reg_ah=0;
						reg_bx=(Bit16u)ret;
					} else reg_ah=1;
					}
					break;
				case 1:
					if (INT10_VideoState_Save(reg_cx,RealMake(SegValue(es),reg_bx))) reg_ah=0;
					else reg_ah=1;
					break;
				case 2:
					if (INT10_VideoState_Restore(reg_cx,RealMake(SegValue(es),reg_bx))) reg_ah=0;
					else reg_ah=1;
					break;
				default:
					reg_ah=1;
					break;
			}
			break;
		case 0x05:							
			if (reg_bh==0) {				/* Set CPU Window */
				reg_ah=VESA_SetCPUWindow(reg_bl,reg_dl);
				reg_al=0x4f;
			} else if (reg_bh == 1) {		/* Get CPU Window */
				reg_ah=VESA_GetCPUWindow(reg_bl,reg_dx);
				reg_al=0x4f;
			} else {
				LOG(LOG_INT10,LOG_ERROR)("Unhandled VESA Function %X Subfunction %X",reg_al,reg_bh);
				reg_ah=0x01;
			}
			break;
		case 0x06:
			reg_al=0x4f;
			reg_ah=VESA_ScanLineLength(reg_bl,reg_cx,reg_bx,reg_cx,reg_dx);
			break;
		case 0x07:
			switch (reg_bl) {
			case 0x80:						/* Set Display Start during retrace */
			case 0x00:						/* Set display Start */
				reg_al=0x4f;
				reg_ah=VESA_SetDisplayStart(reg_cx,reg_dx,reg_bl==0x80);
				break;
			case 0x01:
				reg_al=0x4f;
				reg_bh=0x00;				//reserved
				reg_ah=VESA_GetDisplayStart(reg_cx,reg_dx);
				break;
			default:
				LOG(LOG_INT10,LOG_ERROR)("Unhandled VESA Function %X Subfunction %X",reg_al,reg_bl);
				reg_ah=0x1;
				break;
			}
			break;
		case 0x09:
			switch (reg_bl) {
			case 0x80:						/* Set Palette during retrace */
			case 0x00:						/* Set Palette */
				reg_ah=VESA_SetPalette(SegPhys(es)+reg_di,reg_dx,reg_cx,reg_bl==0x80);
				reg_al=0x4f;
				break;
			case 0x01:						/* Get Palette */
				reg_ah=VESA_GetPalette(SegPhys(es)+reg_di,reg_dx,reg_cx);
				reg_al=0x4f;
				break;
			default:
				LOG(LOG_INT10,LOG_ERROR)("Unhandled VESA Function %X Subfunction %X",reg_al,reg_bl);
				reg_ah=0x01;
				break;
			}
			break;
		case 0x0a:							/* Get Pmode Interface */
			if (int10.vesa_oldvbe) {
				reg_ax=0x014f;
				break;
			}
			switch (reg_bl) {
			case 0x00:
				SegSet16(es,RealSeg(int10.rom.pmode_interface));
				reg_di=RealOff(int10.rom.pmode_interface);
				reg_cx=int10.rom.pmode_interface_size;
				reg_ax=0x004f;
				break;
			case 0x01:						/* Get code for "set window" */
				SegSet16(es,RealSeg(int10.rom.pmode_interface));
				reg_di=RealOff(int10.rom.pmode_interface)+int10.rom.pmode_interface_window;
				reg_cx=int10.rom.pmode_interface_start-int10.rom.pmode_interface_window;
				reg_ax=0x004f;
				break;
			case 0x02:						/* Get code for "set display start" */
				SegSet16(es,RealSeg(int10.rom.pmode_interface));
				reg_di=RealOff(int10.rom.pmode_interface)+int10.rom.pmode_interface_start;
				reg_cx=int10.rom.pmode_interface_palette-int10.rom.pmode_interface_start;
				reg_ax=0x004f;
				break;
			case 0x03:						/* Get code for "set palette" */
				SegSet16(es,RealSeg(int10.rom.pmode_interface));
				reg_di=RealOff(int10.rom.pmode_interface)+int10.rom.pmode_interface_palette;
				reg_cx=int10.rom.pmode_interface_size-int10.rom.pmode_interface_palette;
				reg_ax=0x004f;
				break;
			default:
				reg_ax=0x014f;
				break;
			}
			break;

		default:
			LOG(LOG_INT10,LOG_ERROR)("Unhandled VESA Function %X",reg_al);
			reg_al=0x0;
			break;
		}
		break;
	// For AX architecture
	case 0x50:// Set/Read JP/US mode of CRT BIOS
		if(IS_AX_ARCH) {
			switch (reg_al) {
				case 0x00:
					LOG(LOG_INT10, LOG_NORMAL)("AX CRT BIOS 5000h is called.");
					if (INT10_AX_SetCRTBIOSMode(reg_bx)) reg_al = 0x00;
					else reg_al = 0x01;
					break;
				case 0x01:
					//LOG(LOG_INT10,LOG_NORMAL)("AX CRT BIOS 5001h is called.");
					reg_bx=INT10_AX_GetCRTBIOSMode();
					reg_al=0;
					break;
				default:
					LOG(LOG_INT10,LOG_ERROR)("Unhandled AX Function %X",reg_al);
					reg_al=0x0;
					break;
			}
		}
		break;
	case 0x51:// Save/Read JFONT pattern
		if(INT10_AX_GetCRTBIOSMode() == 0x01) break;//exit if CRT BIOS is in US mode
		switch (reg_al) {
			//INT 10h/AX=5100h Store user font pattern
			//IN
			//ES:BP=Index of store buffer
			//DX=Char code
			//BH=width bits of char
			//BL=height bits of char
			//OUT
			//AL=status 00h=Success 01h=Failed
		case 0x00:
		{
			LOG(LOG_INT10, LOG_NORMAL)("AX CRT BIOS 5100h is called.");
			Bitu buf_es = SegValue(es);
			Bitu buf_bp = reg_bp;
			Bitu chr = reg_dx;
			Bitu w_chr = reg_bh;
			Bitu h_chr = reg_bl;
			Bitu font;
			if (w_chr == 16 && h_chr == 16) {
				for (Bitu line = 0; line < 16; line++)
				{
					//Order of font pattern is different between FONTX2 and AX(JEGA).
					font = real_readb(buf_es, buf_bp + line);
					jfont_dbcs_16[chr * 32 + line * 2] = font;
					font = real_readb(buf_es, buf_bp + line + 16);
					jfont_dbcs_16[chr * 32 + line * 2 + 1] = font;
				}
				jfont_cache_dbcs_16[chr] = 1;
				reg_al = 0x00;
			}
			else
				reg_al = 0x01;
			break;
		}
		//INT 10h/AX=5101h Read character pattern
		//IN
		//ES:BP=Index of read buffer
		//DX=Char code
		//BH=width bits of char
		//BL=height bits of char
		//OUT
		//AL=status 00h=Success 01h=Failed
		case 0x01:
		{
			LOG(LOG_INT10, LOG_NORMAL)("AX CRT BIOS 5101h is called.");
			Bitu buf_es = SegValue(es);
			Bitu buf_bp = reg_bp;
			Bitu chr = reg_dx;
			Bitu w_chr = reg_bh;
			Bitu h_chr = reg_bl;
			Bitu font;
			if (w_chr == 8) {
				reg_al = 0x00;
				switch (h_chr)
				{
				case 8:
					for (Bitu line = 0; line < 8; line++)
						real_writeb(buf_es, buf_bp + line, int10_font_08[chr * 8 + line]);
					break;
				case 14:
					for (Bitu line = 0; line < 14; line++)
						real_writeb(buf_es, buf_bp + line, int10_font_14[chr * 14 + line]);
					break;
				case 19:
					for (Bitu line = 0; line < 19; line++)
						real_writeb(buf_es, buf_bp + line, jfont_sbcs_19[chr * 19 + line]);
					break;
				default:
					reg_al = 0x01;
					break;
				}
			}
			else if (w_chr == 16 && h_chr == 16) {
				GetDbcsFont(chr);
				reg_al = 0x00;
				for (Bitu line = 0; line < 16; line++)
				{
					font = jfont_dbcs_16[chr * 32 + line * 2];
					real_writeb(buf_es, buf_bp + line, font);
					font = jfont_dbcs_16[chr * 32 + line * 2 + 1];
					real_writeb(buf_es, buf_bp + line + 16, font);
				}
			}
			else
				reg_al = 0x01;
			break;
		}
		default:
			LOG(LOG_INT10,LOG_ERROR)("Unhandled AX Function %X",reg_al);
			reg_al=0x1;
			break;
		}
		break;
	case 0x52:// Set/Read virtual text ram buffer when the video mode is JEGA graphic mode
		if(INT10_AX_GetCRTBIOSMode() == 0x01) break;//exit if CRT BIOS is in US mode
		LOG(LOG_INT10,LOG_NORMAL)("AX CRT BIOS 52xxh is called.");
		switch (reg_al) {
		case 0x00:
		{
			if (reg_bx == 0) real_writew(BIOSMEM_AX_SEG, BIOSMEM_AX_VTRAM_SEGADDR, 0);
			else
			{
				LOG(LOG_INT10, LOG_NORMAL)("AX CRT BIOS set VTRAM segment address at %x", reg_bx);
				real_writew(BIOSMEM_AX_SEG, BIOSMEM_AX_VTRAM_SEGADDR, reg_bx);
				/* Fill VTRAM with 0x20(Space) */
				for (int y = 0; y < 25; y++)
					for (int x = 0; x < 80; x++)
						SetVTRAMChar(x, y, 0x20, 0x00);
			}
			break;
		}
		case 0x01:
		{
			Bitu vtram_seg = real_readw(BIOSMEM_AX_SEG, BIOSMEM_AX_VTRAM_SEGADDR);
			if (vtram_seg == 0x0000) reg_bx = 0;
			else reg_bx = vtram_seg;
			break;
		}
		default:
			LOG(LOG_INT10,LOG_ERROR)("Unhandled AX Function %X",reg_al);
			break;
		}
		break;
	case 0x82:// Set/Read the scroll mode when the video mode is JEGA graphic mode
		if(IS_J3_ARCH) {
			if(reg_al == 0x00) {
				// scroll mode
				reg_al = real_readb(BIOSMEM_J3_SEG, BIOSMEM_J3_SCROLL);
				if(reg_bl == 0x00 || reg_bl == 0x01) {
					real_writeb(BIOSMEM_J3_SEG, BIOSMEM_J3_SCROLL, 0x01);
				}
			} else if(reg_al == 0x04) {
				// cursor blink
				reg_al = real_readb(BIOSMEM_J3_SEG, BIOSMEM_J3_BLINK);
				if(reg_bl == 0x00 || reg_bl == 0x01) {
					real_writeb(BIOSMEM_J3_SEG, BIOSMEM_J3_BLINK, reg_bl);
				}
			} else if(reg_al == 0x05) {
				Section_prop *section = static_cast<Section_prop *>(control->GetSection("dosbox"));
				if (section && section->Get_bool("j3colordriver")) reg_bl = 0x01;
			}
			break;
		}
		if(INT10_AX_GetCRTBIOSMode() == 0x01) break;//exit if CRT BIOS is in US mode
		LOG(LOG_INT10,LOG_NORMAL)("AX CRT BIOS 82xxh is called.");
		switch (reg_al) {
		case 0x00:
			if (reg_bl == -1) {//Read scroll mode
				reg_al = real_readb(BIOSMEM_AX_SEG, BIOSMEM_AX_JPNSTATUS) & 0x01;
			}
			else {//Set scroll mode
				Bit8u tmp = real_readb(BIOSMEM_AX_SEG, BIOSMEM_AX_JPNSTATUS);
				reg_al = tmp & 0x01;//Return previous scroll mode
				tmp |= (reg_bl & 0x01);
				real_writeb(BIOSMEM_AX_SEG, BIOSMEM_AX_JPNSTATUS, tmp);
			}
			break;
		default:
			LOG(LOG_INT10,LOG_ERROR)("Unhandled AX Function %X",reg_al);
			break;
		}
		break;
	case 0x83:// Read the video RAM address and virtual text video RAM
		if(IS_J3_ARCH) {
			// AX=graphics offset, BX=text offset
			reg_ax = 0;
			reg_bx = 0;
			break;
		}
		//Out: AX=base address of video RAM, ES:BX=base address of virtual text video RAM
		if(INT10_AX_GetCRTBIOSMode() == 0x01) {
			break;//exit if CRT BIOS is in US mode
		}
		LOG(LOG_INT10,LOG_NORMAL)("AX CRT BIOS 83xxh is called.");
		switch (reg_al) {
		case 0x00:
		{
			reg_ax = CurMode->pstart;
			Bitu vtram_seg = real_readw(BIOSMEM_AX_SEG, BIOSMEM_AX_VTRAM_SEGADDR);
			RealMakeSeg(es, vtram_seg >> 4);
			reg_bx = vtram_seg << 4;
			break;
		}
		default:
			LOG(LOG_INT10,LOG_ERROR)("Unhandled AX Function %X",reg_al);
			break;
		}
		break;
	case 0x85:
		if (IS_J3_ARCH) {
			// Get attr
			reg_al = GetKanjiAttr();
		}
		break;
	case 0x88:
		if(IS_J3_ARCH) {
			// gaiji font table
			reg_bx = 0x0000;
			SegSet16(es, GetGaijiSeg());
		}
		break;
	case 0xf0:
		INT10_EGA_RIL_ReadRegister(reg_bl, reg_dx);
		break;
	case 0xf1:
		INT10_EGA_RIL_WriteRegister(reg_bl, reg_bh, reg_dx);
		break;
	case 0xf2:
		INT10_EGA_RIL_ReadRegisterRange(reg_ch, reg_cl, reg_dx, SegPhys(es)+reg_bx);
		break;
	case 0xf3:
		INT10_EGA_RIL_WriteRegisterRange(reg_ch, reg_cl, reg_dx, SegPhys(es)+reg_bx);
		break;
	case 0xf4:
		INT10_EGA_RIL_ReadRegisterSet(reg_cx, SegPhys(es)+reg_bx);
		break;
	case 0xf5:
		INT10_EGA_RIL_WriteRegisterSet(reg_cx, SegPhys(es)+reg_bx);
		break;
	case 0xfa: {
		RealPt pt=INT10_EGA_RIL_GetVersionPt();
		SegSet16(es,RealSeg(pt));
		reg_bx=RealOff(pt);
		}
		break;
	case 0xfe:
		if((IS_J3_ARCH || IS_DOSV) && DOSV_CheckJapaneseVideoMode()) {
			reg_di = 0x0000;
			SegSet16(es, GetTextSeg());
		}
		break;
	case 0xff:
		if((IS_J3_ARCH || IS_DOSV) && DOSV_CheckJapaneseVideoMode()) {
			WriteCharTopView(reg_di, reg_cx);
		} else {
			if (!warned_ff) LOG(LOG_INT10,LOG_NORMAL)("INT10:FF:Weird NC call");
			warned_ff=true;
		}
		break;
	default:
		LOG(LOG_INT10,LOG_ERROR)("Function %4X not supported",reg_ax);
//		reg_al=0x00;		//Successfull, breaks marriage
		break;
	};
	return CBRET_NONE;
}

static void INT10_Seg40Init(void) {
	// Set the default MSR
	real_writeb(BIOSMEM_SEG,BIOSMEM_CURRENT_MSR,0x09);
	if (IS_EGAVGA_ARCH || IS_J3_ARCH) {
		// Set the default char height
		real_writeb(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT,16);
		// Clear the screen 
		real_writeb(BIOSMEM_SEG,BIOSMEM_VIDEO_CTL,0x60);
		// Set the basic screen we have
		real_writeb(BIOSMEM_SEG,BIOSMEM_SWITCHES,0xF9);
		// Set the basic modeset options
		real_writeb(BIOSMEM_SEG,BIOSMEM_MODESET_CTL,0x51);
		// Set the pointer to video save pointer table
		real_writed(BIOSMEM_SEG,BIOSMEM_VS_POINTER,int10.rom.video_save_pointers);
	}
}


static void INT10_InitVGA(void) {
	if (IS_EGAVGA_ARCH || IS_J3_ARCH) {
		/* switch to color mode and enable CPU access 480 lines */
		IO_Write(0x3c2,0xc3);
		/* More than 64k */
		IO_Write(0x3c4,0x04);
		IO_Write(0x3c5,0x02);
		if (IS_VGA_ARCH || IS_J3_ARCH) {
			/* Initialize DAC */
			IO_Write(0x3c8,0);
			for (Bitu i=0;i<3*256;i++) IO_Write(0x3c9,0);
		}
	}
}

static void SetupTandyBios(void) {
	static Bit8u TandyConfig[130]= {
		0x21, 0x42, 0x49, 0x4f, 0x53, 0x20, 0x52, 0x4f, 0x4d, 0x20, 0x76, 0x65, 0x72,
		0x73, 0x69, 0x6f, 0x6e, 0x20, 0x30, 0x32, 0x2e, 0x30, 0x30, 0x2e, 0x30, 0x30,
		0x0d, 0x0a, 0x43, 0x6f, 0x6d, 0x70, 0x61, 0x74, 0x69, 0x62, 0x69, 0x6c, 0x69,
		0x74, 0x79, 0x20, 0x53, 0x6f, 0x66, 0x74, 0x77, 0x61, 0x72, 0x65, 0x0d, 0x0a,
		0x43, 0x6f, 0x70, 0x79, 0x72, 0x69, 0x67, 0x68, 0x74, 0x20, 0x28, 0x43, 0x29,
		0x20, 0x31, 0x39, 0x38, 0x34, 0x2c, 0x31, 0x39, 0x38, 0x35, 0x2c, 0x31, 0x39,
		0x38, 0x36, 0x2c, 0x31, 0x39, 0x38, 0x37, 0x0d, 0x0a, 0x50, 0x68, 0x6f, 0x65,
		0x6e, 0x69, 0x78, 0x20, 0x53, 0x6f, 0x66, 0x74, 0x77, 0x61, 0x72, 0x65, 0x20,
		0x41, 0x73, 0x73, 0x6f, 0x63, 0x69, 0x61, 0x74, 0x65, 0x73, 0x20, 0x4c, 0x74,
		0x64, 0x2e, 0x0d, 0x0a, 0x61, 0x6e, 0x64, 0x20, 0x54, 0x61, 0x6e, 0x64, 0x79
	};
	if (machine==MCH_TANDY) {
		Bitu i;
		for(i=0;i<130;i++) {
			phys_writeb(0xf0000+i+0xc000, TandyConfig[i]);
		}
	}
}

void INT10_Init(Section* /*sec*/) {
	INT10_InitVGA();
	if (IS_TANDY_ARCH) SetupTandyBios();
	/* Setup the INT 10 vector */
	call_10=CALLBACK_Allocate();	
	CALLBACK_Setup(call_10,&INT10_Handler,CB_IRET,"Int 10 video");
	RealSetVec(0x10,CALLBACK_RealPointer(call_10));
	//Init the 0x40 segment and init the datastructures in the the video rom area
	INT10_SetupRomMemory();
	INT10_Seg40Init();
	INT10_SetVideoMode(0x3);
	SetTrueVideoMode(0x03);
}
