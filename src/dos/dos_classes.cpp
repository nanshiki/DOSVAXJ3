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
 *
 *  Wengier: LFN support
 */


#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "dosbox.h"
#include "mem.h"
#include "drives.h"
#include "dos_inc.h"
#include "support.h"

Bit8u sdriv[260], sattr[260], fattr;
char sname[260][LFN_NAMELENGTH+1],storect[CTBUF];
extern int lfn_filefind_handle;

struct finddata {
	Bit8u attr;
	Bit8u fres1[19];
	Bit32u mtime;
	Bit32u mdate;
	Bit32u hsize;
	Bit32u size;
	Bit8u fres2[8];
	char lname[260];
	char sname[14];
} fd;

void DOS_ParamBlock::Clear(void) {
	memset(&exec,0,sizeof(exec));
	memset(&overlay,0,sizeof(overlay));
}

void DOS_ParamBlock::LoadData(void) {
	exec.envseg=(Bit16u)sGet(sExec,envseg);
	exec.cmdtail=sGet(sExec,cmdtail);
	exec.fcb1=sGet(sExec,fcb1);
	exec.fcb2=sGet(sExec,fcb2);
	exec.initsssp=sGet(sExec,initsssp);
	exec.initcsip=sGet(sExec,initcsip);
	overlay.loadseg=(Bit16u)sGet(sOverlay,loadseg);
	overlay.relocation=(Bit16u)sGet(sOverlay,relocation);
}

void DOS_ParamBlock::SaveData(void) {
	sSave(sExec,envseg,exec.envseg);
	sSave(sExec,cmdtail,exec.cmdtail);
	sSave(sExec,fcb1,exec.fcb1);
	sSave(sExec,fcb2,exec.fcb2);
	sSave(sExec,initsssp,exec.initsssp);
	sSave(sExec,initcsip,exec.initcsip);
}


void DOS_InfoBlock::SetLocation(Bit16u segment) {
	seg = segment;
	pt=PhysMake(seg,0);
	/* Clear the initial Block */
	for(Bitu i=0;i<sizeof(sDIB);i++) mem_writeb(pt+i,0xff);
	for(Bitu i=0;i<14;i++) mem_writeb(pt+i,0);

	sSave(sDIB,regCXfrom5e,(Bit16u)0);
	sSave(sDIB,countLRUcache,(Bit16u)0);
	sSave(sDIB,countLRUopens,(Bit16u)0);

	sSave(sDIB,protFCBs,(Bit16u)0);
	sSave(sDIB,specialCodeSeg,(Bit16u)0);
	sSave(sDIB,joindedDrives,(Bit8u)0);
	sSave(sDIB,lastdrive,(Bit8u)0x01);//increase this if you add drives to cds-chain

	sSave(sDIB,diskInfoBuffer,RealMake(segment,offsetof(sDIB,diskBufferHeadPt)));
	sSave(sDIB,setverPtr,(Bit32u)0);

	sSave(sDIB,a20FixOfs,(Bit16u)0);
	sSave(sDIB,pspLastIfHMA,(Bit16u)0);
	sSave(sDIB,blockDevices,(Bit8u)0);
	
	sSave(sDIB,bootDrive,(Bit8u)0);
	sSave(sDIB,useDwordMov,(Bit8u)1);
	sSave(sDIB,extendedSize,(Bit16u)(MEM_TotalPages()*4-1024));
	sSave(sDIB,magicWord,(Bit16u)0x0001);		// dos5+

	sSave(sDIB,sharingCount,(Bit16u)0);
	sSave(sDIB,sharingDelay,(Bit16u)0);
	sSave(sDIB,ptrCONinput,(Bit16u)0);			// no unread input available
	sSave(sDIB,maxSectorLength,(Bit16u)0x200);

	sSave(sDIB,dirtyDiskBuffers,(Bit16u)0);
	sSave(sDIB,lookaheadBufPt,(Bit32u)0);
	sSave(sDIB,lookaheadBufNumber,(Bit16u)0);
	sSave(sDIB,bufferLocation,(Bit8u)0);		// buffer in base memory, no workspace
	sSave(sDIB,workspaceBuffer,(Bit32u)0);

	sSave(sDIB,minMemForExec,(Bit16u)0);
	sSave(sDIB,memAllocScanStart,(Bit16u)DOS_MEM_START);
	sSave(sDIB,startOfUMBChain,(Bit16u)0xffff);
	sSave(sDIB,chainingUMB,(Bit8u)0);

	sSave(sDIB,nulNextDriver,(Bit32u)0xffffffff);
	sSave(sDIB,nulAttributes,(Bit16u)0x8004);
	sSave(sDIB,nulStrategy,(Bit32u)0x00000000);
	sSave(sDIB,nulString[0],(Bit8u)0x4e);
	sSave(sDIB,nulString[1],(Bit8u)0x55);
	sSave(sDIB,nulString[2],(Bit8u)0x4c);
	sSave(sDIB,nulString[3],(Bit8u)0x20);
	sSave(sDIB,nulString[4],(Bit8u)0x20);
	sSave(sDIB,nulString[5],(Bit8u)0x20);
	sSave(sDIB,nulString[6],(Bit8u)0x20);
	sSave(sDIB,nulString[7],(Bit8u)0x20);

	/* Create a fake SFT, so programs think there are 100 file handles */
	Bit16u sftOffset=offsetof(sDIB,firstFileTable)+0xa2;
	sSave(sDIB,firstFileTable,RealMake(segment,sftOffset));
	real_writed(segment,sftOffset+0x00,RealMake(segment+0x26,0));	//Next File Table
	real_writew(segment,sftOffset+0x04,100);		//File Table supports 100 files
	real_writed(segment+0x26,0x00,0xffffffff);		//Last File Table
	real_writew(segment+0x26,0x04,100);				//File Table supports 100 files
}

