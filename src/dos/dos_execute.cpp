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


#include <string.h>
#include <ctype.h>
#include "dosbox.h"
#include "mem.h"
#include "dos_inc.h"
#include "regs.h"
#include "callback.h"
#include "debug.h"
#include "cpu.h"

const char * RunningProgram="DOSBOX";

#ifdef _MSC_VER
#pragma pack(1)
#endif
struct EXE_Header {
	Bit16u signature;					/* EXE Signature MZ or ZM */
	Bit16u extrabytes;					/* Bytes on the last page */
	Bit16u pages;						/* Pages in file */
	Bit16u relocations;					/* Relocations in file */
	Bit16u headersize;					/* Paragraphs in header */
	Bit16u minmemory;					/* Minimum amount of memory */
	Bit16u maxmemory;					/* Maximum amount of memory */
	Bit16u initSS;
	Bit16u initSP;
	Bit16u checksum;
	Bit16u initIP;
	Bit16u initCS;
	Bit16u reloctable;
	Bit16u overlay;
} GCC_ATTRIBUTE(packed);
#ifdef _MSC_VER
#pragma pack()
#endif

#define MAGIC1 0x5a4d
#define MAGIC2 0x4d5a
#define MAXENV 32768u
#define ENV_KEEPFREE 83				 /* keep unallocated by environment variables */
#define LOADNGO 0
#define LOAD    1
#define OVERLAY 3


extern void GFX_SetTitle(Bit32s cycles,int frameskip,bool paused);
void DOS_UpdatePSPName(void) {
	DOS_MCB mcb(dos.psp()-1);
	static char name[9];
	mcb.GetFileName(name);
	name[8] = 0;
	if (!strlen(name)) strcpy(name,"DOSBOX");
	for(Bitu i = 0;i < 8;i++) { //Don't put garbage in the title bar. Mac OS X doesn't like it
		if (name[i] == 0) break;
		if ( !isprint(*reinterpret_cast<unsigned char*>(&name[i])) ) name[i] = '?';
	}
	RunningProgram = name;
	GFX_SetTitle(-1,-1,false);
}

void DOS_Terminate(Bit16u pspseg,bool tsr,Bit8u exitcode) {

	dos.return_code=exitcode;
	dos.return_mode=(tsr)?(Bit8u)RETURN_TSR:(Bit8u)RETURN_EXIT;
	
	DOS_PSP curpsp(pspseg);
	if (pspseg==curpsp.GetParent()) return;
	/* Free Files owned by process */
	if (!tsr) curpsp.CloseFiles();
	
	/* Get the termination address */
	RealPt old22 = curpsp.GetInt22();
	/* Restore vector 22,23,24 */
	curpsp.RestoreVectors();
	/* Set the parent PSP */
	dos.psp(curpsp.GetParent());
	DOS_PSP parentpsp(curpsp.GetParent());

	/* Restore the SS:SP to the previous one */
	SegSet16(ss,RealSeg(parentpsp.GetStack()));
	reg_sp = RealOff(parentpsp.GetStack());		
	/* Restore registers */
	reg_ax = real_readw(SegValue(ss),reg_sp+ 0);
	reg_bx = real_readw(SegValue(ss),reg_sp+ 2);
	reg_cx = real_readw(SegValue(ss),reg_sp+ 4);
	reg_dx = real_readw(SegValue(ss),reg_sp+ 6);
	reg_si = real_readw(SegValue(ss),reg_sp+ 8);
	reg_di = real_readw(SegValue(ss),reg_sp+10);
	reg_bp = real_readw(SegValue(ss),reg_sp+12);
	SegSet16(ds,real_readw(SegValue(ss),reg_sp+14));
	SegSet16(es,real_readw(SegValue(ss),reg_sp+16));
	reg_sp+=18;
	/* Set the CS:IP stored in int 0x22 back on the stack */
	real_writew(SegValue(ss),reg_sp+0,RealOff(old22));
	real_writew(SegValue(ss),reg_sp+2,RealSeg(old22));
	/* set IOPL=3 (Strike Commander), nested task set,
	   interrupts enabled, test flags cleared */
	real_writew(SegValue(ss),reg_sp+4,0x7202);
	// Free memory owned by process
	if (!tsr) DOS_FreeProcessMemory(pspseg);
	DOS_UpdatePSPName();

	if ((!(CPU_AutoDetermineMode>>CPU_AUTODETERMINE_SHIFT)) || (cpu.pmode)) return;

	CPU_AutoDetermineMode>>=CPU_AUTODETERMINE_SHIFT;
	if (CPU_AutoDetermineMode&CPU_AUTODETERMINE_CYCLES) {
		CPU_CycleAutoAdjust=false;
		CPU_CycleLeft=0;
		CPU_Cycles=0;
		CPU_CycleMax=CPU_OldCycleMax;
		GFX_SetTitle(CPU_OldCycleMax,-1,false);
	} else {
		GFX_SetTitle(-1,-1,false);
	}
#if (C_DYNAMIC_X86) || (C_DYNREC)
	if (CPU_AutoDetermineMode&CPU_AUTODETERMINE_CORE) {
		cpudecoder=&CPU_Core_Normal_Run;
		CPU_CycleLeft=0;
		CPU_Cycles=0;
	}
#endif

	return;
}

