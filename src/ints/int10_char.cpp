/*
 *  Copyright (C) 2002-2015  The DOSBox Team
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


/* Character displaying moving functions */

#include "dosbox.h"
#include "bios.h"
#include "mem.h"
#include "inout.h"
#include "int10.h"
#include "pic.h"
#include "regs.h"
#include "callback.h"
#include "jega.h"//for AX
#include "j3.h"
#include "jfont.h"
#include "dosv.h"
#include "SDL_events.h"

Bit8u prevchr = 0;

static void DCGA_CopyRow(Bit8u cleft,Bit8u cright,Bit8u rold,Bit8u rnew,PhysPt base) {
	Bit8u cheight = real_readb(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT);
	PhysPt dest=base+((CurMode->twidth*rnew)*(cheight/4)+cleft);
	PhysPt src=base+((CurMode->twidth*rold)*(cheight/4)+cleft);
	Bitu copy=(cright-cleft);
	Bitu nextline=CurMode->twidth;
	for (Bitu i=0;i<cheight/4U;i++) {
		MEM_BlockCopy(dest,src,copy);
		MEM_BlockCopy(dest+8*1024,src+8*1024,copy);
		MEM_BlockCopy(dest+8*1024*2,src+8*1024*2,copy);
		MEM_BlockCopy(dest+8*1024*3,src+8*1024*3,copy);

		dest+=nextline;src+=nextline;
	}
}

static void DCGA_Text_CopyRow(Bit8u cleft,Bit8u cright,Bit8u rold,Bit8u rnew) {
	Bitu textcopy = (cright - cleft);
	Bitu dest = real_readw(BIOSMEM_SEG, BIOSMEM_NB_COLS) * 2 * rnew + cleft * 2;
	Bitu src = real_readw(BIOSMEM_SEG, BIOSMEM_NB_COLS) * 2 * rold + cleft * 2;
	Bit16u seg = GetTextSeg();
	for(Bitu x = 0 ; x < textcopy ; x++) {
		real_writeb(seg, dest + x * 2, real_readb(seg, src + x * 2));
		real_writeb(seg, dest + x * 2 + 1, real_readb(seg, src + x * 2 + 1));
	}
}

static void CGA2_CopyRow(Bit8u cleft,Bit8u cright,Bit8u rold,Bit8u rnew,PhysPt base) {
	Bit8u cheight = real_readb(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT);
	PhysPt dest=base+((CurMode->twidth*rnew)*(cheight/2)+cleft);
	PhysPt src=base+((CurMode->twidth*rold)*(cheight/2)+cleft);
	Bitu copy=(cright-cleft);
	Bitu nextline=CurMode->twidth;
	for (Bitu i=0;i<cheight/2U;i++) {
		MEM_BlockCopy(dest,src,copy);
		MEM_BlockCopy(dest+8*1024,src+8*1024,copy);
		dest+=nextline;src+=nextline;
	}
}

static void CGA4_CopyRow(Bit8u cleft,Bit8u cright,Bit8u rold,Bit8u rnew,PhysPt base) {
	Bit8u cheight = real_readb(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT);
	PhysPt dest=base+((CurMode->twidth*rnew)*(cheight/2)+cleft)*2;
	PhysPt src=base+((CurMode->twidth*rold)*(cheight/2)+cleft)*2;	
	Bitu copy=(cright-cleft)*2;Bitu nextline=CurMode->twidth*2;
	for (Bitu i=0;i<cheight/2U;i++) {
		MEM_BlockCopy(dest,src,copy);
		MEM_BlockCopy(dest+8*1024,src+8*1024,copy);
		dest+=nextline;src+=nextline;
	}
}

static void TANDY16_CopyRow(Bit8u cleft,Bit8u cright,Bit8u rold,Bit8u rnew,PhysPt base) {
	Bit8u cheight = real_readb(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT);
	Bit8u banks=CurMode->twidth/10;
	PhysPt dest=base+((CurMode->twidth*rnew)*(cheight/banks)+cleft)*4;
	PhysPt src=base+((CurMode->twidth*rold)*(cheight/banks)+cleft)*4;
	Bitu copy=(cright-cleft)*4;Bitu nextline=CurMode->twidth*4;
	for (Bit8u i=0;i<cheight/banks;i++) {
		for (Bitu b=0;b<banks;b++) MEM_BlockCopy(dest+b*8*1024,src+b*8*1024,copy);
		dest+=nextline;src+=nextline;
	}
}

static void EGA16_CopyRow(Bit8u cleft,Bit8u cright,Bit8u rold,Bit8u rnew,PhysPt base) {
	PhysPt src,dest;
	Bitu copy;
	Bit8u cheight = real_readb(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT);
	Bitu rowsize=(cright-cleft);
	Bitu nextline=CurMode->twidth;
	dest=(CurMode->twidth*rnew)*cheight+cleft;
	src=(CurMode->twidth*rold)*cheight+cleft;
	/* Setup registers correctly */
	IO_Write(0x3ce,5);IO_Write(0x3cf,1);		/* Memory transfer mode */
	IO_Write(0x3c4,2);IO_Write(0x3c5,0xf);		/* Enable all Write planes */
	/* Do some copying */
	Bit8u select;
	if(svgaCard == SVGA_TsengET4K) {
		select = 0x00;
		if(src >= 0x20000) {
			select |= 0x20;
			src -= 0x20000;
		} else if(src >= 0x10000) {
			select |= 0x10;
			src -= 0x10000;
		}
		if(dest >= 0x20000) {
			select |= 0x02;
			dest -= 0x20000;
		} else if(dest >= 0x10000) {
			select |= 0x01;
			dest -= 0x10000;
		}
		IO_Write(0x3cd, select);
	}
	for(copy = 0 ; copy < cheight ; copy++) {
		PhysPt src_pt = src;
		PhysPt dest_pt = dest;
		for(Bitu x = 0 ; x < rowsize ; x++) {
			if(svgaCard == SVGA_TsengET4K) {
				if(src_pt >= 0x10000) {
					if((select & 0xf0) == 0x00) {
						select = (select & 0x0f) | 0x10;
					} else if((select & 0xf0) == 0x10) {
						select = (select & 0x0f) | 0x20;
					}
					src_pt -= 0x10000;
					src -= 0x10000;
					IO_Write(0x3cd, select);
				}
				if(dest_pt >= 0x10000) {
					if((select & 0x0f) == 0x00) {
						select = (select & 0xf0) | 0x01;
					} else if((select & 0x0f) == 0x01) {
						select = (select & 0xf0) | 0x02;
					}
					dest_pt -= 0x10000;
					dest -= 0x10000;
					IO_Write(0x3cd, select);
				}
			}
			mem_writeb(base + dest_pt, mem_readb(base + src_pt));
			src_pt++;
			dest_pt++;
		}
		dest += nextline;
		src += nextline;
	}
	/* Restore registers */
	IO_Write(0x3ce,5);IO_Write(0x3cf,0);		/* Normal transfer mode */
	if(svgaCard == SVGA_TsengET4K) {
		IO_Write(0x3cd, 0);
	}
}

static void CopyRowMask(PhysPt base, PhysPt dest_pt, PhysPt src_pt, Bit8u mask)
{
	Bit8u no;
	Bit8u plane[4];

	IO_Write(0x3ce, 5); IO_Write(0x3cf, 0);
	for(no = 0 ; no < 4 ; no++) {
		IO_Write(0x3ce, 4); IO_Write(0x3cf, no);
		plane[no] = mem_readb(base + dest_pt) & (mask ^ 0xff);
		plane[no] |= mem_readb(base + src_pt) & mask;
	}
	IO_Write(0x3ce, 5); IO_Write(0x3cf, 8);
	IO_Write(0x3ce, 1); IO_Write(0x3cf, 0);
	IO_Write(0x3ce, 7); IO_Write(0x3cf, 0);
	IO_Write(0x3ce, 3); IO_Write(0x3cf, 0);
	IO_Write(0x3ce, 8); IO_Write(0x3cf, 0xff);
	for(no = 0 ; no < 4 ; no++) {
		IO_Write(0x3c4, 2); IO_Write(0x3c5, 1 << no);
		mem_writeb(base + dest_pt, plane[no]);
	}

	IO_Write(0x3ce, 5); IO_Write(0x3cf, 1);
	IO_Write(0x3c4, 2); IO_Write(0x3c5, 0xf);
}