void DOS_InfoBlock::SetFirstMCB(Bit16u _firstmcb) {
	sSave(sDIB,firstMCB,_firstmcb); //c2woody
}

void DOS_InfoBlock::SetBuffers(Bit16u x,Bit16u y) {
	sSave(sDIB,buffers_x,x);
	sSave(sDIB,buffers_y,y);
}

void DOS_InfoBlock::SetCurDirStruct(Bit32u _curdirstruct) {
	sSave(sDIB,curDirStructure,_curdirstruct);
}

void DOS_InfoBlock::SetFCBTable(Bit32u _fcbtable) {
	sSave(sDIB,fcbTable,_fcbtable);
}

void DOS_InfoBlock::SetDeviceChainStart(Bit32u _devchain) {
	sSave(sDIB,nulNextDriver,_devchain);
}

void DOS_InfoBlock::SetDiskBufferHeadPt(Bit32u _dbheadpt) {
	sSave(sDIB,diskBufferHeadPt,_dbheadpt);
}

Bit16u DOS_InfoBlock::GetStartOfUMBChain(void) {
	return (Bit16u)sGet(sDIB,startOfUMBChain);
}

void DOS_InfoBlock::SetStartOfUMBChain(Bit16u _umbstartseg) {
	sSave(sDIB,startOfUMBChain,_umbstartseg);
}

Bit8u DOS_InfoBlock::GetUMBChainState(void) {
	return (Bit8u)sGet(sDIB,chainingUMB);
}

void DOS_InfoBlock::SetUMBChainState(Bit8u _umbchaining) {
	sSave(sDIB,chainingUMB,_umbchaining);
}

void DOS_InfoBlock::SetBlockDevices(Bit8u _count) {
	sSave(sDIB,blockDevices,_count);
}

void DOS_InfoBlock::SetFirstDPB(Bit32u _first_dpb) {
    sSave(sDIB,firstDPB,_first_dpb);
}

RealPt DOS_InfoBlock::GetPointer(void) {
	return RealMake(seg,offsetof(sDIB,firstDPB));
}

Bit32u DOS_InfoBlock::GetDeviceChain(void) {
	return sGet(sDIB,nulNextDriver);
}


/* program Segment prefix */

Bit16u DOS_PSP::rootpsp = 0;