static bool MakeEnv(char * name,Bit16u * segment) {
	/* If segment to copy environment is 0 copy the caller's environment */
	DOS_PSP psp(dos.psp());
	PhysPt envread,envwrite;
	Bit16u envsize=1;
	bool parentenv=true;

	if (*segment==0) {
		if (!psp.GetEnvironment()) parentenv=false;				//environment seg=0
		envread=PhysMake(psp.GetEnvironment(),0);
	} else {
		if (!*segment) parentenv=false;						//environment seg=0
		envread=PhysMake(*segment,0);
	}

	if (parentenv) {
		for (envsize=0; ;envsize++) {
			if (envsize>=MAXENV - ENV_KEEPFREE) {
				DOS_SetError(DOSERR_ENVIRONMENT_INVALID);
				return false;
			}
			if (mem_readw(envread+envsize)==0) break;
		}
		envsize += 2;									/* account for trailing \0\0 */
	}
	Bit16u size=long2para(envsize+ENV_KEEPFREE);
	if (!DOS_AllocateMemory(segment,&size)) return false;
	envwrite=PhysMake(*segment,0);
	if (parentenv) {
		MEM_BlockCopy(envwrite,envread,envsize);
//		mem_memcpy(envwrite,envread,envsize);
		envwrite+=envsize;
	} else {
		mem_writeb(envwrite++,0);
	}
	mem_writew(envwrite,1);
	envwrite+=2;
	char namebuf[DOS_PATHLENGTH];
	if (DOS_Canonicalize(name,namebuf)) {
		MEM_BlockWrite(envwrite,namebuf,(Bitu)(strlen(namebuf)+1));
		return true;
	} else return false;
}

bool DOS_NewPSP(Bit16u segment, Bit16u size) {
	DOS_PSP psp(segment);
	psp.MakeNew(size);
	Bit16u parent_psp_seg=psp.GetParent();
	DOS_PSP psp_parent(parent_psp_seg);
	psp.CopyFileTable(&psp_parent,false);
	// copy command line as well (Kings Quest AGI -cga switch)
	psp.SetCommandTail(RealMake(parent_psp_seg,0x80));
	return true;
}

bool DOS_ChildPSP(Bit16u segment, Bit16u size) {
	DOS_PSP psp(segment);
	psp.MakeNew(size);
	Bit16u parent_psp_seg = psp.GetParent();
	Bit16u parent = psp.GetParent();
	DOS_PSP psp_parent(parent);
	{
	    // copy cmdline arguments
	    PhysPt pcmd, ccmd;
	    pcmd = PhysMake(parent, 0x80);
	    ccmd = PhysMake(segment, 0x80);
	    mem_memcpy(ccmd, pcmd, 128);
	}
	psp.CopyFileTable(&psp_parent,true);
	psp.SetCommandTail(RealMake(parent_psp_seg,0x80));
	psp.SetFCB1(RealMake(parent_psp_seg,0x5c));
	psp.SetFCB2(RealMake(parent_psp_seg,0x6c));
	psp.SetEnvironment(psp_parent.GetEnvironment());
	psp.SetSize(size);
	return true;
}

static void SetupPSP(Bit16u pspseg,Bit16u memsize,Bit16u envseg) {
	/* Fix the PSP for psp and environment MCB's */
	DOS_MCB mcb((Bit16u)(pspseg-1));
	mcb.SetPSPSeg(pspseg);
	mcb.SetPt((Bit16u)(envseg-1));
	mcb.SetPSPSeg(pspseg);

	DOS_PSP psp(pspseg);
	psp.MakeNew(memsize);
	psp.SetEnvironment(envseg);

	/* Copy file handles */
	DOS_PSP oldpsp(dos.psp());
	psp.CopyFileTable(&oldpsp,true);

}