static void EGA16_CopyRow_24(Bit8u cleft,Bit8u cright,Bit8u rold,Bit8u rnew,PhysPt base) {
	PhysPt src, dest;
	Bitu rowsize;
	Bitu start = (cleft * 12) / 8;
	Bitu width = (real_readw(BIOSMEM_SEG, BIOSMEM_NB_COLS) == 85) ? 128 : 160;

	rowsize = (cright - cleft) * 12 / 8;
	if((cleft & 1) || (cright & 1)) {
		rowsize++;
	}
	dest = (width * rnew) * 24 + start;
	src = (width * rold) * 24 + start;
	IO_Write(0x3ce,5); IO_Write(0x3cf,1);
	IO_Write(0x3c4,2); IO_Write(0x3c5,0xf);
	Bit8u select;
	select = 0x00;
	if(src >= 0x20000) {
		select |= 0x20;
		src -= 0x20000;
	} else if(src >= 0x10000) {
		select |= 0x10;
		src -= 0x10000;
	}
	if(dest >= 0x20000) {
		select |= 0x02;
		dest -= 0x20000;
	} else if(dest >= 0x10000) {
		select |= 0x01;
		dest -= 0x10000;
	}
	IO_Write(0x3cd, select);
	for(Bitu copy = 0 ; copy < 24 ; copy++) {
		PhysPt src_pt = src;
		PhysPt dest_pt = dest;

		for(Bitu x = 0 ; x < rowsize ; x++) {
			if(src_pt  >= 0x10000) {
				if((select & 0xf0) == 0x00) {
					select = (select & 0x0f) | 0x10;
				} else if((select & 0xf0) == 0x10) {
					select = (select & 0x0f) | 0x20;
				}
				src_pt -= 0x10000;
				src -= 0x10000;
				IO_Write(0x3cd, select);
			}
			if(dest_pt >= 0x10000) {
				if((select & 0x0f) == 0x00) {
					select = (select & 0xf0) | 0x01;
				} else if((select & 0x0f) == 0x01) {
					select = (select & 0xf0) | 0x02;
				}
				dest_pt -= 0x10000;
				dest -= 0x10000;
				IO_Write(0x3cd, select);
			}
			if(x == 0 && (cleft & 1)) {
				CopyRowMask(base, dest_pt, src_pt, 0x0f);
			} else if(x == rowsize - 1 && (cright & 1)) {
				CopyRowMask(base, dest_pt, src_pt, 0xf0);
			} else {
				mem_writeb(base + dest_pt, mem_readb(base + src_pt));
			}
			src_pt++;
			dest_pt++;
		}
		dest += width;
		src += width;
	}
	IO_Write(0x3ce,5);IO_Write(0x3cf,0);
	IO_Write(0x3cd, 0);
}

static void VGA_CopyRow(Bit8u cleft,Bit8u cright,Bit8u rold,Bit8u rnew,PhysPt base) {
	PhysPt src,dest;Bitu copy;
	Bit8u cheight = real_readb(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT);
	dest=base+8*((CurMode->twidth*rnew)*cheight+cleft);
	src=base+8*((CurMode->twidth*rold)*cheight+cleft);
	Bitu nextline=8*CurMode->twidth;
	Bitu rowsize=8*(cright-cleft);
	copy=cheight;
	for (;copy>0;copy--) {
		for (Bitu x=0;x<rowsize;x++) mem_writeb(dest+x,mem_readb(src+x));
		dest+=nextline;src+=nextline;
	}
}

static void TEXT_CopyRow(Bit8u cleft,Bit8u cright,Bit8u rold,Bit8u rnew,PhysPt base) {
	PhysPt src,dest;
	src=base+(rold*CurMode->twidth+cleft)*2;
	dest=base+(rnew*CurMode->twidth+cleft)*2;
	MEM_BlockCopy(dest,src,(cright-cleft)*2);
}

static void DCGA_FillRow(Bit8u cleft,Bit8u cright,Bit8u row,PhysPt base,Bit8u attr) {
	Bit8u cheight = real_readb(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT);
	PhysPt dest=base+((CurMode->twidth*row)*(cheight/4)+cleft);
	Bitu copy=(cright-cleft);
	Bitu nextline=CurMode->twidth;
	attr = 0x00;
	for (Bitu i=0;i<cheight/4U;i++) {
		for (Bitu x=0;x<copy;x++) {
			mem_writeb(dest+x,attr);
			mem_writeb(dest+8*1024+x,attr);
			mem_writeb(dest+8*1024*2+x,attr);
			mem_writeb(dest+8*1024*3+x,attr);
		}
		dest+=nextline;
	}
}

static void DCGA_Text_FillRow(Bit8u cleft,Bit8u cright,Bit8u row,Bit8u attr) {
	Bitu textdest = real_readw(BIOSMEM_SEG, BIOSMEM_NB_COLS) * 2 * row + cleft * 2;
	Bitu textcopy = (cright - cleft);
	Bit16u seg = GetTextSeg();
	for (Bitu x = 0 ; x < textcopy ; x++) {
		real_writeb(seg, textdest + x * 2, 0x20);
		real_writeb(seg, textdest + x * 2 + 1, attr);
	}
}

static void CGA2_FillRow(Bit8u cleft,Bit8u cright,Bit8u row,PhysPt base,Bit8u attr) {
	Bit8u cheight = real_readb(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT);
	PhysPt dest=base+((CurMode->twidth*row)*(cheight/2)+cleft);
	Bitu copy=(cright-cleft);
	Bitu nextline=CurMode->twidth;
	attr=(attr & 0x3) | ((attr & 0x3) << 2) | ((attr & 0x3) << 4) | ((attr & 0x3) << 6);
	for (Bitu i=0;i<cheight/2U;i++) {
		for (Bitu x=0;x<copy;x++) {
			mem_writeb(dest+x,attr);
			mem_writeb(dest+8*1024+x,attr);
		}
		dest+=nextline;
	}
}

static void CGA4_FillRow(Bit8u cleft,Bit8u cright,Bit8u row,PhysPt base,Bit8u attr) {
	Bit8u cheight = real_readb(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT);
	PhysPt dest=base+((CurMode->twidth*row)*(cheight/2)+cleft)*2;
	Bitu copy=(cright-cleft)*2;Bitu nextline=CurMode->twidth*2;
	attr=(attr & 0x3) | ((attr & 0x3) << 2) | ((attr & 0x3) << 4) | ((attr & 0x3) << 6);
	for (Bitu i=0;i<cheight/2U;i++) {
		for (Bitu x=0;x<copy;x++) {
			mem_writeb(dest+x,attr);
			mem_writeb(dest+8*1024+x,attr);
		}
		dest+=nextline;
	}
}

static void TANDY16_FillRow(Bit8u cleft,Bit8u cright,Bit8u row,PhysPt base,Bit8u attr) {
	Bit8u cheight = real_readb(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT);
	Bit8u banks=CurMode->twidth/10;
	PhysPt dest=base+((CurMode->twidth*row)*(cheight/banks)+cleft)*4;
	Bitu copy=(cright-cleft)*4;Bitu nextline=CurMode->twidth*4;
	attr=(attr & 0xf) | (attr & 0xf) << 4;
	for (Bit8u i=0;i<cheight/banks;i++) {
		for (Bitu x=0;x<copy;x++) {
			for (Bitu b=0;b<banks;b++) mem_writeb(dest+b*8*1024+x,attr);
		}
		dest+=nextline;
	}
}

static void EGA16_FillRow(Bit8u cleft,Bit8u cright,Bit8u row,PhysPt base,Bit8u attr) {
	/* Set Bitmask / Color / Full Set Reset */
	IO_Write(0x3ce,0x8);IO_Write(0x3cf,0xff);
	IO_Write(0x3ce,0x0);IO_Write(0x3cf,attr);
	IO_Write(0x3ce,0x1);IO_Write(0x3cf,0xf);
	/* Enable all Write planes */
	IO_Write(0x3c4,2);IO_Write(0x3c5,0xf);
	/* Write some bytes */
	Bit8u cheight = real_readb(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT);
	PhysPt dest=base+(CurMode->twidth*row)*cheight+cleft;	
	Bitu nextline=CurMode->twidth;
	Bitu copy = cheight;Bitu rowsize=(cright-cleft);
	for (;copy>0;copy--) {
		for (Bitu x=0;x<rowsize;x++) mem_writeb(dest+x,0xff);
		dest+=nextline;
	}
	IO_Write(0x3cf,0);
}

static void VGA_FillRow(Bit8u cleft,Bit8u cright,Bit8u row,PhysPt base,Bit8u attr) {
	/* Write some bytes */
	Bit8u cheight = real_readb(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT);
	PhysPt dest=base+8*((CurMode->twidth*row)*cheight+cleft);
	Bitu nextline=8*CurMode->twidth;
	Bitu copy = cheight;Bitu rowsize=8*(cright-cleft);
	for (;copy>0;copy--) {
		for (Bitu x=0;x<rowsize;x++) mem_writeb(dest+x,attr);
		dest+=nextline;
	}
}

static void TEXT_FillRow(Bit8u cleft,Bit8u cright,Bit8u row,PhysPt base,Bit8u attr) {
	/* Do some filing */
	PhysPt dest;
	dest=base+(row*CurMode->twidth+cleft)*2;
	Bit16u fill=(attr<<8)+' ';
	for (Bit8u x=0;x<(cright-cleft);x++) {
		mem_writew(dest,fill);
		dest+=2;
	}
}