void DOS_PSP::MakeNew(Bit16u mem_size) {
	/* get previous */
//	DOS_PSP prevpsp(dos.psp());
	/* Clear it first */
	Bitu i;
	for (i=0;i<sizeof(sPSP);i++) mem_writeb(pt+i,0);
	// Set size
	sSave(sPSP,next_seg,seg+mem_size);
	/* far call opcode */
	sSave(sPSP,far_call,0xea);
	// far call to interrupt 0x21 - faked for bill & ted 
	// lets hope nobody really uses this address
	sSave(sPSP,cpm_entry,RealMake(0xDEAD,0xFFFF));
	/* Standard blocks,int 20  and int21 retf */
	sSave(sPSP,exit[0],0xcd);
	sSave(sPSP,exit[1],0x20);
	sSave(sPSP,service[0],0xcd);
	sSave(sPSP,service[1],0x21);
	sSave(sPSP,service[2],0xcb);
	/* psp and psp-parent */
	sSave(sPSP,psp_parent,dos.psp());
	sSave(sPSP,prev_psp,0xffffffff);
	sSave(sPSP,dos_version,0x0005);
	/* terminate 22,break 23,crititcal error 24 address stored */
	SaveVectors();

	/* FCBs are filled with 0 */
	// ....
	/* Init file pointer and max_files */
	sSave(sPSP,file_table,RealMake(seg,offsetof(sPSP,files)));
	sSave(sPSP,max_files,20);
	for (Bit16u ct=0;ct<20;ct++) SetFileHandle(ct,0xff);

	/* User Stack pointer */
//	if (prevpsp.GetSegment()!=0) sSave(sPSP,stack,prevpsp.GetStack());

	if (rootpsp==0) rootpsp = seg;
}

Bit8u DOS_PSP::GetFileHandle(Bit16u index) {
	if (index>=sGet(sPSP,max_files)) return 0xff;
	PhysPt files=Real2Phys(sGet(sPSP,file_table));
	return mem_readb(files+index);
}

void DOS_PSP::SetFileHandle(Bit16u index, Bit8u handle) {
	if (index<sGet(sPSP,max_files)) {
		PhysPt files=Real2Phys(sGet(sPSP,file_table));
		mem_writeb(files+index,handle);
	}
}

Bit16u DOS_PSP::FindFreeFileEntry(void) {
	PhysPt files=Real2Phys(sGet(sPSP,file_table));
	for (Bit16u i=0;i<sGet(sPSP,max_files);i++) {
		if (mem_readb(files+i)==0xff) return i;
	}	
	return 0xff;
}

Bit16u DOS_PSP::FindEntryByHandle(Bit8u handle) {
	PhysPt files=Real2Phys(sGet(sPSP,file_table));
	for (Bit16u i=0;i<sGet(sPSP,max_files);i++) {
		if (mem_readb(files+i)==handle) return i;
	}	
	return 0xFF;
}

void DOS_PSP::CopyFileTable(DOS_PSP* srcpsp,bool createchildpsp) {
	/* Copy file table from calling process */
	for (Bit16u i=0;i<20;i++) {
		Bit8u handle = srcpsp->GetFileHandle(i);
		if(createchildpsp)
		{	//copy obeying not inherit flag.(but dont duplicate them)
			bool allowCopy = true;//(handle==0) || ((handle>0) && (FindEntryByHandle(handle)==0xff));
			if((handle<DOS_FILES) && Files[handle] && !(Files[handle]->flags & DOS_NOT_INHERIT) && allowCopy)
			{   
				Files[handle]->AddRef();
				SetFileHandle(i,handle);
			}
			else
			{
				SetFileHandle(i,0xff);
			}
		}
		else
		{	//normal copy so don't mind the inheritance
			SetFileHandle(i,handle);
		}
	}
}

void DOS_PSP::CloseFiles(void) {
	for (Bit16u i=0;i<sGet(sPSP,max_files);i++) {
		DOS_CloseFile(i);
	}
}

void DOS_PSP::SaveVectors(void) {
	/* Save interrupt 22,23,24 */
	sSave(sPSP,int_22,RealGetVec(0x22));
	sSave(sPSP,int_23,RealGetVec(0x23));
	sSave(sPSP,int_24,RealGetVec(0x24));
}

void DOS_PSP::RestoreVectors(void) {
	/* Restore interrupt 22,23,24 */
	RealSetVec(0x22,sGet(sPSP,int_22));
	RealSetVec(0x23,sGet(sPSP,int_23));
	RealSetVec(0x24,sGet(sPSP,int_24));
}

void DOS_PSP::SetCommandTail(RealPt src) {
	if (src) {	// valid source
		MEM_BlockCopy(pt+offsetof(sPSP,cmdtail),Real2Phys(src),CTBUF+1);
	} else {	// empty
		sSave(sPSP,cmdtail.count,0x00);
		mem_writeb(pt+offsetof(sPSP,cmdtail.buffer),0x0d);
	};
}

void DOS_PSP::StoreCommandTail() {
	int len=mem_strlen(pt+offsetof(sPSP,cmdtail.buffer));
	MEM_StrCopy(pt+offsetof(sPSP,cmdtail.buffer),storect,len>CTBUF?CTBUF:len);
}