static void SetupCMDLine(Bit16u pspseg,DOS_ParamBlock & block) {
	DOS_PSP psp(pspseg);
	// if cmdtail==0 it will inited as empty in SetCommandTail
	psp.SetCommandTail(block.exec.cmdtail);
}

#if defined(WIN32)

#include <map>
#include <imagehlp.h>
#pragma comment(linker, "/DEFAULTLIB:imagehlp.lib")

#define	TEXT_MAX			100
#define	NO_VC				1
#define	SET_ENV				2
#define	NO_ECHO				4
#define	ENV_SIZE			4096
#define	VALUE_SIZE			32768

bool DOS_GetRealFileName(char const *name, char *realname);
bool DOS_GetRealDirName(char const *name, char *dir);
bool get_key(Bit16u &code);

std::map<std::string, std::string> keep_env;

static bool OutputReadPipe(HANDLE rp, bool err = false)
{
	DWORD total, len;

	if(PeekNamedPipe(rp, NULL, 0, NULL, &total, NULL) == 0) {
		return false;
	}
	if(total > 0) {
		Bit8u c;
		Bit16u n;
		char *buff = new char[total + 1];
		if(ReadFile(rp, buff, total, &len, NULL)) {
			buff[len] = 0;
			for(DWORD no = 0 ; no < len ; no++) {
				c = buff[no];
				n = 1;
				DOS_WriteFile(err ? STDERR : STDOUT, &c, &n);
			}
		}
		delete [] buff;
		return true;
	}
	return false;
}

static void SetEnv()
{
	DOS_PSP *psp = new DOS_PSP(dos.psp());
	PhysPt env_read = PhysMake(psp->GetEnvironment(), 0);
	char env_string[ENV_SIZE + 1];
	char value[VALUE_SIZE];

	keep_env.clear();
	while(1) {
		MEM_StrCopy(env_read, env_string, ENV_SIZE);
		if(env_string[0]) {
			env_read += (PhysPt)(strlen(env_string) + 1);
			char *equal = strchr(env_string, '=');
			if(equal) {
				*equal = 0;
				if(strcasecmp(env_string, "COMSPEC") && strcasecmp(env_string, "DOSVAXJ3")) {
					value[0] = '\0';
					GetEnvironmentVariable(env_string, value, VALUE_SIZE);
					keep_env[env_string] = value;
					SetEnvironmentVariable(env_string, equal + 1);
				}
			}
		} else {
			break;
		}
	}
	delete psp;
}

static void BackEnv()
{
	for(std::map<std::string, std::string>::iterator env = keep_env.begin() ; env != keep_env.end() ; env++) {
		SetEnvironmentVariable(env->first.c_str(), env->second.empty() ? NULL : env->second.c_str());
	}
	keep_env.clear();
}

static int GetEnvDOSVAXJ3()
{
	DOS_PSP *psp = new DOS_PSP(dos.psp());
	PhysPt env_read = PhysMake(psp->GetEnvironment(), 0);
	char env_string[ENV_SIZE + 1];
	int mode = 0;

	while(1) {
		MEM_StrCopy(env_read, env_string, ENV_SIZE);
		if(env_string[0]) {
			env_read += (PhysPt)(strlen(env_string) + 1);
			char *equal = strchr(env_string, '=');
			if(equal) {
				*equal = 0;
				if(!strcasecmp(env_string, "DOSVAXJ3")) {
					char *pt;
					char value[ENV_SIZE];
					strncpy(value, equal + 1, ENV_SIZE);
					value[ENV_SIZE - 1] = '\0';
					pt = strtok(value, ", ");
					while(pt != NULL) {
						if(!strcasecmp(pt, "NOVC")) {
							mode |= NO_VC;
						} else if(!strcasecmp(pt, "SETENV")) {
							mode |= SET_ENV;
						} else if(!strcasecmp(pt, "NOECHO")) {
							mode |= NO_ECHO;
						}
						pt = strtok(NULL, ", ");
					}
					break;
				}
			}
		} else {
			break;
		}
	}
	delete psp;

	return mode;
}