void INT10_ScrollWindow(Bit8u rul,Bit8u cul,Bit8u rlr,Bit8u clr,Bit8s nlines,Bit8u attr,Bit8u page) {
/* Do some range checking */
	if(IS_J3_ARCH && J3_IsJapanese()) {
		J3_OffCursor();
	} else if((IS_J3_ARCH || IS_DOSV) && DOSV_CheckJapaneseVideoMode()) {
		DOSV_OffCursor();
	}
	if (CurMode->type!=M_TEXT) page=0xff;
	BIOS_NCOLS;BIOS_NROWS;
	if(rul>rlr) return;
	if(cul>clr) return;
	if(rlr>=nrows) rlr=(Bit8u)nrows-1;
	if(clr>=ncols) clr=(Bit8u)ncols-1;
	clr++;

	/* Get the correct page: current start address for current page (0xFF),
	   otherwise calculate from page number and page size */
	PhysPt base=CurMode->pstart;
	if (page==0xff) base+=real_readw(BIOSMEM_SEG,BIOSMEM_CURRENT_START);
	else base+=page*real_readw(BIOSMEM_SEG,BIOSMEM_PAGE_SIZE);
	
	if (GCC_UNLIKELY(machine==MCH_PCJR)) {
		if (real_readb(BIOSMEM_SEG, BIOSMEM_CURRENT_MODE) >= 9) {
			// PCJr cannot handle these modes at 0xb800
			// See INT10_PutPixel M_TANDY16
			Bitu cpupage =
				(real_readb(BIOSMEM_SEG, BIOSMEM_CRTCPU_PAGE) >> 3) & 0x7;

			base = cpupage << 14;
			if (page!=0xff)
				base += page*real_readw(BIOSMEM_SEG,BIOSMEM_PAGE_SIZE);
		}
	}

	/* See how much lines need to be copied */
	Bit8u start,end;Bits next;
	/* Copy some lines */
	if (nlines>0) {
		start=rlr-nlines+1;
		end=rul;
		next=-1;
	} else if (nlines<0) {
		start=rul-nlines-1;
		end=rlr;
		next=1;
	} else {
		nlines=rlr-rul+1;
		goto filling;
	}
	while (start!=end) {
		start+=next;
		switch (CurMode->type) {
		case M_TEXT:
			TEXT_CopyRow(cul,clr,start,start+nlines,base);break;
		case M_DCGA:
			DCGA_CopyRow(cul,clr,start,start+nlines,base);
			DCGA_Text_CopyRow(cul,clr,start,start+nlines);
			break;
		case M_CGA2:
			CGA2_CopyRow(cul,clr,start,start+nlines,base);
			break;
		case M_CGA4:
			CGA4_CopyRow(cul,clr,start,start+nlines,base);break;
		case M_TANDY16:
			TANDY16_CopyRow(cul,clr,start,start+nlines,base);break;
		case M_EGA:		
			EGA16_CopyRow(cul,clr,start,start+nlines,base);
			if(DOSV_CheckJapaneseVideoMode()) {
				DCGA_Text_CopyRow(cul,clr,start,start+nlines);
			}
			break;
		case M_VGA:		
			VGA_CopyRow(cul,clr,start,start+nlines,base);break;
		case M_LIN4:
			if(DOSV_CheckJapaneseVideoMode()) {
				if(real_readb(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT) == 24) {
					EGA16_CopyRow_24(cul,clr,start,start+nlines,base);
				} else {
					EGA16_CopyRow(cul,clr,start,start+nlines,base);
				}
				DCGA_Text_CopyRow(cul,clr,start,start+nlines);
				break;
			}
			if ((machine==MCH_VGA) && (svgaCard==SVGA_TsengET4K) &&
					(CurMode->swidth<=800)) {
				// the ET4000 BIOS supports text output in 800x600 SVGA
				EGA16_CopyRow(cul,clr,start,start+nlines,base);break;
			}
			// fall-through
		default:
			LOG(LOG_INT10,LOG_ERROR)("Unhandled mode %d for scroll",CurMode->type);
		}	
	} 
	/* Fill some lines */
filling:
	if (nlines>0) {
		start=rul;
	} else {
		nlines=-nlines;
		start=rlr-nlines+1;
	}
	for (;nlines>0;nlines--) {
		switch (CurMode->type) {
		case M_TEXT:
			TEXT_FillRow(cul,clr,start,base,attr);break;
		case M_DCGA:
			DCGA_FillRow(cul,clr,start,base,attr);
			DCGA_Text_FillRow(cul,clr,start,attr);
			break;
		case M_CGA2:
			CGA2_FillRow(cul,clr,start,base,attr);
			break;
		case M_CGA4:
			CGA4_FillRow(cul,clr,start,base,attr);break;
		case M_TANDY16:		
			TANDY16_FillRow(cul,clr,start,base,attr);break;
		case M_EGA:		
			if(DOSV_CheckJapaneseVideoMode()) {
				DCGA_Text_FillRow(cul,clr,start,attr);
				WriteCharTopView(real_readw(BIOSMEM_SEG, BIOSMEM_NB_COLS) * 2 * start + cul * 2, clr - cul);
			} else {
				EGA16_FillRow(cul,clr,start,base,attr);
			}
			break;
		case M_VGA:		
			VGA_FillRow(cul,clr,start,base,attr);break;
		case M_LIN4:
			if(DOSV_CheckJapaneseVideoMode()) {
				DCGA_Text_FillRow(cul,clr,start,attr);
				WriteCharTopView(real_readw(BIOSMEM_SEG, BIOSMEM_NB_COLS) * 2 * start + cul * 2, clr - cul);
				break;
			}
			if ((machine==MCH_VGA) && (svgaCard==SVGA_TsengET4K) &&
					(CurMode->swidth<=800)) {
				EGA16_FillRow(cul,clr,start,base,attr);break;
			}
			// fall-through
		default:
			LOG(LOG_INT10,LOG_ERROR)("Unhandled mode %d for scroll",CurMode->type);
		}	
		start++;
	} 
}

void INT10_SetActivePage(Bit8u page) {
	Bit16u mem_address;
	if (page>7) LOG(LOG_INT10,LOG_ERROR)("INT10_SetActivePage page %d",page);

	if (IS_EGAVGA_ARCH && (svgaCard==SVGA_S3Trio)) page &= 7;

	mem_address=page*real_readw(BIOSMEM_SEG,BIOSMEM_PAGE_SIZE);
	/* Write the new page start */
	real_writew(BIOSMEM_SEG,BIOSMEM_CURRENT_START,mem_address);
	if (IS_EGAVGA_ARCH || IS_J3_ARCH) {
		if (CurMode->mode<8) mem_address>>=1;
		// rare alternative: if (CurMode->type==M_TEXT)  mem_address>>=1;
	} else {
		mem_address>>=1;
	}
	/* Write the new start address in vgahardware */
	Bit16u base=real_readw(BIOSMEM_SEG,BIOSMEM_CRTC_ADDRESS);
	IO_Write(base,0x0c);
	IO_Write(base+1,(Bit8u)(mem_address>>8));
	IO_Write(base,0x0d);
	IO_Write(base+1,(Bit8u)mem_address);

	// And change the BIOS page
	real_writeb(BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE,page);
	Bit8u cur_row=CURSOR_POS_ROW(page);
	Bit8u cur_col=CURSOR_POS_COL(page);
	// Display the cursor, now the page is active
	INT10_SetCursorPos(cur_row,cur_col,page);
}

#define _pushregs \
    Bit16u tmp_ax = reg_ax, tmp_bx = reg_bx, tmp_cx = reg_cx, tmp_dx = reg_dx;
#define _popregs \
    reg_ax = tmp_ax, reg_bx = tmp_bx, reg_cx = tmp_cx, reg_dx = tmp_dx;

bool INT10_isTextMode() {
	bool result;
	if (CurMode->type == M_TEXT)
		return true;

	if(IS_J3_ARCH) {
		return true;
	}

	_pushregs;
	Bit16u tmp_di = reg_di, tmp_es = SegValue(es);
	SegSet16(es, 0); reg_di = 0;
	reg_ah = 0xfe;
	CALLBACK_RunRealInt(0x10);
	result = !(SegValue(es) && reg_di);
	SegSet16(es, tmp_es); reg_di = tmp_di;
	_popregs;
	return result;
}

void INT10_SetCursorShape_viaRealInt(Bit8u first, Bit8u last) {
	_pushregs;
	reg_ah = 0x01;
	reg_ch = first;
	reg_cl = last;
	CALLBACK_RunRealInt(0x10);
	_popregs;
}

void INT10_SetCursorPos_viaRealInt(Bit8u row, Bit8u col, Bit8u page) {
	_pushregs;
	reg_ah = 0x02;
	if (page == 0xFF) page = real_readb(BIOSMEM_SEG, BIOSMEM_CURRENT_PAGE);
	reg_bh = page;
	reg_dl = col;
	reg_dh = row;
	CALLBACK_RunRealInt(0x10);
	_popregs;
}

void INT10_ScrollWindow_viaRealInt(Bit8u rul, Bit8u cul, Bit8u rlr, Bit8u clr, Bit8s nlines, Bit8u attr, Bit8u page) {
	BIOS_NCOLS;
	BIOS_NROWS;

	_pushregs;

	if (nrows == 256 || nrows == 1) nrows = 25;
	if (nlines > 0) {
		reg_ah = 0x07;
		reg_al = (Bit8u)nlines;
	}
	else {
		reg_ah = 0x06;
		reg_al = (Bit8u)(-nlines);
	}
	/* only works with active page */
	/* if(page==0xFF) page=real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE); */

	if (clr >= ncols) clr = (Bit8u)(ncols - 1);
	if (rlr >= nrows) rlr = nrows - 1;

	reg_bh = attr;
	reg_cl = cul;
	reg_ch = rul;
	reg_dl = clr;
	reg_dh = rlr;
	CALLBACK_RunRealInt(0x10);

	_popregs;
}