void DOS_PSP::RestoreCommandTail() {
	mem_writeb(pt+offsetof(sPSP,cmdtail.count),(Bit8u)(strlen(storect)>0?strlen(storect)-1:0));
	MEM_BlockWrite(pt+offsetof(sPSP,cmdtail.buffer),storect,strlen(storect));
}

void DOS_PSP::SetFCB1(RealPt src) {
	if (src) MEM_BlockCopy(PhysMake(seg,offsetof(sPSP,fcb1)),Real2Phys(src),16);
}

void DOS_PSP::SetFCB2(RealPt src) {
	if (src) MEM_BlockCopy(PhysMake(seg,offsetof(sPSP,fcb2)),Real2Phys(src),16);
}

bool DOS_PSP::SetNumFiles(Bit16u fileNum) {
	//20 minimum. clipper program.
	if (fileNum < 20) fileNum = 20;
	 
	if (fileNum > 20 && ((fileNum+2U) > sGet(sPSP,max_files))) {
		// Allocate needed paragraphs
		fileNum+=2;	// Add a few more files for safety
		Bit16u para = (fileNum/16)+((fileNum%16)>0);
		RealPt data	= RealMake(DOS_GetMemory(para),0);
		for (Bit16u i=0; i<fileNum; i++) mem_writeb(Real2Phys(data)+i,(i<20)?GetFileHandle(i):0xFF);
		sSave(sPSP,file_table,data);
	}
	sSave(sPSP,max_files,fileNum);
	return true;
}

void DOS_DTA::SetupSearch(Bit8u _sdrive,Bit8u _sattr,char * pattern) {
	unsigned int i;

	if (lfn_filefind_handle<LFN_FILEFIND_NONE) {
		sdriv[lfn_filefind_handle]=_sdrive;
		sattr[lfn_filefind_handle]=_sattr;
		for (i=0;i<LFN_NAMELENGTH;i++) {
			if (pattern[i]==0) break;
				sname[lfn_filefind_handle][i]=pattern[i];
		}
		while (i<=LFN_NAMELENGTH) sname[lfn_filefind_handle][i++]=0;
	}
    for (i=0;i<11;i++) mem_writeb(pt+offsetof(sDTA,spname)+i,0);

	sSave(sDTA,sdrive,_sdrive);
	sSave(sDTA,sattr,_sattr);
    const char* find_ext;
    find_ext=strchr(pattern,'.');
    if (find_ext) {
        Bitu size=(Bitu)(find_ext-pattern);
		if (size>8) size=8;
			MEM_BlockWrite(pt+offsetof(sDTA,spname),pattern,size);
			find_ext++;
			MEM_BlockWrite(pt+offsetof(sDTA,spext),find_ext,(strlen(find_ext)>3) ? 3 : (Bitu)strlen(find_ext));
	} else {
		MEM_BlockWrite(pt+offsetof(sDTA,spname),pattern,(strlen(pattern) > 8) ? 8 : (Bitu)strlen(pattern));
	}
}

void DOS_DTA::SetResult(const char * _name, const char * _lname, Bit32u _size,Bit16u _date,Bit16u _time,Bit8u _attr) {
	fd.hsize=0;
	fd.size=_size;
	fd.mdate=_date;
	fd.mtime=_time;
	fd.attr=_attr;
	strcpy(fd.lname,_lname);
	strcpy(fd.sname,_name);
	if (!strcasecmp(fd.lname,fd.sname)) fd.sname[0]=0;
	if (lfn_filefind_handle>=LFN_FILEFIND_MAX) {
		MEM_BlockWrite(pt+offsetof(sDTA,name),(void *)_name,strlen(_name)+1);
		sSave(sDTA,size,_size);
		sSave(sDTA,date,_date);
		sSave(sDTA,time,_time);
		sSave(sDTA,attr,_attr);
	}
}

void DOS_DTA::GetResult(char * _name, char * _lname,Bit32u & _size,Bit16u & _date,Bit16u & _time,Bit8u & _attr) {
	strcpy(_lname,fd.lname);
	if (fd.sname[0]!=0) strcpy(_name,fd.sname);
	else if (strlen(fd.lname)<DOS_NAMELENGTH_ASCII) strcpy(_name,fd.lname);
	_size = fd.size;
	_date = fd.mdate;
	_time = fd.mtime;
	_attr = fd.attr;
	if (lfn_filefind_handle>=LFN_FILEFIND_MAX) {
		MEM_BlockRead(pt+offsetof(sDTA,name),_name,DOS_NAMELENGTH_ASCII);
		_size=sGet(sDTA,size);
		_date=(uint16_t)sGet(sDTA,date);
		_time=(uint16_t)sGet(sDTA,time);
		_attr=(uint8_t)sGet(sDTA,attr);
	}
}