static bool ExecuteConsoleProgram(char *exename, char *param, char *dir, bool novc_flag)
{
	bool result = false;
	int mode;
	HANDLE rp, wp;
	HANDLE erp, ewp;
	HANDLE irp, iwp;
	HMODULE hm;
	SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };

	mode = GetEnvDOSVAXJ3();
	if(mode & NO_VC) {
		novc_flag = true;
	}
	if(mode & SET_ENV) {
		SetEnv();
	}
	if(hm = LoadLibraryEx(exename, 0, LOAD_LIBRARY_AS_DATAFILE)) {
		// Delphi or C++Builder ?
		if(FindResource(hm, "DVCLAL", RT_RCDATA)) {
			novc_flag = true;
		}
		FreeLibrary(hm);
	}
	if(CreatePipe(&rp, &wp, &sa, 0) && CreatePipe(&erp, &ewp, &sa, 0) && CreatePipe(&irp, &iwp, &sa, 0)) {
		char cmd[256 * 2];
		PROCESS_INFORMATION pi = { 0 };
		STARTUPINFO si = { sizeof(STARTUPINFO) };
		si.dwFlags = STARTF_USESTDHANDLES;
		if(novc_flag) {
			si.hStdOutput = wp;
			si.hStdError = ewp;
			si.hStdInput = irp;
		}
		si.wShowWindow = SW_HIDE;
		sprintf(cmd, "%s %s", exename, param);
		SetHandleInformation(rp, HANDLE_FLAG_INHERIT, 0);
		SetHandleInformation(erp, HANDLE_FLAG_INHERIT, 0);
		if(!novc_flag) {
			SetHandleInformation(iwp, HANDLE_FLAG_INHERIT, 0);

			// Undocumented CreateProcess
			// It seems to work even though the handle size is different in x64, 
			// so I think the OS is probably doing the conversion.
			WORD size = (sizeof(long) + (3 * (sizeof(char) + sizeof(HANDLE))));
			if(si.lpReserved2 = (LPBYTE)calloc(size, 1)) {
				si.cbReserved2 = size;
				*((UNALIGNED long *)si.lpReserved2) = 3;
				char *pt = (char *)(si.lpReserved2 + sizeof(long));
				// 0x41 = FOPEN | FDEV
				*pt++ = 0x41;
				*pt++ = 0x41;
				*pt++ = 0x41;
				UNALIGNED HANDLE *handle_pt = (UNALIGNED HANDLE *)pt;
				*handle_pt++ = irp;
				*handle_pt++ = wp;
				*handle_pt = ewp;
			}
		}
		if(CreateProcess(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, dir, &si, &pi)) {
			CloseHandle(wp);
			CloseHandle(ewp);
			CloseHandle(irp);

			int pos = 0;
			Bit16u text[TEXT_MAX];
			Bit16u code;
			while(1) {
				if(!OutputReadPipe(erp, true)) {
					OutputReadPipe(rp);
				}
				CALLBACK_Idle();

				if(get_key(code)) {
					DWORD len;
					Bit16u n;
					unsigned char c = code & 0xff;
					if(c == 0x03) {
						n = 2;
						DOS_WriteFile(STDOUT, (Bit8u *)"^C", &n);
						// If the attribute CREATE_NO_WINDOW,
						// GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pi.dwProcessId);
						// does not work.
						// If the attribute CREATE_NO_WINDOW is not present,
						// the console screen will be displayed;
						// there are various problems if AllocConsole() is used to pre-generate and hide it.
						TerminateProcess(pi.hProcess, 0);
					} else {
						if(mode & NO_ECHO) {
							if(c == 0x0d) {
								c = 0x0a;
							}
							WriteFile(iwp, &c, 1, &len, 0);
						} else {
							if(c == 0x08) {
								if(pos >= 2 && (text[pos - 1] & 0xff00) == 0xf100) {
									n = 6;
									DOS_WriteFile(STDOUT, (Bit8u *)"\b\b  \b\b", &n);
									pos -= 2;
								} else if(pos >= 1) {
									n = 3;
									DOS_WriteFile(STDOUT, (Bit8u *)"\b \b", &n);
									pos--;
								}
							} else if(c != 0xf0) {
								if(c == 0x0a || c == 0x0d) {
									for(int p = 0 ; p < pos ; p++) {
										WriteFile(iwp, &text[p], 1, &len, 0);
									}
									pos = 0;

									n = 1;
									c = 0x0a;
									DOS_WriteFile(STDOUT, &c, &n);
									WriteFile(iwp, &c, 1, &len, 0);
								} else if(pos < TEXT_MAX) {
									n = 1;
									DOS_WriteFile(STDOUT, &c, &n);
									text[pos++] = code;
								}
							}
						}
					}
				}
				if(WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
					bool flag;
					do {
						flag = false;
						if(OutputReadPipe(erp, true)) {
							flag = true;
						} else if(OutputReadPipe(rp)) {
							flag = true;
						}
					} while(flag);
					break;
				}
			}
			CloseHandle(rp);
			CloseHandle(erp);
			CloseHandle(iwp);
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			result = true;
		}
		if(si.lpReserved2) {
			free(si.lpReserved2);
		}
	}
	if(mode & SET_ENV) {
		BackEnv();
	}
	return result;
}