void INT10_TeletypeOutputAttr_viaRealInt(Bit8u chr, Bit8u attr, bool useattr) {
	BIOS_NCOLS; BIOS_NROWS;
	Bit8u page = real_readb(BIOSMEM_SEG, BIOSMEM_CURRENT_PAGE);
	Bit8u cur_row = CURSOR_POS_ROW(page);
	Bit8u cur_col = CURSOR_POS_COL(page);
	switch (chr) {
	case 7:
		//TODO BEEP
		break;
	case 8:
		if (cur_col>0) cur_col--;
		break;
	case '\r':
		cur_col = 0;
		break;
	case '\n':
		//		cur_col=0; //Seems to break an old chess game
		cur_row++;
		break;
	case '\t':
		do {
			INT10_TeletypeOutputAttr_viaRealInt(' ', attr, useattr);
			cur_row = CURSOR_POS_ROW(page);
			cur_col = CURSOR_POS_COL(page);
		} while (cur_col % 8);
		break;
	default:
		/* Draw the actual Character */
		INT10_WriteChar_viaRealInt(chr, attr, page, 1, useattr);
		cur_col++;
	}
	if (cur_col >= ncols) {
		cur_col = 0;
		cur_row++;
	}
	// Do we need to scroll ?
	if (cur_row >= nrows) {
		//Fill with black on non-text modes and with 0x7 on textmode
		Bit8u fill = INT10_isTextMode() ? 0x7 : 0;
		INT10_ScrollWindow_viaRealInt(0, 0, nrows - 1, ncols - 1, -1, fill, page);
		cur_row--;
	}
	// Set the cursor for the page
	INT10_SetCursorPos_viaRealInt(cur_row, cur_col, page);
}


void INT10_TeletypeOutput_viaRealInt(Bit8u chr, Bit8u attr) {
	INT10_TeletypeOutputAttr_viaRealInt(chr, attr, !INT10_isTextMode());
}


void INT10_WriteChar_viaRealInt(Bit8u chr, Bit8u attr, Bit8u page, Bit16u count, bool showattr) {
	_pushregs;
	if (page == 0xFF) page = real_readb(BIOSMEM_SEG, BIOSMEM_CURRENT_PAGE);
	reg_ah = showattr ? 0x09 : 0x0a;
	reg_al = chr;
	reg_bh = page;
	reg_bl = attr;
	reg_cx = count;
	CALLBACK_RunRealInt(0x10);
	_popregs;
}

void INT10_SetCursorShape(Bit8u first,Bit8u last) {
	if(IS_J3_ARCH && J3_IsJapanese()) {
		if(first == 0x06 && last == 0x07) {
			first = 0x0f;
			last = 0x0f;
		}
		if(last > 0x0f) {
			last = 0x0f;
		}
	}
	real_writew(BIOSMEM_SEG,BIOSMEM_CURSOR_TYPE,last|(first<<8));
	if (machine==MCH_CGA || (IS_J3_ARCH && J3_IsJapanese())) goto dowrite;
	if (IS_TANDY_ARCH) goto dowrite;
	/* Skip CGA cursor emulation if EGA/VGA system is active */
	if (!(real_readb(BIOSMEM_SEG,BIOSMEM_VIDEO_CTL) & 0x8)) {
		/* Check for CGA type 01, invisible */
		if ((first & 0x60) == 0x20) {
			first=0x1e;
			last=0x00;
			goto dowrite;
		}
		/* Check if we need to convert CGA Bios cursor values */
		if (!(real_readb(BIOSMEM_SEG,BIOSMEM_VIDEO_CTL) & 0x1)) { // set by int10 fun12 sub34
//			if (CurMode->mode>0x3) goto dowrite;	//Only mode 0-3 are text modes on cga
			if ((first & 0xe0) || (last & 0xe0)) goto dowrite;
			Bit8u cheight=real_readb(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT)-1;
			/* Creative routine i based of the original ibmvga bios */

			if (last<first) {
				if (!last) goto dowrite;
				first=last;
				last=cheight;
			/* Test if this might be a cga style cursor set, if not don't do anything */
			} else if (((first | last)>=cheight) || !(last==(cheight-1)) || !(first==cheight) ) {
				if (last<=3) goto dowrite;
				if (first+2<last) {
					if (first>2) {
						first=(cheight+1)/2;
						last=cheight;
					} else {
						last=cheight;
					}
				} else {
					first=(first-last)+cheight;
					last=cheight;

					if (cheight>0xc) { // vgatest sets 15 15 2x where only one should be decremented to 14 14
						first--;     // implementing int10 fun12 sub34 fixed this.
						last--;
					}
				}
			}

		}
	}
dowrite:
	Bit16u base=real_readw(BIOSMEM_SEG,BIOSMEM_CRTC_ADDRESS);
	IO_Write(base,0xa);IO_Write(base+1,first);
	IO_Write(base,0xb);IO_Write(base+1,last);
}

void INT10_SetCursorPos(Bit8u row,Bit8u col,Bit8u page) {
	Bit16u address;

	if(IS_J3_ARCH && J3_IsJapanese()) {
		J3_OffCursor();
	} else if((IS_J3_ARCH || IS_DOSV) && DOSV_CheckJapaneseVideoMode()) {
		DOSV_OffCursor();
	}
	if (page>7) LOG(LOG_INT10,LOG_ERROR)("INT10_SetCursorPos page %d",page);
	// Bios cursor pos
	real_writeb(BIOSMEM_SEG,BIOSMEM_CURSOR_POS+page*2,col);
	real_writeb(BIOSMEM_SEG,BIOSMEM_CURSOR_POS+page*2+1,row);
	// Set the hardware cursor
	Bit8u current=real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE);
	if(page==current) {
		// Get the dimensions
		BIOS_NCOLS;
		// Calculate the address knowing nbcols nbrows and page num
		// NOTE: BIOSMEM_CURRENT_START counts in colour/flag pairs
		address=(ncols*row)+col+real_readw(BIOSMEM_SEG,BIOSMEM_CURRENT_START)/2;
		// CRTC regs 0x0e and 0x0f
		Bit16u base=real_readw(BIOSMEM_SEG,BIOSMEM_CRTC_ADDRESS);
		IO_Write(base,0x0e);
		IO_Write(base+1,(Bit8u)(address>>8));
		IO_Write(base,0x0f);
		IO_Write(base+1,(Bit8u)address);
	}
}

void ReadCharAttr(Bit16u col,Bit16u row,Bit8u page,Bit16u * result) {
	/* Externally used by the mouse routine */
	PhysPt fontdata;
	Bitu x,y,pos = row*real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS)+col;
	Bit8u cheight = real_readb(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT);
	bool split_chr = false;
	switch (CurMode->type) {
	case M_TEXT:
		{	
			// Compute the address  
			Bit16u address=page*real_readw(BIOSMEM_SEG,BIOSMEM_PAGE_SIZE);
			address+=pos*2;
			// read the char 
			PhysPt where = CurMode->pstart+address;
			*result=mem_readw(where);
		}
		return;
	case M_CGA4:
	case M_CGA2:
	case M_DCGA:
	case M_TANDY16:
		split_chr = true;
		switch (machine) {
		case MCH_CGA:
		case MCH_HERC:
			fontdata=PhysMake(0xf000,0xfa6e);
			break;
		case TANDY_ARCH_CASE:
			fontdata=Real2Phys(RealGetVec(0x44));
			break;
		case MCH_DCGA:
			if(J3_IsJapanese()) {
				*result = real_readw(GetTextSeg(), (row * real_readw(BIOSMEM_SEG, BIOSMEM_NB_COLS) + col) * 2);
				return;
			}
		default:
			fontdata=Real2Phys(RealGetVec(0x43));
			break;
		}
		break;
	default:
		if (isJEGAEnabled()) {
			if (real_readw(BIOSMEM_AX_SEG, BIOSMEM_AX_VTRAM_SEGADDR) != 0) {
				ReadVTRAMChar(col, row, result);
				return;
			}
		} else if(DOSV_CheckJapaneseVideoMode()) {
			*result = real_readw(GetTextSeg(), (row * real_readw(BIOSMEM_SEG, BIOSMEM_NB_COLS) + col) * 2);
			return;
		}
		fontdata=Real2Phys(RealGetVec(0x43));
		break;
	}

	x=(pos%CurMode->twidth)*8;
	y=(pos/CurMode->twidth)*cheight;

	for (Bit16u chr=0;chr<256;chr++) {

		if (chr==128 && split_chr) fontdata=Real2Phys(RealGetVec(0x1f));

		bool error=false;
		Bit16u ty=(Bit16u)y;
		for (Bit8u h=0;h<cheight;h++) {
			Bit8u bitsel=128;
			Bit8u bitline=mem_readb(fontdata++);
			Bit8u res=0;
			Bit8u vidline=0;
			Bit16u tx=(Bit16u)x;
			while (bitsel) {
				//Construct bitline in memory
				INT10_GetPixel(tx,ty,page,&res);
				if(res) vidline|=bitsel;
				tx++;
				bitsel>>=1;
			}
			ty++;
			if(bitline != vidline){
				/* It's not character 'chr', move on to the next */
				fontdata+=(cheight-h-1);
				error = true;
				break;
			}
		}
		if(!error){
			/* We found it */
			*result = chr;
			return;
		}
	}
	LOG(LOG_INT10,LOG_ERROR)("ReadChar didn't find character");
	*result = 0;
}
void INT10_ReadCharAttr(Bit16u * result,Bit8u page) {
	if(page==0xFF) page=real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE);
	Bit8u cur_row=CURSOR_POS_ROW(page);
	Bit8u cur_col=CURSOR_POS_COL(page);
	ReadCharAttr(cur_col,cur_row,page,result);
}