#define	FIND_DATA_SIZE	(4+8+8+8+8+8+260+14)
void set_dword(char *buff, uint32_t data);

int DOS_DTA::GetFindData(int fmt, char * fdstr) {
	memset(fdstr, 0, FIND_DATA_SIZE);
	set_dword(fdstr, fd.attr);
	if(fmt == 1) {
		set_dword(&fdstr[20], ((Bit32u)fd.mdate << 16) | fd.mtime);
	} else {
		struct tm ftm = {0};
		ftm.tm_year = ((fd.mdate >> 9) & 0x7f) + 80;
		ftm.tm_mon = ((fd.mdate >> 5) & 0x0f) - 1;
		ftm.tm_mday = (fd.mdate & 0x1f);
		ftm.tm_hour = (fd.mtime >> 11) & 0x1f;
		ftm.tm_min = (fd.mtime >> 5) & 0x3f;
		ftm.tm_sec = (fd.mtime & 0x1f) * 2;
		ftm.tm_isdst = -1;
		// 116444736000000000LL = FILETIME 1970/01/01 00:00:00
		long long ff = 116444736000000000LL + (long long)mktime(&ftm) * 10000000LL;
		set_dword(&fdstr[20], (Bit32u)ff);
		set_dword(&fdstr[24], (Bit32u)(ff >> 32));
	}
	set_dword(&fdstr[28], fd.hsize);
	set_dword(&fdstr[32], fd.size);
	strcpy(&fdstr[44], fd.lname);
	strcpy(&fdstr[304], fd.sname);
	return FIND_DATA_SIZE;
}

Bit8u DOS_DTA::GetSearchDrive(void) {
	return lfn_filefind_handle>=LFN_FILEFIND_MAX?(uint8_t)sGet(sDTA,sdrive):sdriv[lfn_filefind_handle];
}

void DOS_DTA::GetSearchParams(Bit8u & attr,char * pattern, bool lfn) {
    if (lfn||uselfn&&lfn_filefind_handle<LFN_FILEFIND_NONE) {
		attr=sattr[lfn_filefind_handle];
        memcpy(pattern,sname[lfn_filefind_handle],LFN_NAMELENGTH);
        pattern[LFN_NAMELENGTH]=0;
    } else {
		attr=(uint8_t)sGet(sDTA,sattr);
        char temp[11];
        MEM_BlockRead(pt+offsetof(sDTA,spname),temp,11);
        for (int i=0;i<13;i++) pattern[i]=0;
            memcpy(pattern,temp,8);
            pattern[strlen(pattern)]='.';
            memcpy(&pattern[strlen(pattern)],&temp[8],3);
    }
}

DOS_FCB::DOS_FCB(Bit16u seg,Bit16u off,bool allow_extended) { 
	SetPt(seg,off); 
	real_pt=pt;
	extended=false;
	if (allow_extended) {
		if (sGet(sFCB,drive)==0xff) {
			pt+=7;
			extended=true;
		}
	}
}

bool DOS_FCB::Extended(void) {
	return extended;
}

void DOS_FCB::Create(bool _extended) {
	Bitu fill;
	if (_extended) fill=33+7;
	else fill=33;
	Bitu i;
	for (i=0;i<fill;i++) mem_writeb(real_pt+i,0);
	pt=real_pt;
	if (_extended) {
		mem_writeb(real_pt,0xff);
		pt+=7;
		extended=true;
	} else extended=false;
}

void DOS_FCB::SetName(Bit8u _drive,char * _fname,char * _ext) {
	sSave(sFCB,drive,_drive);
	MEM_BlockWrite(pt+offsetof(sFCB,filename),_fname,8);
	MEM_BlockWrite(pt+offsetof(sFCB,ext),_ext,3);
}

void DOS_FCB::SetSizeDateTime(Bit32u _size, Bit16u _date, Bit16u _time, bool byFindFCB) {
	sSave(sFCB,filesize,_size);
	sSave(sFCB,date,_date);
	sSave(sFCB,time,_time);
#if 1
	if (byFindFCB) {
		/* DOS directory entry (see table 01352 on Interrupt List) */
		SaveIt(2, offsetof(sFCB,filename)+0x16,_time);
		SaveIt(2, offsetof(sFCB,filename)+0x18,_date);
		SaveIt(4, offsetof(sFCB,filename)+0x1c,_size);
	}
#endif
}