static bool ExecuteWindowProgram(char *exename, char *param, char *dir)
{
	char cmd[256 * 2];
	STARTUPINFO si = { sizeof(si) };
	PROCESS_INFORMATION pi = { 0 };
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_SHOW;
	sprintf(cmd, "%s %s", exename, param);
	if(CreateProcess(NULL, cmd, NULL, NULL, TRUE, 0, NULL, dir, &si, &pi)) {
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		return true;
	}
	return false;
}

static void ChangeParamRealName(char *param, char *work)
{
	int pos;
	char name[256 * 2];
	char realname[256 * 2];
	char *top = work;

	while(*work != '\0') {
		if((toupper(*work) >= 'A' && toupper(*work) <= 'Z' && *(work + 1) == ':') || (*work == '\\' && *(work + 1) != '"' && *(work + 1) != '\'')) {
			char *work_top = work;
			char *param_top = param;
			if(*work == '\\') {
				while(work > top && *(work - 1) > ' ') {
					work--;
					param--;
				}
			}
			pos = 0;
			while(*work > ' ') {
				name[pos++] = *work++;
			}
			name[pos] = '\0';
			if(DOS_GetRealFileName(name, realname) && (toupper(realname[0]) >= 'A' && toupper(realname[0]) <= 'Z' && realname[1] == ':' && realname[2] == '\\')) {
				strcpy(param, realname);
				param += strlen(realname);
			} else {
				work = work_top;
				param = param_top;
				*param++ = *work++;
			}
		} else {
			*param++ = *work++;
		}
	}
	*param = '\0';
}

#endif

bool DOS_Execute(char * name,PhysPt block_pt,Bit8u flags) {
	EXE_Header head;Bitu i;
	Bit16u fhandle;Bit16u len;Bit32u pos;
	Bit16u pspseg,envseg,loadseg,memsize,readsize;
	PhysPt loadaddress;RealPt relocpt;
	Bitu headersize=0,imagesize=0;
	DOS_ParamBlock block(block_pt);

	block.LoadData();
	//Remove the loadhigh flag for the moment!
	if(flags&0x80) LOG(LOG_EXEC,LOG_ERROR)("using loadhigh flag!!!!!. dropping it");
	flags &= 0x7f;
	if (flags!=LOADNGO && flags!=OVERLAY && flags!=LOAD) {
		DOS_SetError(DOSERR_FORMAT_INVALID);
		return false;
//		E_Exit("DOS:Not supported execute mode %d for file %s",flags,name);
	}
#if defined(WIN32)
	char realname[256];
	if(DOS_GetRealFileName(name, realname)) {
		PLOADED_IMAGE image = ImageLoad(realname, NULL);
		if(image != NULL) {
			char dir[256];
			char work[256 * 2];
			char param[256 * 2];
			int no, pos = 0;
			RealPt pt = block_pt + 0x100;
			Bit8u len = mem_readb(pt++);
			while(pos < len && mem_readb(pt) == 0x20) {
				pos++;
				pt++;
			}
			no = 0;
			while(pos < len) {
				work[no++] = mem_readb(pt++);
				pos++;
			}
			work[no] = 0;
			ChangeParamRealName(param, work);
			DOS_GetRealDirName(".", dir);
			if(image->FileHeader->OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI) {
				// .NET ?
				bool novc_flag = false;
				ULONG size;
				PIMAGE_IMPORT_DESCRIPTOR import;
				if(import = (PIMAGE_IMPORT_DESCRIPTOR)ImageDirectoryEntryToData(image->MappedAddress, FALSE, IMAGE_DIRECTORY_ENTRY_IMPORT, &size)) {
					while(import->Characteristics != 0 && !novc_flag) {
						char *dll_name;
						dll_name = (char *)ImageRvaToVa(image->FileHeader, image->MappedAddress, import->Name, 0);
						if(!strcasecmp(dll_name, "mscoree.dll")) {
							PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)ImageRvaToVa(image->FileHeader, image->MappedAddress, import->OriginalFirstThunk ? import->OriginalFirstThunk : import->FirstThunk, 0);
							while(thunk->u1.Ordinal) {
								if(!IMAGE_SNAP_BY_ORDINAL(thunk->u1.Ordinal)) {
									IMAGE_IMPORT_BY_NAME *ii = (IMAGE_IMPORT_BY_NAME *)ImageRvaToVa(image->FileHeader, image->MappedAddress, thunk->u1.AddressOfData, 0);
									if(!strcasecmp(ii->Name, "_CorExeMain")) {
										novc_flag = true;
										break;
									}
								}
								thunk++;
							}
						}
						import++;
					}
				}
				ExecuteConsoleProgram(realname, param, dir, novc_flag);
			} else if(image->FileHeader->OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI) {
				ExecuteWindowProgram(realname, param, dir);
			}
			ImageUnload(image);
			return true;
		}
	}