/* Draw DBCS char in graphics mode*/
void WriteCharJ(Bit16u col, Bit16u row, Bit8u page, Bit8u chr, Bit8u attr, bool useattr)
{
	Bitu x, y, pos = row*real_readw(BIOSMEM_SEG, BIOSMEM_NB_COLS) + col;
	Bit8u back, cheight = real_readb(BIOSMEM_SEG, BIOSMEM_CHAR_HEIGHT);
	Bit16u sjischr = prevchr;
	sjischr <<= 8;
	sjischr |= chr;

	if (GCC_UNLIKELY(!useattr)) { //Set attribute(color) to a sensible value
		static bool warned_use = false;
		if (GCC_UNLIKELY(!warned_use)) {
			LOG(LOG_INT10, LOG_ERROR)("writechar used without attribute in non-textmode %c %X", chr, chr);
			warned_use = true;
		}
		attr = 0xf;
	}

	//Attribute behavior of mode 6; mode 11 does something similar but
	//it is in INT 10h handler because it only applies to function 09h
	if (CurMode->mode == 0x06) attr = (attr & 0x80) | 1;

	switch (CurMode->type) {
	case M_VGA:
	case M_EGA:
		/* enable all planes for EGA modes (Ultima 1 colour bug) */
		/* might be put into INT10_PutPixel but different vga bios
		implementations have different opinions about this */
		IO_Write(0x3c4, 0x2); IO_Write(0x3c5, 0xf);
		// fall-through
	default:
		back = attr & 0x80;
		break;
	}

	x = (pos%CurMode->twidth - 1) * 8;//move right 1 column.
	y = (pos / CurMode->twidth)*cheight;

	GetDbcsFont(sjischr);

	Bit16u ty = (Bit16u)y;
	for (Bit8u h = 0; h<16 ; h++) {
		Bit16u bitsel = 0x8000;
		Bit16u bitline = jfont_dbcs_16[sjischr * 32 + h * 2];
		bitline <<= 8;
		bitline |= jfont_dbcs_16[sjischr * 32 + h * 2 + 1];
		Bit16u tx = (Bit16u)x;
		while (bitsel) {
			INT10_PutPixel(tx, ty, page, (bitline&bitsel) ? attr : back);
			tx++;
			bitsel >>= 1;
		}
		ty++;
	}

}

/* Read char code and attribute from Virtual Text RAM (for AX)*/
void ReadVTRAMChar(Bit16u col, Bit16u row, Bit16u * result) {
	Bitu addr = real_readw(BIOSMEM_AX_SEG, BIOSMEM_AX_VTRAM_SEGADDR);
	addr <<= 4;
	addr += 2 * (80 * row + col);
	*result = mem_readw(addr);
}

/* Write char code and attribute into Virtual Text RAM (for AX)*/
void SetVTRAMChar(Bit16u col, Bit16u row, Bit8u chr, Bit8u attr)
{
	Bitu addr = real_readw(BIOSMEM_AX_SEG, BIOSMEM_AX_VTRAM_SEGADDR);
	addr <<= 4;
	addr += 2 * (80 * row + col);
	mem_writeb(addr,chr);
	mem_writeb(addr+1, attr);
}

static Bit16u font_net_data[2][16] = {
	{ 0x2449, 0x0000, 0x0000, 0x2449, 0x0000, 0x0000, 0x2449, 0x0000, 0x0000, 0x2449, 0x0000, 0x0000, 0x2449, 0x0000, 0x0000, 0x2449 },
	{ 0x0000, 0x4444, 0x0000, 0x0000, 0x0000, 0x4444, 0x0000, 0x0000, 0x0000, 0x4444, 0x0000, 0x0000, 0x0000, 0x4444, 0x0000, 0x0000 }
};

void WriteCharJ31Sbcs(Bit16u col, Bit16u row, Bit8u chr, Bit8u attr)
{
	Bitu x, y, off, net, pos;
	Bit8u data;
	Bit8u *font;
	RealPt fontdata;

	net = ((attr & 0xf0) == 0xe0) ? 1 : 0;
	pos = 0;
	if(J3_IsJapanese() && chr >= 0x20) {
		font = GetSbcsFont(chr);
	} else {
		fontdata = RealGetVec(0x43);
		fontdata = RealMake(RealSeg(fontdata), RealOff(fontdata) + chr * 16);
	}
	x = col;
	y = row * 16;
	off = (y >> 2) * 80 + 8 * 1024 * (y & 3) + x;
	for(Bit8u h = 0 ; h < 16 ; h++) {
		if(J3_IsJapanese() && chr >= 0x20) {
			data = *font++;
		} else {
			data = mem_readb(Real2Phys(fontdata++));
		}
		if((attr & 0x07) == 0x00) {
			data ^= 0xff;
		}
		if(attr & 0x80) {
			if(attr & 0x70) {
				data |= (font_net_data[net][pos++] & 0xff);
			} else {
				data ^= real_readb(GRAPH_J3_SEG, off);
			}
		}
		if(h == 15 && (attr & 0x08) == 0x08) {
			data = 0xff;
		}
		real_writeb(GRAPH_J3_SEG, off, data);
		off += 0x2000;
		if(off >= 0x8000) {
			off -= 0x8000;
			off += 80;
		}
	}
}

void WriteCharJ31Dbcs(Bit16u col, Bit16u row, Bit16u chr, Bit8u attr)
{
	Bitu x, y, off, net, pos;
	Bit16u data;
	Bit16u *font;

	net = ((attr & 0xf0) == 0xe0) ? 1 : 0;
	pos = 0;
	font = (Bit16u *)GetDbcsFont(chr);
	x = col;
	y = row * 16;
	off = (y >> 2) * 80 + 8 * 1024 * (y & 3) + x;
	for(Bit8u h = 0 ; h < 16 ; h++) {
		data = *font++;
		if((attr & 0x07) == 0x00) {
			data ^= 0xffff;
		}
		if(attr & 0x80) {
			if(attr & 0x70) {
				data |= font_net_data[net][pos++];
			} else {
				data ^= real_readw(GRAPH_J3_SEG, off);
			}
		}
		if(h == 15 && (attr & 0x08) == 0x08) {
			data = 0xffff;
		}
		real_writew(GRAPH_J3_SEG, off, data);
		off += 0x2000;
		if(off >= 0x8000) {
			off -= 0x8000;
			off += 80;
		}
	}
}

static bool CheckJapaneseGraphicsMode(Bit8u attr)
{
	if(real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_MODE) == 0x72) {
		if(attr & 0x80) {
			return true;
		}
	}
	return false;
}

