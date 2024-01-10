/*
 *  Copyright (C) 2002-2021  The DOSBox Team
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
#include <stdio.h>

#include "dosbox.h"

#include "pic.h"
#include "regs.h"
#include "cpu.h"
#include "lazyflags.h"
#include "paging.h"
#include "fpu.h"
#include "debug.h"
#include "inout.h"
#include "callback.h"

extern bool ignore_opcode_63;

typedef PhysPt EAPoint;
#define SegBase(c)	SegPhys(c)

#define LoadMb(off) mem_readb_inline(off)
#define LoadMw(off) mem_readw_inline(off)
#define LoadMd(off) mem_readd_inline(off)

#define LoadMbs(off) (Bit8s)(LoadMb(off))
#define LoadMws(off) (Bit16s)(LoadMw(off))
#define LoadMds(off) (Bit32s)(LoadMd(off))

#define SaveMb(off,val)	mem_writeb_inline(off,val)
#define SaveMw(off,val)	mem_writew_inline(off,val)
#define SaveMd(off,val)	mem_writed_inline(off,val)

#define LoadD(reg) reg
#define SaveD(reg,val)	reg=val



#include "core_full/loadwrite.h"
#include "core_full/support.h"
#include "core_full/optable.h"
#include "instructions.h"

#define EXCEPTION(blah)										\
	{														\
		Bit8u new_num=blah;									\
		CPU_Exception(new_num,0);							\
		continue;											\
	}

Bits CPU_Core_Full_Run(void) {
	FullData inst;	
	while (CPU_Cycles-->0) {
#if C_DEBUG
		cycle_count++;
#if C_HEAVY_DEBUG
		if (DEBUG_HeavyIsBreakpoint()) {
			FillFlags();
			return debugCallback;
		};
#endif
#endif
		LoadIP();
		inst.entry=cpu.code.big*0x200;
		inst.prefix=cpu.code.big;
restartopcode:
		inst.entry=(inst.entry & 0xffffff00) | Fetchb();
		inst.code=OpCodeTable[inst.entry];
		#include "core_full/load.h"
		#include "core_full/op.h"
		#include "core_full/save.h"
nextopcode:;
		SaveIP();
		continue;
illegalopcode:
#if C_DEBUG	
		{
			bool ignore=false;
			Bitu len=(GetIP()-reg_eip);
			LoadIP();
			if (len>16) len=16;
			char tempcode[16*2+1];char * writecode=tempcode;
			if (ignore_opcode_63 && mem_readb(inst.cseip) == 0x63)
				ignore = true;
			for (;len>0;len--) {
				sprintf(writecode,"%02X",mem_readb(inst.cseip++));
				writecode+=2;
			}
			if (!ignore)
				LOG(LOG_CPU,LOG_NORMAL)("Illegal/Unhandled opcode %s",tempcode);
		}
#endif
		CPU_Exception(0x6,0);
	}
	FillFlags();
	return CBRET_NONE;
}


void CPU_Core_Full_Init(void) {

}