#endif
	/* Check for EXE or COM File */
	bool iscom=false;
	if (!DOS_OpenFile(name,OPEN_READ,&fhandle)) {
		DOS_SetError(DOSERR_FILE_NOT_FOUND);
		return false;
	}
	len=sizeof(EXE_Header);
	if (!DOS_ReadFile(fhandle,(Bit8u *)&head,&len)) {
		DOS_CloseFile(fhandle);
		return false;
	}
	if (len<sizeof(EXE_Header)) {
		if (len==0) {
			/* Prevent executing zero byte files */
			DOS_SetError(DOSERR_ACCESS_DENIED);
			DOS_CloseFile(fhandle);
			return false;
		}
		/* Otherwise must be a .com file */
		iscom=true;
	} else {
		/* Convert the header to correct endian, i hope this works */
		HostPt endian=(HostPt)&head;
		for (i=0;i<sizeof(EXE_Header)/2;i++) {
			*((Bit16u *)endian)=host_readw(endian);
			endian+=2;
		}
		if ((head.signature!=MAGIC1) && (head.signature!=MAGIC2)) iscom=true;
		else {
			if(head.pages & ~0x07ff) /* 1 MB dos maximum address limit. Fixes TC3 IDE (kippesoep) */
				LOG(LOG_EXEC,LOG_NORMAL)("Weird header: head.pages > 1 MB");
			head.pages&=0x07ff;
			headersize = head.headersize*16;
			imagesize = head.pages*512-headersize; 
			if (imagesize+headersize<512) imagesize = 512-headersize;
		}
	}
	Bit8u * loadbuf=(Bit8u *)new Bit8u[0x10000];
	if (flags!=OVERLAY) {
		/* Create an environment block */
		envseg=block.exec.envseg;
		if (!MakeEnv(name,&envseg)) {
			DOS_CloseFile(fhandle);
			delete [] loadbuf;
			return false;
		}
		/* Get Memory */		
		Bit16u minsize,maxsize;Bit16u maxfree=0xffff;DOS_AllocateMemory(&pspseg,&maxfree);
		if (iscom) {
			minsize=0x1000;maxsize=0xffff;
			if (machine==MCH_PCJR) {
				/* try to load file into memory below 96k */ 
				pos=0;DOS_SeekFile(fhandle,&pos,DOS_SEEK_SET);	
				Bit16u dataread=0x1800;
				DOS_ReadFile(fhandle,loadbuf,&dataread);
				if (dataread<0x1800) maxsize=((dataread+0x10)>>4)+0x20;
				if (minsize>maxsize) minsize=maxsize;
			}
		} else {	/* Exe size calculated from header */
			minsize=long2para(imagesize+(head.minmemory<<4)+256);
			if (head.maxmemory!=0) maxsize=long2para(imagesize+(head.maxmemory<<4)+256);
			else maxsize=0xffff;
		}
		if (maxfree<minsize) {
			if (iscom) {
				/* Reduce minimum of needed memory size to filesize */
				pos=0;DOS_SeekFile(fhandle,&pos,DOS_SEEK_SET);	
				Bit16u dataread=0xf800;
				DOS_ReadFile(fhandle,loadbuf,&dataread);
				if (dataread<0xf800) minsize=((dataread+0x10)>>4)+0x20;
			}
			if (maxfree<minsize) {
				DOS_CloseFile(fhandle);
				DOS_SetError(DOSERR_INSUFFICIENT_MEMORY);
				DOS_FreeMemory(envseg);
				delete[] loadbuf;
				return false;
			}
		}
		if (maxfree<maxsize) memsize=maxfree;
		else memsize=maxsize;
		if (!DOS_AllocateMemory(&pspseg,&memsize)) E_Exit("DOS:Exec error in memory");
		if (iscom && (machine==MCH_PCJR) && (pspseg<0x2000)) {
			maxsize=0xffff;
			/* resize to full extent of memory block */
			DOS_ResizeMemory(pspseg,&maxsize);
			memsize=maxsize;
		}
		loadseg=pspseg+16;
		if (!iscom) {
			/* Check if requested to load program into upper part of allocated memory */
			if ((head.minmemory == 0) && (head.maxmemory == 0))
				loadseg = (Bit16u)(((pspseg+memsize)*0x10-imagesize)/0x10);
		}
	} else loadseg=block.overlay.loadseg;
	/* Load the executable */
	loadaddress=PhysMake(loadseg,0);

	if (iscom) {	/* COM Load 64k - 256 bytes max */
		pos=0;DOS_SeekFile(fhandle,&pos,DOS_SEEK_SET);	
		readsize=0xffff-256;
		DOS_ReadFile(fhandle,loadbuf,&readsize);
		MEM_BlockWrite(loadaddress,loadbuf,readsize);
	} else {	/* EXE Load in 32kb blocks and then relocate */
		pos=headersize;DOS_SeekFile(fhandle,&pos,DOS_SEEK_SET);	
		while (imagesize>0x7FFF) {
			readsize=0x8000;DOS_ReadFile(fhandle,loadbuf,&readsize);
			MEM_BlockWrite(loadaddress,loadbuf,readsize);
//			if (readsize!=0x8000) LOG(LOG_EXEC,LOG_NORMAL)("Illegal header");
			loadaddress+=0x8000;imagesize-=0x8000;
		}
		if (imagesize>0) {
			readsize=(Bit16u)imagesize;DOS_ReadFile(fhandle,loadbuf,&readsize);
			MEM_BlockWrite(loadaddress,loadbuf,readsize);
//			if (readsize!=imagesize) LOG(LOG_EXEC,LOG_NORMAL)("Illegal header");
		}
		/* Relocate the exe image */
		Bit16u relocate;
		if (flags==OVERLAY) relocate=block.overlay.relocation;
		else relocate=loadseg;
		pos=head.reloctable;DOS_SeekFile(fhandle,&pos,0);
		for (i=0;i<head.relocations;i++) {
			readsize=4;DOS_ReadFile(fhandle,(Bit8u *)&relocpt,&readsize);
			relocpt=host_readd((HostPt)&relocpt);		//Endianize
			PhysPt address=PhysMake(RealSeg(relocpt)+loadseg,RealOff(relocpt));
			mem_writew(address,mem_readw(address)+relocate);
		}
	}
	delete[] loadbuf;
	DOS_CloseFile(fhandle);

	/* Setup a psp */
	if (flags!=OVERLAY) {
		// Create psp after closing exe, to avoid dead file handle of exe in copied psp
		SetupPSP(pspseg,memsize,envseg);
		SetupCMDLine(pspseg,block);
	};
	CALLBACK_SCF(false);		/* Carry flag cleared for caller if successfull */
	if (flags==OVERLAY) {
		/* Changed registers */
		reg_ax=0;
		reg_dx=0;
		return true;			/* Everything done for overlays */
	}
	RealPt csip,sssp;
	if (iscom) {
		csip=RealMake(pspseg,0x100);
		if (memsize<0x1000) {
			LOG(LOG_EXEC,LOG_WARN)("COM format with only %X paragraphs available",memsize);
			sssp=RealMake(pspseg,(memsize<<4)-2);
		} else sssp=RealMake(pspseg,0xfffe);
		mem_writew(Real2Phys(sssp),0);
	} else {
		csip=RealMake(loadseg+head.initCS,head.initIP);
		sssp=RealMake(loadseg+head.initSS,head.initSP);
		if (head.initSP<4) LOG(LOG_EXEC,LOG_ERROR)("stack underflow/wrap at EXEC");
		if ((pspseg+memsize)<(RealSeg(sssp)+(RealOff(sssp)>>4)))
			LOG(LOG_EXEC,LOG_ERROR)("stack outside memory block at EXEC");
	}

	if ((flags==LOAD) || (flags==LOADNGO)) {
		/* Get Caller's program CS:IP of the stack and set termination address to that */
		RealSetVec(0x22,RealMake(real_readw(SegValue(ss),reg_sp+2),real_readw(SegValue(ss),reg_sp)));
		reg_sp-=18;
		DOS_PSP callpsp(dos.psp());
		/* Save the SS:SP on the PSP of calling program */
		callpsp.SetStack(RealMakeSeg(ss,reg_sp));
		/* Switch the psp's and set new DTA */
		dos.psp(pspseg);
		DOS_PSP newpsp(dos.psp());
		dos.dta(RealMake(newpsp.GetSegment(),0x80));
		/* save vectors */
		newpsp.SaveVectors();
		/* copy fcbs */
		newpsp.SetFCB1(block.exec.fcb1);
		newpsp.SetFCB2(block.exec.fcb2);
		/* Save the SS:SP on the PSP of new program */
		newpsp.SetStack(RealMakeSeg(ss,reg_sp));

		/* Setup bx, contains a 0xff in bl and bh if the drive in the fcb is not valid */
		DOS_FCB fcb1(RealSeg(block.exec.fcb1),RealOff(block.exec.fcb1));
		DOS_FCB fcb2(RealSeg(block.exec.fcb2),RealOff(block.exec.fcb2));
		Bit8u d1 = fcb1.GetDrive(); //depends on 0 giving the dos.default drive
		if ( (d1>=DOS_DRIVES) || !Drives[d1] ) reg_bl = 0xFF; else reg_bl = 0;
		Bit8u d2 = fcb2.GetDrive();
		if ( (d2>=DOS_DRIVES) || !Drives[d2] ) reg_bh = 0xFF; else reg_bh = 0;

		/* Write filename in new program MCB */
		char stripname[8]= { 0 };Bitu index=0;
		while (char chr=*name++) {
			switch (chr) {
			case ':':case '\\':case '/':index=0;break;
			default:if (index<8) stripname[index++]=(char)toupper(chr);
			}
		}
		index=0;
		while (index<8) {
			if (stripname[index]=='.') break;
			if (!stripname[index]) break;	
			index++;
		}
		memset(&stripname[index],0,8-index);
		DOS_MCB pspmcb(dos.psp()-1);
		pspmcb.SetFileName(stripname);
		DOS_UpdatePSPName();
	}

	if (flags==LOAD) {
		/* First word on the stack is the value ax should contain on startup */
		real_writew(RealSeg(sssp-2),RealOff(sssp-2),reg_bx);
		/* Write initial CS:IP and SS:SP in param block */
		block.exec.initsssp = sssp-2;
		block.exec.initcsip = csip;
		block.SaveData();
		/* Changed registers */
		reg_sp+=18;
		reg_ax=RealOff(csip);
		reg_bx=memsize;
		reg_dx=0;
		return true;
	}

	if (flags==LOADNGO) {
		if ((reg_sp>0xfffe) || (reg_sp<18)) LOG(LOG_EXEC,LOG_ERROR)("stack underflow/wrap at EXEC");
		/* Set the stack for new program */
		SegSet16(ss,RealSeg(sssp));reg_sp=RealOff(sssp);
		/* Add some flags and CS:IP on the stack for the IRET */
		CPU_Push16(RealSeg(csip));
		CPU_Push16(RealOff(csip));
		/* DOS starts programs with a RETF, so critical flags
		 * should not be modified (IOPL in v86 mode);
		 * interrupt flag is set explicitly, test flags cleared */
		reg_flags=(reg_flags&(~FMASK_TEST))|FLAG_IF;
		//Jump to retf so that we only need to store cs:ip on the stack
		reg_ip++;
		/* Setup the rest of the registers */
		reg_ax=reg_bx;
		reg_cx=0xff;
		reg_dx=pspseg;
		reg_si=RealOff(csip);
		reg_di=RealOff(sssp);
		reg_bp=0x91c;	/* DOS internal stack begin relict */
		SegSet16(ds,pspseg);SegSet16(es,pspseg);
#if C_DEBUG		
		/* Started from debug.com, then set breakpoint at start */
		DEBUG_CheckExecuteBreakpoint(RealSeg(csip),RealOff(csip));
#endif
		return true;
	}
	return false;
}