void WriteCharDOSVSbcs24(Bit16u col, Bit16u row, Bit8u chr, Bit8u attr)
{
	Bit8u back, data, select;
	Bit8u *font;
	Bitu off;
	Bitu width = (real_readw(BIOSMEM_SEG, BIOSMEM_NB_COLS) == 85) ? 128 : 160;
	volatile Bit8u dummy;

	font = GetSbcs24Font(chr);

	back = attr >> 4;
	attr &= 0x0f;

	off = row * width * 24 + (col * 12) / 8;
	if(off >= 0x20000) {
		select = 0x22;
		off -= 0x20000;
	} else if(off >= 0x10000) {
		select = 0x11;
		off -= 0x10000;
	} else {
		select = 0x00;
	}
	IO_Write(0x3cd, select);

	IO_Write(0x3ce, 5); IO_Write(0x3cf, 0);
	IO_Write(0x3ce, 1); IO_Write(0x3cf, 0xf);

	if(col & 1) {
		for(Bit8u y = 0 ; y < 24 ; y++) {
			data = *font++;

			IO_Write(0x3ce, 8); IO_Write(0x3cf, (data >> 4));
			IO_Write(0x3ce, 0); IO_Write(0x3cf, attr);
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);

			IO_Write(0x3ce, 8); IO_Write(0x3cf, (data >> 4) ^ 0x0f);
			IO_Write(0x3ce, 0); IO_Write(0x3cf, back);
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);

			off++;
			if(off >= 0x10000) {
				if(select == 0x00) {
					select = 0x11;
				} else if(select == 0x11) {
					select = 0x22;
				}
				IO_Write(0x3cd, select);
				off -= 0x10000;
			}
			data <<= 4;
			data |= *font++ >> 4;
			IO_Write(0x3ce, 8); IO_Write(0x3cf, data);
			IO_Write(0x3ce, 0); IO_Write(0x3cf, attr);
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);

			IO_Write(0x3ce, 8); IO_Write(0x3cf, data ^ 0xff);
			IO_Write(0x3ce, 0); IO_Write(0x3cf, back);
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);

			off += width - 1;
			if(off >= 0x10000) {
				if(select == 0x00) {
					select = 0x11;
				} else if(select == 0x11) {
					select = 0x22;
				}
				IO_Write(0x3cd, select);
				off -= 0x10000;
			}
		}
	} else {
		for(Bit8u y = 0 ; y < 24 ; y++) {
			data = *font++;

			IO_Write(0x3ce, 8); IO_Write(0x3cf, data);
			IO_Write(0x3ce, 0); IO_Write(0x3cf, attr);
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);

			IO_Write(0x3ce, 8); IO_Write(0x3cf, data ^ 0xff);
			IO_Write(0x3ce, 0); IO_Write(0x3cf, back);
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);

			off++;
			if(off >= 0x10000) {
				if(select == 0x00) {
					select = 0x11;
				} else if(select == 0x11) {
					select = 0x22;
				}
				IO_Write(0x3cd, select);
				off -= 0x10000;
			}
			data = *font++;
			IO_Write(0x3ce, 8); IO_Write(0x3cf, data & 0xf0);
			IO_Write(0x3ce, 0); IO_Write(0x3cf, attr);
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);

			IO_Write(0x3ce, 8); IO_Write(0x3cf, (data & 0xf0) ^ 0xf0);
			IO_Write(0x3ce, 0); IO_Write(0x3cf, back);
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);

			off += width - 1;
			if(off >= 0x10000) {
				if(select == 0x00) {
					select = 0x11;
				} else if(select == 0x11) {
					select = 0x22;
				}
				IO_Write(0x3cd, select);
				off -= 0x10000;
			}
		}
	}
	IO_Write(0x3ce, 8); IO_Write(0x3cf, 0xff);
	IO_Write(0x3ce, 1); IO_Write(0x3cf, 0);
	IO_Write(0x3cd, 0x00);
}

void WriteCharDOSVDbcs24(Bit16u col, Bit16u row, Bit16u chr, Bit8u attr)
{
	Bit8u back, select;
	Bit8u *font;
	Bitu off;
	Bitu width = (real_readw(BIOSMEM_SEG, BIOSMEM_NB_COLS) == 85) ? 128 : 160;
	volatile Bit8u dummy;

	font = GetDbcs24Font(chr);

	back = attr >> 4;
	attr &= 0x0f;

	off = row * width * 24 + (col * 12) / 8;
	if(off >= 0x20000) {
		select = 0x22;
		off -= 0x20000;
	} else if(off >= 0x10000) {
		select = 0x11;
		off -= 0x10000;
	} else {
		select = 0x00;
	}
	IO_Write(0x3cd, select);

	IO_Write(0x3ce, 5); IO_Write(0x3cf, 0);
	IO_Write(0x3ce, 1); IO_Write(0x3cf, 0xf);

	if(col & 1) {
		Bit8u data[4];
		for(Bit8u y = 0 ; y < 24 ; y++) {
			data[0] = *font >> 4;
			data[1] = (*font << 4) | (*(font + 1) >> 4);
			data[2] = (*(font + 1) << 4) | (*(font + 2) >> 4);
			data[3] = *(font + 2) << 4;
			font += 3;

			IO_Write(0x3ce, 8); IO_Write(0x3cf, data[0]);
			IO_Write(0x3ce, 0); IO_Write(0x3cf, attr);
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);

			IO_Write(0x3ce, 8); IO_Write(0x3cf, data[0] ^ 0x0f);
			IO_Write(0x3ce, 0); IO_Write(0x3cf, back);
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);

			off++;
			if(off >= 0x10000) {
				if(select == 0x00) {
					select = 0x11;
				} else if(select == 0x11) {
					select = 0x22;
				}
				IO_Write(0x3cd, select);
				off -= 0x10000;
			}
			IO_Write(0x3ce, 8); IO_Write(0x3cf, data[1]);
			IO_Write(0x3ce, 0); IO_Write(0x3cf, attr);
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);

			IO_Write(0x3ce, 8); IO_Write(0x3cf, data[1] ^ 0xff);
			IO_Write(0x3ce, 0); IO_Write(0x3cf, back);
			dummy = real_readb(0xa000,  off);
			real_writeb(0xa000, off, 0xff);

			off++;
			if(off >= 0x10000) {
				if(select == 0x00) {
					select = 0x11;
				} else if(select == 0x11) {
					select = 0x22;
				}
				IO_Write(0x3cd, select);
				off -= 0x10000;
			}
			IO_Write(0x3ce, 8); IO_Write(0x3cf, data[2]);
			IO_Write(0x3ce, 0); IO_Write(0x3cf, attr);
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);

			IO_Write(0x3ce, 8); IO_Write(0x3cf, data[2] ^ 0xff);
			IO_Write(0x3ce, 0); IO_Write(0x3cf, back);
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);

			off++;
			if(off >= 0x10000) {
				if(select == 0x00) {
					select = 0x11;
				} else if(select == 0x11) {
					select = 0x22;
				}
				IO_Write(0x3cd, select);
				off -= 0x10000;
			}
			IO_Write(0x3ce, 8); IO_Write(0x3cf, data[3]);
			IO_Write(0x3ce, 0); IO_Write(0x3cf, attr);
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);

			IO_Write(0x3ce, 8); IO_Write(0x3cf, data[3] ^ 0xf0);
			IO_Write(0x3ce, 0); IO_Write(0x3cf, back);
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);

			off += width - 3;
			if(off >= 0x10000) {
				if(select == 0x00) {
					select = 0x11;
				} else if(select == 0x11) {
					select = 0x22;
				}
				IO_Write(0x3cd, select);
				off -= 0x10000;
			}
		}
	} else {
		Bit8u data;
		for(Bit8u y = 0 ; y < 24 ; y++) {
			data = *font++;

			IO_Write(0x3ce, 8); IO_Write(0x3cf, data);
			IO_Write(0x3ce, 0); IO_Write(0x3cf, attr);
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);

			IO_Write(0x3ce, 8); IO_Write(0x3cf, data ^ 0xff);
			IO_Write(0x3ce, 0); IO_Write(0x3cf, back);
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);

			data = *font++;
			off++;
			if(off >= 0x10000) {
				if(select == 0x00) {
					select = 0x11;
				} else if(select == 0x11) {
					select = 0x22;
				}
				IO_Write(0x3cd, select);
				off -= 0x10000;
			}
			IO_Write(0x3ce, 8); IO_Write(0x3cf, data);
			IO_Write(0x3ce, 0); IO_Write(0x3cf, attr);
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);

			IO_Write(0x3ce, 8); IO_Write(0x3cf, data ^ 0xff);
			IO_Write(0x3ce, 0); IO_Write(0x3cf, back);
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);

			off++;
			if(off >= 0x10000) {
				if(select == 0x00) {
					select = 0x11;
				} else if(select == 0x11) {
					select = 0x22;
				}
				IO_Write(0x3cd, select);
				off -= 0x10000;
			}
			data = *font++;
			IO_Write(0x3ce, 8); IO_Write(0x3cf, data);
			IO_Write(0x3ce, 0); IO_Write(0x3cf, attr);
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);

			IO_Write(0x3ce, 8); IO_Write(0x3cf, data ^ 0xff);
			IO_Write(0x3ce, 0); IO_Write(0x3cf, back);
			dummy = real_readb(0xa000, off);
			real_writeb(0xa000, off, 0xff);

			off += width - 2;
			if(off >= 0x10000) {
				if(select == 0x00) {
					select = 0x11;
				} else if(select == 0x11) {
					select = 0x22;
				}
				IO_Write(0x3cd, select);
				off -= 0x10000;
			}
		}
	}
	IO_Write(0x3ce, 8); IO_Write(0x3cf, 0xff);
	IO_Write(0x3ce, 1); IO_Write(0x3cf, 0);
	IO_Write(0x3cd, 0x00);
}