void DOS_FCB::GetSizeDateTime(Bit32u & _size,Bit16u & _date,Bit16u & _time) {
	_size=sGet(sFCB,filesize);
	_date=(Bit16u)sGet(sFCB,date);
	_time=(Bit16u)sGet(sFCB,time);
}

void DOS_FCB::GetRecord(Bit16u & _cur_block,Bit8u & _cur_rec) {
	_cur_block=(Bit16u)sGet(sFCB,cur_block);
	_cur_rec=(Bit8u)sGet(sFCB,cur_rec);

}

void DOS_FCB::SetRecord(Bit16u _cur_block,Bit8u _cur_rec) {
	sSave(sFCB,cur_block,_cur_block);
	sSave(sFCB,cur_rec,_cur_rec);
}

void DOS_FCB::GetSeqData(Bit8u & _fhandle,Bit16u & _rec_size) {
	_fhandle=(Bit8u)sGet(sFCB,file_handle);
	_rec_size=(Bit16u)sGet(sFCB,rec_size);
}

void DOS_FCB::SetSeqData(Bit8u _fhandle,Bit16u _rec_size) {
	sSave(sFCB,file_handle,_fhandle);
	sSave(sFCB,rec_size,_rec_size);
}

void DOS_FCB::GetRandom(Bit32u & _random) {
	_random=sGet(sFCB,rndm);
}

void DOS_FCB::SetRandom(Bit32u _random) {
	sSave(sFCB,rndm,_random);
}

void DOS_FCB::ClearBlockRecsize(void) {
	sSave(sFCB,cur_block,0);
	sSave(sFCB,rec_size,0);
}
void DOS_FCB::FileOpen(Bit8u _fhandle) {
	sSave(sFCB,drive,GetDrive()+1);
	sSave(sFCB,file_handle,_fhandle);
	sSave(sFCB,cur_block,0);
	sSave(sFCB,rec_size,128);
//	sSave(sFCB,rndm,0); // breaks Jewels of darkness. 
	Bit32u size = 0;
	Files[_fhandle]->Seek(&size,DOS_SEEK_END);
	sSave(sFCB,filesize,size);
	size = 0;
	Files[_fhandle]->Seek(&size,DOS_SEEK_SET);
	sSave(sFCB,time,Files[_fhandle]->time);
	sSave(sFCB,date,Files[_fhandle]->date);
}

bool DOS_FCB::Valid() {
	//Very simple check for Oubliette
	if(sGet(sFCB,filename[0]) == 0 && sGet(sFCB,file_handle) == 0) return false;
	return true;
}

void DOS_FCB::FileClose(Bit8u & _fhandle) {
	_fhandle=(Bit8u)sGet(sFCB,file_handle);
	sSave(sFCB,file_handle,0xff);
}

Bit8u DOS_FCB::GetDrive(void) {
	Bit8u drive=(Bit8u)sGet(sFCB,drive);
	if (!drive) return  DOS_GetDefaultDrive();
	else return drive-1;
}

void DOS_FCB::GetName(char * fillname) {
	fillname[0]=GetDrive()+'A';
	fillname[1]=':';
	MEM_BlockRead(pt+offsetof(sFCB,filename),&fillname[2],8);
	fillname[10]='.';
	MEM_BlockRead(pt+offsetof(sFCB,ext),&fillname[11],3);
	fillname[14]=0;
}

void DOS_FCB::GetAttr(Bit8u& attr) {
	if(extended) attr=mem_readb(pt - 1);
}

void DOS_FCB::SetAttr(Bit8u attr, bool byFindFCB) {
	if (extended) mem_writeb(pt - 1, attr);
#if 1
	/* DOS directory entry (see table 01352 on Interrupt List) */
	if (byFindFCB) SaveIt(1, offsetof(sFCB, filename) + 0x0b, attr);
#endif
}

void DOS_FCB::SetResult(Bit32u size,Bit16u date,Bit16u time,Bit8u attr) {
	mem_writed(pt + 0x1d,size);
	mem_writew(pt + 0x19,date);
	mem_writew(pt + 0x17,time);
	mem_writeb(pt + 0x0c,attr);
}

void DOS_SDA::Init() {
	/* Clear */
	for(Bitu i=0;i<sizeof(sSDA);i++) mem_writeb(pt+i,0x00);
	sSave(sSDA,drive_crit_error,0xff);   
}