void WriteCharDOSVSbcs(Bit16u col, Bit16u row, Bit8u chr, Bit8u attr)
{
	Bitu off;
	Bit8u data, select;
	Bit8u *font;

	if(real_readb(BIOSMEM_SEG, BIOSMEM_CHAR_HEIGHT) == 24) {
		WriteCharDOSVSbcs24(col, row, chr, attr);
		return;
	}

	if(CheckJapaneseGraphicsMode(attr)) {
		IO_Write(0x3ce, 0x05); IO_Write(0x3cf, 0x03);
		IO_Write(0x3ce, 0x00); IO_Write(0x3cf, attr & 0x0f);
		IO_Write(0x3ce, 0x03); IO_Write(0x3cf, 0x18);

		volatile Bit8u dummy;
		Bitu width = real_readw(BIOSMEM_SEG, BIOSMEM_NB_COLS);
		Bit8u height = real_readb(BIOSMEM_SEG, BIOSMEM_CHAR_HEIGHT);
		if(height == 16) {
			font = GetSbcsFont(chr);
		} else {
			font = GetSbcs19Font(chr);
		}
		off = row * width * height + col;
		if(svgaCard == SVGA_TsengET4K) {
			if(off >= 0x20000) {
				select = 0x22;
				off -= 0x20000;
			} else if(off >= 0x10000) {
				select = 0x11;
				off -= 0x10000;
			} else {
				select = 0x00;
			}
			IO_Write(0x3cd, select);
		}
		for(Bit8u h = 0 ; h < height ; h++) {
			dummy = real_readb(0xa000, off);
			data = *font++;
			real_writeb(0xa000, off, data);
			off += width;
			if(off >= 0x10000) {
				if(select == 0x00) {
					select = 0x11;
				} else if(select == 0x11) {
					select = 0x22;
				}
				off -= 0x10000;
				IO_Write(0x3cd, select);
			}
		}
		IO_Write(0x3ce, 0x03); IO_Write(0x3cf, 0x00);
		return;
	}
	volatile Bit8u dummy;
	Bitu width = real_readw(BIOSMEM_SEG, BIOSMEM_NB_COLS);
	Bit8u height = real_readb(BIOSMEM_SEG, BIOSMEM_CHAR_HEIGHT);
	if(height == 16) {
		font = GetSbcsFont(chr);
	} else {
		font = GetSbcs19Font(chr);
	}
	off = row * width * height + col;
	if(svgaCard == SVGA_TsengET4K) {
		if(off >= 0x20000) {
			select = 0x22;
			off -= 0x20000;
		} else if(off >= 0x10000) {
			select = 0x11;
			off -= 0x10000;
		} else {
			select = 0x00;
		}
		IO_Write(0x3cd, select);
	}
	IO_Write(0x3ce, 0x05); IO_Write(0x3cf, 0x03);
	IO_Write(0x3ce, 0x00); IO_Write(0x3cf, attr >> 4);
	real_writeb(0xa000, off, 0xff); dummy = real_readb(0xa000, off);
	IO_Write(0x3ce, 0x00); IO_Write(0x3cf, attr & 0x0f);
	for(Bit8u h = 0 ; h < height ; h++) {
		data = *font++;
		real_writeb(0xa000, off, data);
		off += width;
		if(off >= 0x10000) {
			if(select == 0x00) {
				select = 0x11;
			} else if(select == 0x11) {
				select = 0x22;
			}
			off -= 0x10000;
			IO_Write(0x3cd, select);
		}
	}
}

void WriteCharDOSVDbcs(Bit16u col, Bit16u row, Bit16u chr, Bit8u attr)
{
	Bitu off;
	Bit16u data;
	Bit16u *font;
	Bit8u select;

	if(real_readb(BIOSMEM_SEG, BIOSMEM_CHAR_HEIGHT) == 24) {
		WriteCharDOSVDbcs24(col, row, chr, attr);
		return;
	}

	if(CheckJapaneseGraphicsMode(attr)) {
		IO_Write(0x3ce, 0x05); IO_Write(0x3cf, 0x03);
		IO_Write(0x3ce, 0x00); IO_Write(0x3cf, attr & 0x0f);
		IO_Write(0x3ce, 0x03); IO_Write(0x3cf, 0x18);

		volatile Bit16u dummy;
		Bitu width = real_readw(BIOSMEM_SEG, BIOSMEM_NB_COLS);
		Bit8u height = real_readb(BIOSMEM_SEG, BIOSMEM_CHAR_HEIGHT);
		font = (Bit16u *)GetDbcsFont(chr);
		off = row * width * height + col;
		if(svgaCard == SVGA_TsengET4K) {
			if(off >= 0x20000) {
				select = 0x22;
				off -= 0x20000;
			} else if(off >= 0x10000) {
				select = 0x11;
				off -= 0x10000;
			} else {
				select = 0x00;
			}
			IO_Write(0x3cd, select);
		}
		for(Bit8u h = 0 ; h < height ; h++) {
			dummy = real_readw(0xa000, off);
			if(height == 19 && (h == 0 || h > 16)) {
				data = 0;
			} else {
				data = *font++;
			}
			real_writew(0xa000, off, data);
			off += width;
		}
		IO_Write(0x3ce, 0x03); IO_Write(0x3cf, 0x00);
		return;
	}
	volatile Bit16u dummy;
	Bitu width = real_readw(BIOSMEM_SEG, BIOSMEM_NB_COLS);
	Bit8u height = real_readb(BIOSMEM_SEG, BIOSMEM_CHAR_HEIGHT);
	font = (Bit16u *)GetDbcsFont(chr);
	off = row * width * height + col;
	if(svgaCard == SVGA_TsengET4K) {
		if(off >= 0x20000) {
			select = 0x22;
			off -= 0x20000;
		} else if(off >= 0x10000) {
			select = 0x11;
			off -= 0x10000;
		} else {
			select = 0x00;
		}
		IO_Write(0x3cd, select);
	}
	IO_Write(0x3ce, 0x05); IO_Write(0x3cf, 0x03);
	IO_Write(0x3ce, 0x00); IO_Write(0x3cf, attr >> 4);
	real_writew(0xa000, off, 0xffff); dummy = real_readw(0xa000, off);
	IO_Write(0x3ce, 0x00); IO_Write(0x3cf, attr & 0x0f);
	for(Bit8u h = 0 ; h < height ; h++) {
		if(height == 19 && (h == 0 || h > 16)) {
			data = 0;
		} else {
			data = *font++;
		}
		real_writew(0xa000, off, data);
		off += width;
		if(off >= 0x10000) {
			if(select == 0x00) {
				select = 0x11;
			} else if(select == 0x11) {
				select = 0x22;
			}
			off -= 0x10000;
			IO_Write(0x3cd, select);
		}
	}
}

void WriteCharTopView(Bit16u off, int count)
{
	Bit16u seg = GetTextSeg();
	Bit8u code, attr;
	Bit16u col, row;
	Bit16u width = real_readw(BIOSMEM_SEG, BIOSMEM_NB_COLS);
	col = (off / 2) % width;
	row = (off / 2) / width;
	while(count > 0) {
		code = real_readb(seg, off);
		attr = real_readb(seg, off + 1);
		if(isKanji1(code)) {
			real_writeb(seg, row * width * 2 + col * 2, code);
			real_writeb(seg, row * width * 2 + col * 2 + 1, attr);
			off += 2;
			if(IS_J3_ARCH && J3_IsJapanese()) {
				WriteCharJ31Dbcs(col, row, ((Bit16u)code << 8) | real_readb(seg, off), attr);
			} else {
				WriteCharDOSVDbcs(col, row, ((Bit16u)code << 8) | real_readb(seg, off), attr);
			}
			count--;
			col++;
			if(col >= width) {
				col = 0;
				row++;
			}
			real_writeb(seg, row * width * 2 + col * 2, real_readb(seg, off));
			real_writeb(seg, row * width * 2 + col * 2 + 1, attr);
		} else {
			real_writeb(seg, row * width * 2 + col * 2, code);
			real_writeb(seg, row * width * 2 + col * 2 + 1, attr);
			if(IS_J3_ARCH && J3_IsJapanese()) {
				WriteCharJ31Sbcs(col, row, code, attr);
			} else {
				WriteCharDOSVSbcs(col, row, code, attr);
			}
		}
		col++;
		if(col >= width) {
			col = 0;
			row++;
		}
		off += 2;
		count--;
	}
}

void WriteChar(Bit16u col,Bit16u row,Bit8u page,Bit8u chr,Bit8u attr,bool useattr) {
	/* Externally used by the mouse routine */
	PhysPt fontdata;
	Bitu x,y,pos = row*real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS)+col;
	Bit8u back,cheight = real_readb(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT);
	switch (CurMode->type) {
	case M_TEXT:
		{	
			// Compute the address  
			Bit16u address=page*real_readw(BIOSMEM_SEG,BIOSMEM_PAGE_SIZE);
			address+=pos*2;
			// Write the char 
			PhysPt where = CurMode->pstart+address;
			mem_writeb(where,chr);
			if (useattr) mem_writeb(where+1,attr);
		}
		return;
	case M_DCGA:
		{
			Bit16u seg = GetTextSeg();
			J3_OffCursor();
			real_writeb(BIOSMEM_J3_SEG, BIOSMEM_J3_MODE, real_readb(BIOSMEM_J3_SEG, BIOSMEM_J3_MODE) | 0x20);
			real_writeb(seg, row * 80 * 2 + col * 2, chr);
			if(useattr) {
				real_writeb(seg, row * 80 * 2 + col * 2 + 1, attr);
			}
			if (isKanji1(chr) && prevchr == 0) {
				prevchr = chr;
			} else if (isKanji2(chr) && prevchr != 0) {
				WriteCharJ31Dbcs(col - 1, row, (prevchr << 8) | chr, attr);
				prevchr = 0;
				return;
			}
			WriteCharJ31Sbcs(col, row, chr, attr);
			real_writeb(BIOSMEM_J3_SEG, BIOSMEM_J3_MODE, real_readb(BIOSMEM_J3_SEG, BIOSMEM_J3_MODE) & 0xdf);
		}
		return;
	case M_CGA4:
	case M_CGA2:
	case M_TANDY16:
		if (chr>=128) {
			chr-=128;
			fontdata=Real2Phys(RealGetVec(0x1f));
			break;
		}
		switch (machine) {
		case MCH_CGA:
		case MCH_HERC:
			fontdata=PhysMake(0xf000,0xfa6e);
			break;
		case TANDY_ARCH_CASE:
			fontdata=Real2Phys(RealGetVec(0x44));
			break;
		default:
			fontdata=Real2Phys(RealGetVec(0x43));
			break;
		}
		break;
	default:
		if (isJEGAEnabled()) {
			if (real_readw(BIOSMEM_AX_SEG, BIOSMEM_AX_VTRAM_SEGADDR) != 0)
			SetVTRAMChar(col, row, chr, attr);
			if (isKanji1(chr) && prevchr == 0)
				prevchr = chr;
			else if (isKanji2(chr) && prevchr != 0)
			{
				WriteCharJ(col, row, page, chr, attr, useattr);
				prevchr = 0;
				return;
			}
		} else if(DOSV_CheckJapaneseVideoMode()) {
			DOSV_OffCursor();

			Bit16u seg = GetTextSeg();
			Bit16u width = real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS);
			real_writeb(seg, row * width * 2 + col * 2, chr);
			if(useattr) {
				real_writeb(seg, row * width * 2 + col * 2 + 1, attr);
			}
			if (isKanji1(chr) && prevchr == 0) {
				prevchr = chr;
			} else if (isKanji2(chr) && prevchr != 0) {
				WriteCharDOSVDbcs(col - 1, row, (prevchr << 8) | chr, attr);
				prevchr = 0;
				return;
			}
			WriteCharDOSVSbcs(col, row, chr, attr);
			return;
		}
		fontdata=Real2Phys(RealGetVec(0x43));
		break;
	}
	fontdata+=chr*cheight;

	if(GCC_UNLIKELY(!useattr)) { //Set attribute(color) to a sensible value
		static bool warned_use = false;
		if(GCC_UNLIKELY(!warned_use)){ 
			LOG(LOG_INT10,LOG_ERROR)("writechar used without attribute in non-textmode %c %X",chr,chr);
			warned_use = true;
		}
		switch(CurMode->type) {
		case M_CGA4:
			attr = 0x3;
			break;
		case M_CGA2:
			attr = 0x1;
			break;
		case M_TANDY16:
		case M_EGA:
		default:
			attr = 0xf;
			break;
		}
	}

	//Attribute behavior of mode 6; mode 11 does something similar but
	//it is in INT 10h handler because it only applies to function 09h
	if (CurMode->mode==0x06) attr=(attr&0x80)|1;

	switch (CurMode->type) {
	case M_VGA:
	case M_LIN8:
		// 256-color modes have background color instead of page
		back=page;
		page=0;
		break;
	case M_EGA:
		/* enable all planes for EGA modes (Ultima 1 colour bug) */
		/* might be put into INT10_PutPixel but different vga bios
		   implementations have different opinions about this */
		IO_Write(0x3c4,0x2);IO_Write(0x3c5,0xf);
		// fall-through
	default:
		back=attr&0x80;
		break;
	}

	x=(pos%CurMode->twidth)*8;
	y=(pos/CurMode->twidth)*cheight;

	Bit16u ty=(Bit16u)y;
	for (Bit8u h=0;h<cheight;h++) {
		Bit8u bitsel=128;
		Bit8u bitline = mem_readb(fontdata++);
		Bit16u tx=(Bit16u)x;
		while (bitsel) {
			INT10_PutPixel(tx,ty,page,(bitline&bitsel)?attr:back);
			tx++;
			bitsel>>=1;
		}
		ty++;
	}
}

void INT10_WriteChar(Bit8u chr,Bit8u attr,Bit8u page,Bit16u count,bool showattr) {
	Bit8u pospage=page;
	if (CurMode->type!=M_TEXT) {
		showattr=true; //Use attr in graphics mode always
		switch (machine) {
			case EGAVGA_ARCH_CASE:
				switch (CurMode->type) {
				case M_VGA:
				case M_LIN8:
					pospage=0;
					break;
				default:
					page%=CurMode->ptotal;
					pospage=page;
					break;
				}
				break;
			case MCH_CGA:
			case MCH_DCGA:
			case MCH_PCJR:
				page=0;
				pospage=0;
				break;
		}
	}

	Bit8u cur_row=CURSOR_POS_ROW(pospage);
	Bit8u cur_col=CURSOR_POS_COL(pospage);
	BIOS_NCOLS;
	while (count>0) {
		WriteChar(cur_col,cur_row,page,chr,attr,showattr);
		count--;
		cur_col++;
		if(cur_col==ncols) {
			cur_col=0;
			cur_row++;
		}
	}
}

static void INT10_TeletypeOutputAttr(Bit8u chr,Bit8u attr,bool useattr,Bit8u page) {
	BIOS_NCOLS;BIOS_NROWS;
	Bit8u cur_row=CURSOR_POS_ROW(page);
	Bit8u cur_col=CURSOR_POS_COL(page);
	switch (chr) {
	case 7: /* Beep */
		// Prepare PIT counter 2 for ~900 Hz square wave
		IO_Write(0x43,0xb6);
		IO_Write(0x42,0x28);
		IO_Write(0x42,0x05);
		// Speaker on
		IO_Write(0x61,IO_Read(0x61)|3);
		// Idle for 1/3rd of a second
		double start;
		start=PIC_FullIndex();
		while ((PIC_FullIndex()-start)<333.0) CALLBACK_Idle();
		// Speaker off
		IO_Write(0x61,IO_Read(0x61)&~3);
		// No change in position
		return;
	case 8:
		if(cur_col>0) cur_col--;
		break;
	case '\r':
		cur_col=0;
		break;
	case '\n':
//		cur_col=0; //Seems to break an old chess game
		cur_row++;
		break;
	default:
		/* Return if the char code is DBCS at the end of the line (for AX) */
		if (cur_col + 1 == ncols && DOSV_CheckJapaneseVideoMode() && isKanji1(chr) && prevchr == 0)
		{ 
			INT10_TeletypeOutputAttr(' ', attr, useattr, page);
			cur_row = CURSOR_POS_ROW(page);
			cur_col = CURSOR_POS_COL(page);
		}
		/* Draw the actual Character */
		WriteChar(cur_col,cur_row,page,chr,attr,useattr);
		cur_col++;
	}
	if(cur_col==ncols) {
		cur_col=0;
		cur_row++;
	}
	// Do we need to scroll ?
	if(cur_row==nrows) {
		//Fill with black on non-text modes and with attribute at cursor on textmode
		Bit8u fill=0;
		if(CurMode->type==M_TEXT || DOSV_CheckJapaneseVideoMode()) {
			Bit16u chat;
			ReadCharAttr(cur_col,cur_row - 1,page, &chat);
			//INT10_ReadCharAttr(&chat,page);
			fill=(Bit8u)(chat>>8);
		}
		INT10_ScrollWindow(0,0,(Bit8u)(nrows-1),(Bit8u)(ncols-1),-1,fill,page);
		cur_row--;
	}
	// Set the cursor for the page
	INT10_SetCursorPos(cur_row,cur_col,page);
}

void INT10_TeletypeOutputAttr(Bit8u chr,Bit8u attr,bool useattr) {
	INT10_TeletypeOutputAttr(chr,attr,useattr,real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE));
}

void INT10_TeletypeOutput(Bit8u chr,Bit8u attr) {
	INT10_TeletypeOutputAttr(chr,attr, CurMode->type!=M_TEXT);
}

void INT10_WriteString(Bit8u row,Bit8u col,Bit8u flag,Bit8u attr,PhysPt string,Bit16u count,Bit8u page) {
	Bit8u cur_row=CURSOR_POS_ROW(page);
	Bit8u cur_col=CURSOR_POS_COL(page);

	// if row=0xff special case : use current cursor position
	if (row==0xff) {
		row=cur_row;
		col=cur_col;
	}
	INT10_SetCursorPos(row,col,page);
	while (count>0) {
		Bit8u chr=mem_readb(string);
		string++;
		if((flag & 2) != 0 || flag == 0x20) {
			attr=mem_readb(string);
			string++;
		};
		if(flag == 0x20) {
			WriteChar(col, row, page, chr, attr, true);
			col++;
			if(col == real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS)) {
				col = 0;
				if(row == real_readb(BIOSMEM_SEG,BIOSMEM_NB_ROWS) + 1) {
					break;
				}
				row++;
			}
		} else {
			INT10_TeletypeOutputAttr(chr,attr,true,page);
		}
		count--;
	}
	if((flag & 1) == 0) {
		INT10_SetCursorPos(cur_row, cur_col, page);
	}
}

void INT10_ReadString(Bit8u row, Bit8u col, Bit8u flag, Bit8u attr, PhysPt string, Bit16u count,Bit8u page)
{
	Bit16u result;

	while (count > 0) {
		ReadCharAttr(col, row, page, &result);
		mem_writew(string, result);
		string += 2;
		col++;
		if(col == real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS)) {
			col = 0;
			if(row == real_readb(BIOSMEM_SEG,BIOSMEM_NB_ROWS) + 1) {
				break;
			}
			row++;
		}
		count--;
	}
}
