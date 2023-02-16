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


#include "dosbox.h"
#include "shell.h"
#include "callback.h"
#include "regs.h"
#include "bios.h"
#include "../dos/drives.h"
#include "support.h"
#include "control.h"
#include "../ints/int10.h"
#include "dos_inc.h"
#include "jega.h"
#include "jfont.h"
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <vector>
#include <string>
#include <time.h>

extern bool LineInputFlag;

static SHELL_Cmd cmd_list[]={
{	"DIR",		0,			&DOS_Shell::CMD_DIR,		"SHELL_CMD_DIR_HELP"},
{	"CHDIR",	1,			&DOS_Shell::CMD_CHDIR,		"SHELL_CMD_CHDIR_HELP"},
{	"ATTRIB",	1,			&DOS_Shell::CMD_ATTRIB,		"SHELL_CMD_ATTRIB_HELP"},
{	"CALL",		1,			&DOS_Shell::CMD_CALL,		"SHELL_CMD_CALL_HELP"},
{	"CD",		0,			&DOS_Shell::CMD_CHDIR,		"SHELL_CMD_CHDIR_HELP"},
{	"CHCP",		1,			&DOS_Shell::CMD_CHCP,		"SHELL_CMD_CHCP_HELP" },
{	"CHOICE",	1,			&DOS_Shell::CMD_CHOICE,		"SHELL_CMD_CHOICE_HELP"},
{	"CLS",		0,			&DOS_Shell::CMD_CLS,		"SHELL_CMD_CLS_HELP"},
{	"COPY",		0,			&DOS_Shell::CMD_COPY,		"SHELL_CMD_COPY_HELP"},
{	"DATE",		0,			&DOS_Shell::CMD_DATE,		"SHELL_CMD_DATE_HELP"},
{	"DEL",		0,			&DOS_Shell::CMD_DELETE,		"SHELL_CMD_DELETE_HELP"},
{	"DELETE",	1,			&DOS_Shell::CMD_DELETE,		"SHELL_CMD_DELETE_HELP"},
{	"ERASE",	1,			&DOS_Shell::CMD_DELETE,		"SHELL_CMD_DELETE_HELP"},
{	"ECHO",		1,			&DOS_Shell::CMD_ECHO,		"SHELL_CMD_ECHO_HELP"},
{	"EXIT",		0,			&DOS_Shell::CMD_EXIT,		"SHELL_CMD_EXIT_HELP"},	
{	"GOTO",		1,			&DOS_Shell::CMD_GOTO,		"SHELL_CMD_GOTO_HELP"},
{	"HELP",		1,			&DOS_Shell::CMD_HELP,		"SHELL_CMD_HELP_HELP"},
{	"FOR",		1,			&DOS_Shell::CMD_FOR,		"SHELL_CMD_FOR_HELP"},
{	"IF",		1,			&DOS_Shell::CMD_IF,			"SHELL_CMD_IF_HELP"},
{	"LOADHIGH",	1,			&DOS_Shell::CMD_LOADHIGH, 	"SHELL_CMD_LOADHIGH_HELP"},
{	"LH",		1,			&DOS_Shell::CMD_LOADHIGH,	"SHELL_CMD_LOADHIGH_HELP"},
{	"MKDIR",	1,			&DOS_Shell::CMD_MKDIR,		"SHELL_CMD_MKDIR_HELP"},
{	"MD",		0,			&DOS_Shell::CMD_MKDIR,		"SHELL_CMD_MKDIR_HELP"},
{	"PATH",		1,			&DOS_Shell::CMD_PATH,		"SHELL_CMD_PATH_HELP"},
{	"PAUSE",	1,			&DOS_Shell::CMD_PAUSE,		"SHELL_CMD_PAUSE_HELP"},
{	"PROMPT",	1,			&DOS_Shell::CMD_PROMPT,		"SHELL_CMD_PROMPT_HELP"},
{	"RMDIR",	1,			&DOS_Shell::CMD_RMDIR,		"SHELL_CMD_RMDIR_HELP"},
{	"RD",		0,			&DOS_Shell::CMD_RMDIR,		"SHELL_CMD_RMDIR_HELP"},
{	"REM",		1,			&DOS_Shell::CMD_REM,		"SHELL_CMD_REM_HELP"},
{	"RENAME",	1,			&DOS_Shell::CMD_RENAME,		"SHELL_CMD_RENAME_HELP"},
{	"REN",		0,			&DOS_Shell::CMD_RENAME,		"SHELL_CMD_RENAME_HELP"},
{	"SET",		1,			&DOS_Shell::CMD_SET,		"SHELL_CMD_SET_HELP"},
{	"SHIFT",	1,			&DOS_Shell::CMD_SHIFT,		"SHELL_CMD_SHIFT_HELP"},
{	"SUBST",	1,			&DOS_Shell::CMD_SUBST,		"SHELL_CMD_SUBST_HELP"},
{	"TIME",		0,			&DOS_Shell::CMD_TIME,		"SHELL_CMD_TIME_HELP"},
{	"TYPE",		0,			&DOS_Shell::CMD_TYPE,		"SHELL_CMD_TYPE_HELP"},
{	"VER",		0,			&DOS_Shell::CMD_VER,		"SHELL_CMD_VER_HELP"},
{	"CHEV",		0,			&DOS_Shell::CMD_CHEV,		"SHELL_CMD_CHEV_HELP"},
{	"BREAK",	0,			&DOS_Shell::CMD_BREAK,		"SHELL_CMD_BREAK_HELP"},
{0,0,0,0}
}; 

/* support functions */
static char empty_char = 0;
static char* empty_string = &empty_char;
static void StripSpaces(char*&args) {
	while(args && *args && isspace(*reinterpret_cast<unsigned char*>(args)))
		args++;
}

static void StripSpaces(char*&args,char also) {
	while(args && *args && (isspace(*reinterpret_cast<unsigned char*>(args)) || (*args == also)))
		args++;
}

static char* ExpandDot(char*args, char* buffer , size_t bufsize) {
	if(*args == '.') {
		if(*(args+1) == 0){
			safe_strncpy(buffer, "*.*", bufsize);
			return buffer;
		}
		if( (*(args+1) != '.') && (*(args+1) != '\\') ) {
			buffer[0] = '*';
			buffer[1] = 0;
			if (bufsize > 2) strncat(buffer,args,bufsize - 1 /*used buffer portion*/ - 1 /*trailing zero*/  );
			return buffer;
		} else
			safe_strncpy (buffer, args, bufsize);
	}
	else safe_strncpy(buffer,args, bufsize);
	return buffer;
}



bool DOS_Shell::CheckConfig(char* cmd_in,char*line) {
	Section* test = control->GetSectionFromProperty(cmd_in);
	if(!test) return false;
	if(line && !line[0]) {
		std::string val = test->GetPropValue(cmd_in);
		if(val != NO_SUCH_PROPERTY) WriteOut("%s\n",val.c_str());
		return true;
	}
	char newcom[1024]; newcom[0] = 0; strcpy(newcom,"z:\\config -set ");
	strcat(newcom,test->GetName());	strcat(newcom," ");
	strcat(newcom,cmd_in);strcat(newcom,line);
	DoCommand(newcom);
	return true;
}

extern Bitu autoreload;

void DOS_Shell::DoCommand(char * line) {
	if(autoreload & AUTORELOAD_CMD) {
		Bit8u drive = DOS_GetDefaultDrive();
		if(drive < DOS_DRIVES) {
			if(Drives[drive]) {
				Drives[drive]->EmptyCache();
			}
		}
	}
/* First split the line into command and arguments */
	line=trim(line);
	char cmd_buffer[CMD_MAXLINE];
	char * cmd_write=cmd_buffer;
	while (*line) {
		if (*line == 32) break;
		if (*line == '/') break;
		if (*line == '\t') break;
		if (*line == '=') break;
//		if (*line == ':') break; //This breaks drive switching as that is handled at a later stage. 
		if ((*line == '.') ||(*line == '\\')) {  //allow stuff like cd.. and dir.exe cd\kees
			*cmd_write=0;
			Bit32u cmd_index=0;
			while (cmd_list[cmd_index].name) {
				if (strcasecmp(cmd_list[cmd_index].name,cmd_buffer)==0) {
					dos.save_dta();
					(this->*(cmd_list[cmd_index].handler))(line);
					dos.restore_dta();
			 		return;
				}
				cmd_index++;
			}
		}
		*cmd_write++=*line++;
	}
	*cmd_write=0;
	if (strlen(cmd_buffer)==0) return;
/* Check the internal list */
	Bit32u cmd_index=0;
	while (cmd_list[cmd_index].name) {
		if (strcasecmp(cmd_list[cmd_index].name,cmd_buffer)==0) {
			dos.save_dta();
			(this->*(cmd_list[cmd_index].handler))(line);
			dos.restore_dta();
			return;
		}
		cmd_index++;
	}
/* This isn't an internal command execute it */
	if (strchr(cmd_buffer,'\"')) {
		int k=0;
		for (int i=0;i<(int)strlen(cmd_buffer);i++)
			if (cmd_buffer[i]!='\"')
				cmd_buffer[k++]=cmd_buffer[i];
			cmd_buffer[k]=0;
	}
	if(Execute(cmd_buffer,line)) return;
	if(CheckConfig(cmd_buffer,line)) return;
	WriteOut(MSG_Get("SHELL_EXECUTE_ILLEGAL_COMMAND"),cmd_buffer);
}

#define HELP(command) \
	if (ScanCMDBool(args,"?")) { \
		WriteOut(MSG_Get("SHELL_CMD_" command "_HELP")); \
		const char* long_m = MSG_Get("SHELL_CMD_" command "_HELP_LONG"); \
		WriteOut("\n"); \
		if(strcmp("Message not Found!\n",long_m)) WriteOut(long_m); \
		else WriteOut(command "\n"); \
		return; \
	}

void DOS_Shell::CMD_CLS(char * args) {
	HELP("CLS");
	reg_ah = 0x00;
	reg_al = real_readb(BIOSMEM_SEG, BIOSMEM_CURRENT_MODE);
	if(reg_al == 0x70) {
		reg_al = GetTrueVideoMode();
	}
	CALLBACK_RunRealInt(0x10);
}

void DOS_Shell::CMD_DELETE(char * args) {
	HELP("DELETE");
	/* Command uses dta so set it to our internal dta */
	RealPt save_dta=dos.dta();
	dos.dta(dos.tables.tempdta);

	char * rem=ScanCMDRemain(args);
	if (rem) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_SWITCH"),rem);
		return;
	}
	/* If delete accept switches mind the space infront of them. See the dir /p code */ 

	char full[DOS_PATHLENGTH],sfull[DOS_PATHLENGTH+2];
	char buffer[CROSS_LEN];
	args = ExpandDot(args,buffer, CROSS_LEN);
	StripSpaces(args);
	if (!DOS_Canonicalize(args,full)) { WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));return; }
	Bit16u fattr;
	if (DOS_GetFileAttr(full, &fattr) && (fattr&DOS_ATTR_DIRECTORY)) {
		strcat(args, *args && args[strlen(args)-1]=='\\'?"*.*":"\\*.*");
		strcat(full, *full && full[strlen(full)-1]=='\\'?"*.*":"\\*.*");
	}
	char spath[DOS_PATHLENGTH],sargs[DOS_PATHLENGTH];
	if (!DOS_GetSFNPath(args,spath,false)) {
		WriteOut(MSG_Get("SHELL_CMD_DEL_ERROR"),args);
		return;
	}
	sprintf(sargs,"\"%s\"",spath);
	bool res=DOS_FindFirst(sargs,0xffff & ~DOS_ATTR_VOLUME);
	if (!res) {
		WriteOut(MSG_Get("SHELL_CMD_DEL_ERROR"),args);
		dos.dta(save_dta);
		return;
	}
	char *p=strrchr_dbcs(full,'\\');
	if ((!strcmp(full,"*.*") || uselfn && !strcmp(full,"*") || p!=NULL && (!strcmp(p+1,"*.*") || uselfn && !strcmp(p+1,"*")))) {
		WriteOut("Are you sure to delete all files in directory (Y/N)?");
		Bit8u c;
		Bit16u n=1;
		DOS_ReadFile (STDIN,&c,&n);
		if (c==3) {WriteOut("^C\n");return;}
		c = c=='y'||c=='Y' ? 'Y':'N';
		WriteOut("%c\n", c);
		if (c=='N') return;
	}
	//end can't be 0, but if it is we'll get a nice crash, who cares :)
	char * end=strrchr_dbcs(full,'\\')+1;*end=0;
	char name[DOS_NAMELENGTH_ASCII],lname[LFN_NAMELENGTH+1];
	Bit32u size;Bit16u time,date;Bit8u attr;
	DOS_DTA dta(dos.dta());
	while (res) {
		dta.GetResult(name,lname,size,date,time,attr);	
		if (!(attr & (DOS_ATTR_DIRECTORY|DOS_ATTR_HIDDEN|DOS_ATTR_SYSTEM))) {
			strcpy(end,name);
			strcpy(sfull,full);
			if (uselfn) sprintf(sfull,"\"%s\"",full);
			if (!DOS_UnlinkFile(sfull)) WriteOut(MSG_Get("SHELL_CMD_DEL_ERROR"),full);
		}
		res=DOS_FindNext();
	}
	dos.dta(save_dta);
}

void DOS_Shell::CMD_HELP(char * args){
	HELP("HELP");
	bool optall=ScanCMDBool(args,"ALL");
	/* Print the help */
	if(!optall) WriteOut(MSG_Get("SHELL_CMD_HELP"));
	Bit32u cmd_index=0,write_count=0;
	while (cmd_list[cmd_index].name) {
		if (optall || !cmd_list[cmd_index].flags) {
			WriteOut("<\033[34;1m%-8s\033[0m> %s",cmd_list[cmd_index].name,MSG_Get(cmd_list[cmd_index].help));
			if(!(++write_count%22)) CMD_PAUSE(empty_string);
		}
		cmd_index++;
	}
}

void DOS_Shell::CMD_RENAME(char * args){
	HELP("RENAME");
	StripSpaces(args);
	if (!*args) {SyntaxError();return;}
	if ((strchr(args,'*')!=NULL) || (strchr(args,'?')!=NULL) ) { WriteOut(MSG_Get("SHELL_CMD_NO_WILD"));return;}
	char * arg1=StripArg(args);
	StripSpaces(args);
	if (!*args) {SyntaxError();return;}
	char* slash = strrchr_dbcs(arg1,'\\');
	if (slash) { 
		/* If directory specified (crystal caves installer)
		 * rename from c:\X : rename c:\abc.exe abc.shr. 
		 * File must appear in C:\ 
		 * Ren X:\A\B C => ren X:\A\B X:\A\C */ 
		
		char dir_source[DOS_PATHLENGTH + 4] = {0}; //not sure if drive portion is included in pathlength
		//Copy first and then modify, makes GCC happy
		safe_strncpy(dir_source,arg1,DOS_PATHLENGTH + 4);
		char* dummy = strrchr_dbcs(dir_source,'\\');
		if (!dummy) { //Possible due to length
			WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
			return;
		}
		dummy++;
		*dummy = 0;

		//Maybe check args for directory, as I think that isn't allowed

		//dir_source and target are introduced for when we support multiple files being renamed.
		char target[DOS_PATHLENGTH+CROSS_LEN + 5] = {0};
		strcpy(target,dir_source);
		strncat(target,args,CROSS_LEN);

		if (!DOS_Rename(arg1,target))
			WriteOut(MSG_Get("SHELL_CMD_RENAME_ERROR"),arg1);

	} else {
		if (!DOS_Rename(arg1,args))
			WriteOut(MSG_Get("SHELL_CMD_RENAME_ERROR"),arg1);
	}
}

void DOS_Shell::CMD_ECHO(char * args){
	if (!*args) {
		if (echo) { WriteOut(MSG_Get("SHELL_CMD_ECHO_ON"));}
		else { WriteOut(MSG_Get("SHELL_CMD_ECHO_OFF"));}
		return;
	}
	char buffer[512];
	char* pbuffer = buffer;
	safe_strncpy(buffer,args,512);
	StripSpaces(pbuffer);
	if (strcasecmp(pbuffer,"OFF")==0) {
		echo=false;		
		return;
	}
	if (strcasecmp(pbuffer,"ON")==0) {
		echo=true;		
		return;
	}
	if(strcasecmp(pbuffer,"/?")==0) { HELP("ECHO"); }

	args++;//skip first character. either a slash or dot or space
	size_t len = strlen(args); //TODO check input of else ook nodig is.
	if(len && args[len - 1] == '\r') {
		LOG(LOG_MISC,LOG_WARN)("Hu ? carriage return already present. Is this possible?");
		WriteOut("%s\n",args);
	} else WriteOut("%s\r\n",args);
}


void DOS_Shell::CMD_EXIT(char * args) {
	HELP("EXIT");
	exit = true;
}

void DOS_Shell::CMD_CHDIR(char * args) {
	HELP("CHDIR");
	StripSpaces(args);
	char sargs[CROSS_LEN];
	if (*args && !DOS_GetSFNPath(args,sargs,false)) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
		return;
	}
	Bit8u drive = DOS_GetDefaultDrive()+'A';
	char dir[DOS_PATHLENGTH];
	if (!*args) {
		DOS_GetCurrentDir(0,dir,true);
		WriteOut("%c:\\%s\n",drive,dir);
	} else if(strlen(args) == 2 && args[1]==':') {
		Bit8u targetdrive = (args[0] | 0x20)-'a' + 1;
		unsigned char targetdisplay = *reinterpret_cast<unsigned char*>(&args[0]);
		if(!DOS_GetCurrentDir(targetdrive,dir,true)) {
			if(drive == 'Z') {
				WriteOut(MSG_Get("SHELL_EXECUTE_DRIVE_NOT_FOUND"),toupper(targetdisplay));
			} else {
				WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
			}
			return;
		}
		WriteOut("%c:\\%s\n",toupper(targetdisplay),dir);
		if(drive == 'Z')
			WriteOut(MSG_Get("SHELL_CMD_CHDIR_HINT"),toupper(targetdisplay));
	} else 	if (!DOS_ChangeDir(sargs)) {
		/* Changedir failed. Check if the filename is longer then 8 and/or contains spaces */
	   
		std::string temps(args),slashpart;
		std::string::size_type separator = temps.find_first_of("\\/");
		if(!separator) {
			slashpart = temps.substr(0,1);
			temps.erase(0,1);
		}
		separator = temps.find_first_of("\\/");
		if(separator != std::string::npos) temps.erase(separator);
		separator = temps.find_first_of("\"");
		if(separator != std::string::npos) temps.erase(separator);
		separator = temps.rfind('.');
		if(separator != std::string::npos) temps.erase(separator);
		separator = temps.find(' ');
		if(separator != std::string::npos) {/* Contains spaces */
			temps.erase(separator);
			if(temps.size() >6) temps.erase(6);
			temps += "~1";
			WriteOut(MSG_Get("SHELL_CMD_CHDIR_HINT_2"),temps.insert(0,slashpart).c_str());
		} else {
			if (drive == 'Z') {
				WriteOut(MSG_Get("SHELL_CMD_CHDIR_HINT_3"));
			} else {
				WriteOut(MSG_Get("SHELL_CMD_CHDIR_ERROR"),args);
			}
		}
	}
}

void DOS_Shell::CMD_MKDIR(char * args) {
	HELP("MKDIR");
	StripSpaces(args);
	char * rem=ScanCMDRemain(args);
	if (rem) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_SWITCH"),rem);
		return;
	}
	if (!DOS_MakeDir(args)) {
		WriteOut(MSG_Get("SHELL_CMD_MKDIR_ERROR"),args);
	}
}

void DOS_Shell::CMD_RMDIR(char * args) {
	HELP("RMDIR");
	StripSpaces(args);
	char * rem=ScanCMDRemain(args);
	if (rem) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_SWITCH"),rem);
		return;
	}
	if (!DOS_RemoveDir(args)) {
		WriteOut(MSG_Get("SHELL_CMD_RMDIR_ERROR"),args);
	}
}

static void FormatNumber(Bit32u num,char * buf) {
	Bit32u numm,numk,numb,numg;
	numb=num % 1000;
	num/=1000;
	numk=num % 1000;
	num/=1000;
	numm=num % 1000;
	num/=1000;
	numg=num;
	if (numg) {
		sprintf(buf,"%d,%03d,%03d,%03d",numg,numm,numk,numb);
		return;
	};
	if (numm) {
		sprintf(buf,"%d,%03d,%03d",numm,numk,numb);
		return;
	};
	if (numk) {
		sprintf(buf,"%d,%03d",numk,numb);
		return;
	};
	sprintf(buf,"%d",numb);
}	

extern bool check_key(Bit16u &code);
extern bool get_key(Bit16u &code);

void DOS_Shell::CMD_DIR(char * args) {
	HELP("DIR");
	char numformat[16];
	char path[DOS_PATHLENGTH];
	char sargs[CROSS_LEN];

	std::string line;
	if(GetEnvStr("DIRCMD",line)){
		std::string::size_type idx = line.find('=');
		std::string value=line.substr(idx +1 , std::string::npos);
		line = std::string(args) + " " + value;
		args=const_cast<char*>(line.c_str());
	}
   
	bool optW=ScanCMDBool(args,"W");
	ScanCMDBool(args,"S");
	bool optP=ScanCMDBool(args,"P");
	if (ScanCMDBool(args,"WP") || ScanCMDBool(args,"PW")) {
		optW=optP=true;
	}
	bool optB=ScanCMDBool(args,"B");
	bool optA=ScanCMDBool(args,"A");
	bool optAD=ScanCMDBool(args,"AD");
	char * rem=ScanCMDRemain(args);
	if (rem) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_SWITCH"),rem);
		return;
	}
	Bit32u byte_count,file_count,dir_count;
	Bitu w_count=0;
	Bitu p_count=0;
	Bitu w_size = optW?5:1;
	byte_count=file_count=dir_count=0;

	char buffer[CROSS_LEN];
	args = trim(args);
	size_t argLen = strlen(args);
	if (argLen == 0) {
		strcpy(args,"*.*"); //no arguments.
	} else {
		// handle \, C:\, etc.                          handle C:, etc.
		if(check_last_split_char(args, argLen, '\\') || args[argLen-1] == ':') {
			strcat(args,"*.*");
		}
	}
	args = ExpandDot(args,buffer,CROSS_LEN);

	if (!strrchr(args,'*') && !strrchr(args,'?')) {
		Bit16u attribute=0;
		if(!DOS_GetSFNPath(args,sargs,false)) {
			WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
			return;
		}
		if(DOS_GetFileAttr(sargs,&attribute) && (attribute&DOS_ATTR_DIRECTORY) ) {
			DOS_FindFirst(sargs,0xffff & ~DOS_ATTR_VOLUME);
			DOS_DTA dta(dos.dta());
			strcpy(args,sargs);
			strcat(args,"\\*.*");	// if no wildcard and a directory, get its files
		}
	}
	if (!DOS_GetSFNPath(args,sargs,false)) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
		return;
	}
	sprintf(args,"\"%s\"",sargs);
	if (!strrchr(args,'.')) {
		strcat(args,".*");	// if no extension, get them all
	}

	/* Make a full path in the args */
	if (!DOS_Canonicalize(args,path)) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
		return;
	}
	*(strrchr_dbcs(path,'\\')+1)=0;
	if (!DOS_GetSFNPath(path,sargs,true)) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
		return;
	}
	if (*(sargs+strlen(sargs)-1) != '\\') strcat(sargs,"\\");
	if (!optB) WriteOut(MSG_Get("SHELL_CMD_DIR_INTRO"),sargs);

	/* Command uses dta so set it to our internal dta */
	RealPt save_dta=dos.dta();
	dos.dta(dos.tables.tempdta);
	DOS_DTA dta(dos.dta());
	bool ret=DOS_FindFirst(args,0xffff & ~DOS_ATTR_VOLUME);
	if (!ret) {
		if (!optB) WriteOut(MSG_Get("SHELL_CMD_FILE_NOT_FOUND"),args);
		dos.dta(save_dta);
		return;
	}

	Bitu cr_count = mem_readb(BIOS_SCREEN_COLUMNS) / 16;
 
	Bit16u code;
	do {    /* File name and extension */
		char name[DOS_NAMELENGTH_ASCII], lname[LFN_NAMELENGTH+1];
		Bit32u size;Bit16u date;Bit16u time;Bit8u attr;
		dta.GetResult(name,lname,size,date,time,attr);

		/* Skip non-directories if option AD is present */
		if(optAD && !(attr&DOS_ATTR_DIRECTORY) ) continue;
		
		/* output the file */
		if (optB) {
			// this overrides pretty much everything
			if (strcmp(".",uselfn?lname:name) && strcmp("..",uselfn?lname:name)) {
				WriteOut("%s\n",uselfn?lname:name);
			}
		} else {
			char * ext = empty_string;
			if (!optW && (name[0] != '.')) {
				ext = strrchr(name, '.');
				if (!ext) ext = empty_string;
				else *ext++ = 0;
			}
			Bit8u day	= (Bit8u)(date & 0x001f);
			Bit8u month	= (Bit8u)((date >> 5) & 0x000f);
			Bit16u year = (Bit16u)((date >> 9) + 1980);
			Bit8u hour	= (Bit8u)((time >> 5 ) >> 6);
			Bit8u minute = (Bit8u)((time >> 5) & 0x003f);


			if (optW) {
				w_count++;
			}
			if (attr & DOS_ATTR_DIRECTORY) {
				if (optW) {
					WriteOut("[%s]",name);
					size_t namelen = strlen(name);
					if (namelen <= 14) {
						for (size_t i=14-namelen;i>0;i--) {
							if((w_count % cr_count) == 0 && i == 1) {
								WriteOut("\n");
							} else {
								WriteOut(" ");
							}
						}
					}
				} else {
					if(IS_DOS_JAPANESE) {
						WriteOut("%-8s %-3s   %-16s %04d-%02d-%02d %2d:%02d %s\n",name,ext,"<DIR>",year,month,day,hour,minute,uselfn?lname:"");
					} else {
						WriteOut("%-8s %-3s   %-16s %02d-%02d-%04d %2d:%02d %s\n",name,ext,"<DIR>",day,month,year,hour,minute,uselfn?lname:"");
					}
				}
				dir_count++;
			} else {
				if (optW) {
					if((w_count % cr_count) == 0) {
						WriteOut("%-15s\n",name);
					} else {
						WriteOut("%-16s",name);
					}
				} else {
					FormatNumber(size,numformat);
					if(IS_DOS_JAPANESE) {
						WriteOut("%-8s %-3s   %16s %04d-%02d-%02d %2d:%02d %s\n",name,ext,numformat,year,month,day,hour,minute,uselfn?lname:"");
					} else {
						WriteOut("%-8s %-3s   %16s %02d-%02d-%04d %2d:%02d %s\n",name,ext,numformat,day,month,year,hour,minute,uselfn?lname:"");
					}
				}
				file_count++;
				byte_count+=size;
			}
		}
		if (optP && !(++p_count%(22*w_size))) {
			WriteOut(MSG_Get("SHELL_CMD_PAUSE"));
			Bit8u c;Bit16u n=1;
			DOS_ReadFile(STDIN,&c,&n);
			if (c==3) {WriteOut("^C\n");dos.dta(save_dta);return;}
			if (c==0) DOS_ReadFile(STDIN,&c,&n); // read extended key
		}
		if(check_key(code)) {
			if(code == 0x2e03) {
				get_key(code);
				WriteOut("^C\n");
				dos.dta(save_dta);
				return;
			}
		}
	} while ( (ret=DOS_FindNext()) );
	if (optW) {
		if (w_count % cr_count) {
			WriteOut("\n");
		}
	}
	if (!optB) {
		/* Show the summary of results */
		FormatNumber(byte_count,numformat);
		WriteOut(MSG_Get("SHELL_CMD_DIR_BYTES_USED"),file_count,numformat);
		Bit8u drive=dta.GetSearchDrive();
		//TODO Free Space
		Bitu free_space=1024*1024*100;
		if (Drives[drive]) {
			Bit16u bytes_sector;Bit8u sectors_cluster;Bit16u total_clusters;Bit16u free_clusters;
			Drives[drive]->AllocationInfo(&bytes_sector,&sectors_cluster,&total_clusters,&free_clusters);
			free_space=bytes_sector*sectors_cluster*free_clusters;
		}
		FormatNumber(free_space,numformat);
		WriteOut(MSG_Get("SHELL_CMD_DIR_BYTES_FREE"),dir_count,numformat);
	}
	dos.dta(save_dta);
}

struct copysource {
	std::string filename;
	bool concat;
	copysource(std::string filein,bool concatin):
		filename(filein),concat(concatin){ };
	copysource():filename(""),concat(false){ };
};


void DOS_Shell::CMD_COPY(char * args) {
	HELP("COPY");
	static char defaulttarget[] = ".";
	StripSpaces(args);
	/* Command uses dta so set it to our internal dta */
	RealPt save_dta=dos.dta();
	dos.dta(dos.tables.tempdta);
	DOS_DTA dta(dos.dta());
	Bit32u size;Bit16u date;Bit16u time;Bit8u attr;
	char name[DOS_NAMELENGTH_ASCII], lname[LFN_NAMELENGTH+1];
	std::vector<copysource> sources;
	// ignore /b and /t switches: always copy binary
	while(ScanCMDBool(args,"B")) ;
	while(ScanCMDBool(args,"T")) ; //Shouldn't this be A ?
	while(ScanCMDBool(args,"A")) ;
	ScanCMDBool(args,"Y");
	ScanCMDBool(args,"-Y");
	ScanCMDBool(args,"V");

	char * rem=ScanCMDRemain(args);
	if (rem) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_SWITCH"),rem);
		dos.dta(save_dta);
		return;
	}
	// Gather all sources (extension to copy more then 1 file specified at command line)
	// Concatenating files go as follows: All parts except for the last bear the concat flag.
	// This construction allows them to be counted (only the non concat set)
	char q[]="\"";
	char* source_p = NULL;
	char source_x[DOS_PATHLENGTH+CROSS_LEN];
	while ( (source_p = StripArg(args)) && *source_p ) {
		do {
			char* plus = strchr(source_p,'+');
			// If StripWord() previously cut at a space before a plus then
			// set concatenate flag on last source and remove leading plus.
			if (plus == source_p && sources.size()) {
				sources[sources.size()-1].concat = true;
				// If spaces also followed plus then item is only a plus.
				if (strlen(++source_p)==0) break;
				plus = strchr(source_p,'+');
			}
			if (plus) *plus++ = 0;
			safe_strncpy(source_x,source_p,CROSS_LEN);
			bool has_drive_spec = false;
			size_t source_x_len = strlen(source_x);
			if (source_x_len>0) {
				if (source_x[source_x_len-1]==':') has_drive_spec = true;
			}
			if (!has_drive_spec  && !strpbrk(source_p,"*?") ) { //doubt that fu*\*.* is valid
				char spath[DOS_PATHLENGTH];
				if (DOS_GetSFNPath(source_p,spath,false) && DOS_FindFirst(spath,0xffff & ~DOS_ATTR_VOLUME)) {
					dta.GetResult(name,lname,size,date,time,attr);
					if (attr & DOS_ATTR_DIRECTORY)
						strcat(source_x,"\\*.*");
				}
			}
			sources.push_back(copysource(source_x,(plus)?true:false));
			source_p = plus;
		} while(source_p && *source_p);
	}
	// At least one source has to be there
	if (!sources.size() || !sources[0].filename.size()) {
		WriteOut(MSG_Get("SHELL_MISSING_PARAMETER"));
		dos.dta(save_dta);
		return;
	};

	copysource target;
	// If more then one object exists and last target is not part of a 
	// concat sequence then make it the target.
	if(sources.size()>1 && !sources[sources.size()-2].concat){
		target = sources.back();
		sources.pop_back();
	}
	//If no target => default target with concat flag true to detect a+b+c
	if(target.filename.size() == 0) target = copysource(defaulttarget,true);

	copysource oldsource;
	copysource source;
	Bit32u count = 0;
	while(sources.size()) {
		/* Get next source item and keep track of old source for concat start end */
		oldsource = source;
		source = sources[0];
		sources.erase(sources.begin());

		//Skip first file if doing a+b+c. Set target to first file
		if(!oldsource.concat && source.concat && target.concat) {
			target = source;
			continue;
		}

		/* Make a full path in the args */
		char pathSourcePre[DOS_PATHLENGTH], pathSource[DOS_PATHLENGTH+2];
		char pathTarget[DOS_PATHLENGTH];

		if (!DOS_Canonicalize(const_cast<char*>(source.filename.c_str()),pathSourcePre)) {
			WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
			dos.dta(save_dta);
			return;
		}
		strcpy(pathSource,pathSourcePre);
		if (uselfn) sprintf(pathSource,"\"%s\"",pathSourcePre);
		// cut search pattern
		char* pos = strrchr_dbcs(pathSource,'\\');
		if (pos) *(pos+1) = 0;

		if (!DOS_Canonicalize(const_cast<char*>(target.filename.c_str()),pathTarget)) {
			WriteOut(MSG_Get("SHELL_ILLEGAL_PATH"));
			dos.dta(save_dta);
			return;
		}
		char* temp = strstr(pathTarget,"*.*");
		if(temp) *temp = 0;//strip off *.* from target
	
		// add '\\' if target is a directory
		bool target_is_file = true;
		if (pathTarget[strlen(pathTarget)-1]!='\\') {
			if (DOS_FindFirst(pathTarget,0xffff & ~DOS_ATTR_VOLUME)) {
				dta.GetResult(name,lname,size,date,time,attr);
				if (attr & DOS_ATTR_DIRECTORY) {
					strcat(pathTarget,"\\");
					target_is_file = false;
				}
			}
		} else target_is_file = false;

		//Find first sourcefile
		char sPath[DOS_PATHLENGTH];
		bool ret = DOS_GetSFNPath(source.filename.c_str(),sPath,false) && DOS_FindFirst(const_cast<char*>(sPath),0xffff & ~DOS_ATTR_VOLUME);
		if (!ret) {
			WriteOut(MSG_Get("SHELL_CMD_FILE_NOT_FOUND"),const_cast<char*>(source.filename.c_str()));
			dos.dta(save_dta);
			return;
		}

		Bit16u sourceHandle,targetHandle;
		char nameTarget[DOS_PATHLENGTH];
		char nameSource[DOS_PATHLENGTH];
		
		bool echo=dos.echo;
		bool second_file_of_current_source = false;
		while (ret) {
			dta.GetResult(name,lname,size,date,time,attr);

			if ((attr & DOS_ATTR_DIRECTORY)==0) {
				strcpy(nameSource,pathSource);
				strcat(nameSource,name);
				// Open Source
				if (DOS_OpenFile(nameSource,0,&sourceHandle)) {
					// Create Target or open it if in concat mode
					strcpy(nameTarget,q);
					strcat(nameTarget,pathTarget);
					if (nameTarget[strlen(nameTarget)-1]=='\\') strcat(nameTarget,uselfn?lname:name);
					strcat(nameTarget,q);

					Bit16u fattr;
					bool exist = DOS_GetFileAttr(nameTarget, &fattr);
					//Special variable to ensure that copy * a_file, where a_file is not a directory concats.
					bool special = second_file_of_current_source && target_is_file;
					second_file_of_current_source = true; 
					if (special) oldsource.concat = true;
					//Don't create a new file when in concat mode
					if (oldsource.concat || DOS_CreateFile(nameTarget,0,&targetHandle)) {
						Bit32u dummy=0;
						//In concat mode. Open the target and seek to the eof
						if (!oldsource.concat || (DOS_OpenFile(nameTarget,OPEN_READWRITE,&targetHandle) && 
					        	                  DOS_SeekFile(targetHandle,&dummy,DOS_SEEK_END))) {
							// Copy 
							static Bit8u buffer[0x8000]; // static, otherwise stack overflow possible.
							bool	failed = false;
							Bit16u	toread = 0x8000;
							bool iscon=DOS_FindDevice(name)==DOS_FindDevice("con");
							if (iscon) {
								dos.echo = true;
								LineInputFlag = true;
							}
							bool cont;
							do {
								if (!DOS_ReadFile(sourceHandle,buffer,&toread)) failed = true;
								if (iscon) {
									if(dos.errorcode == 77) {
										WriteOut("^C\r\n");
										dos.dta(save_dta);
										DOS_CloseFile(sourceHandle);
										DOS_CloseFile(targetHandle);
										if (!exist) DOS_UnlinkFile(nameTarget);
										dos.echo=echo;
										return;
									}
									cont = true;
									for(int i = 0 ; i < toread ; i++) {
										if(buffer[i] == 26) {
											toread = i;
											cont = false;
											break;
										}
									}
									if(!DOS_WriteFile(targetHandle,buffer,&toread)) failed = true;
									if(cont) toread = 0x8000;
								} else {
									if (!DOS_WriteFile(targetHandle,buffer,&toread)) failed = true;
									cont = toread == 0x8000;
								}
							} while (cont);
							if (!DOS_CloseFile(sourceHandle)) failed=true;
							if (!DOS_CloseFile(targetHandle)) failed=true;
							if (failed) {
								WriteOut(MSG_Get("SHELL_CMD_COPY_ERROR"),uselfn?lname:name);
							} else if (strcmp(name,lname)&&uselfn) {
								WriteOut(" %s [%s]\n",lname,name);
							} else {
								WriteOut(" %s\n",uselfn?lname:name);
							}
							if(!source.concat && !special) count++; //Only count concat files once
							LineInputFlag = false;
						} else {
							DOS_CloseFile(sourceHandle);
							WriteOut(MSG_Get("SHELL_CMD_COPY_FAILURE"),const_cast<char*>(target.filename.c_str()));
						}
					} else {
						DOS_CloseFile(sourceHandle);
						WriteOut(MSG_Get("SHELL_CMD_COPY_FAILURE"),const_cast<char*>(target.filename.c_str()));
					}
				} else WriteOut(MSG_Get("SHELL_CMD_COPY_FAILURE"),const_cast<char*>(source.filename.c_str()));
			};
			//On to the next file if the previous one wasn't a device
			if ((attr&DOS_ATTR_DEVICE) == 0) ret = DOS_FindNext();
			else ret = false;
		};
		dos.echo=echo;
	}

	WriteOut(MSG_Get("SHELL_CMD_COPY_SUCCESS"),count);
	dos.dta(save_dta);
}

void DOS_Shell::CMD_SET(char * args) {
	HELP("SET");
	StripSpaces(args);
	std::string line;
	if (!*args) {
		/* No command line show all environment lines */	
		Bitu count=GetEnvCount();
		for (Bitu a=0;a<count;a++) {
			if (GetEnvNum(a,line)) WriteOut("%s\n",line.c_str());			
		}
		return;
	}
	//There are args:
	char * pcheck = args;
	while ( *pcheck && (*pcheck == ' ' || *pcheck == '\t')) pcheck++;
	if (*pcheck && strlen(pcheck) >3 && (strncasecmp(pcheck,"/p ",3) == 0)) E_Exit("Set /P is not supported. Use Choice!");

	char * p=strpbrk(args, "=");
	if (!p) {
		if (!GetEnvStr(args,line)) WriteOut(MSG_Get("SHELL_CMD_SET_NOT_SET"),args);
		WriteOut("%s\n",line.c_str());
	} else {
		*p++=0;
		/* parse p for envirionment variables */
		char parsed[CMD_MAXLINE];
		char* p_parsed = parsed;
		while(*p) {
			if(*p != '%') *p_parsed++ = *p++; //Just add it (most likely path)
			else if( *(p+1) == '%') {
				*p_parsed++ = '%'; p += 2; //%% => % 
			} else {
				char * second = strchr(++p,'%');
				if(!second) continue; *second++ = 0;
				std::string temp;
				if (GetEnvStr(p,temp)) {
					std::string::size_type equals = temp.find('=');
					if (equals == std::string::npos) continue;
					strcpy(p_parsed,temp.substr(equals+1).c_str());
					p_parsed += strlen(p_parsed);
				}
				p = second;
			}
		}
		*p_parsed = 0;
		/* Try setting the variable */
		if (!SetEnv(args,parsed)) {
			WriteOut(MSG_Get("SHELL_CMD_SET_OUT_OF_SPACE"));
		}
	}
}

void DOS_Shell::CMD_IF(char * args) {
	HELP("IF");
	StripSpaces(args,'=');
	bool has_not=false;

	while (strncasecmp(args,"NOT",3) == 0) {
		if (!isspace(*reinterpret_cast<unsigned char*>(&args[3])) && (args[3] != '=')) break;
		args += 3;	//skip text
		//skip more spaces
		StripSpaces(args,'=');
		has_not = !has_not;
	}

	if(strncasecmp(args,"ERRORLEVEL",10) == 0) {
		args += 10;	//skip text
		//Strip spaces and ==
		StripSpaces(args,'=');
		char* word = StripWord(args);
		if(!isdigit(*word)) {
			WriteOut(MSG_Get("SHELL_CMD_IF_ERRORLEVEL_MISSING_NUMBER"));
			return;
		}

		Bit8u n = 0;
		do n = n * 10 + (*word - '0');
		while (isdigit(*++word));
		if(*word && !isspace(*word)) {
			WriteOut(MSG_Get("SHELL_CMD_IF_ERRORLEVEL_INVALID_NUMBER"));
			return;
		}
		/* Read the error code from DOS */
		if ((dos.return_code>=n) ==(!has_not)) DoCommand(args);
		return;
	}

	if(strncasecmp(args,"EXIST ",6) == 0) {
		args += 6; //Skip text
		StripSpaces(args);
		char* word = StripArg(args);
		if (!*word) {
			WriteOut(MSG_Get("SHELL_CMD_IF_EXIST_MISSING_FILENAME"));
			return;
		}

		{	/* DOS_FindFirst uses dta so set it to our internal dta */
			RealPt save_dta=dos.dta();
			dos.dta(dos.tables.tempdta);
			bool ret=DOS_FindFirst(word,0xffff & ~DOS_ATTR_VOLUME);
			dos.dta(save_dta);
			if (ret==(!has_not)) DoCommand(args);
		}
		return;
	}

	/* Normal if string compare */

	char* word1 = args;
	// first word is until space or =
	while (*args && !isspace(*reinterpret_cast<unsigned char*>(args)) && (*args != '='))
		args++;
	char* end_word1 = args;

	// scan for =
	while (*args && (*args != '='))
		args++;
	// check for ==
	if ((*args==0) || (args[1] != '=')) {
		SyntaxError();
		return;
	}
	args += 2;
	StripSpaces(args,'=');

	char* word2 = args;
	// second word is until space or =
	while (*args && !isspace(*reinterpret_cast<unsigned char*>(args)) && (*args != '='))
		args++;

	if (*args) {
		*end_word1 = 0;		// mark end of first word
		*args++ = 0;		// mark end of second word
		StripSpaces(args,'=');

		if ((strcmp(word1,word2)==0)==(!has_not)) DoCommand(args);
	}
}

void DOS_Shell::CMD_GOTO(char * args) {
	HELP("GOTO");
	StripSpaces(args);
	if (!bf) return;
	if (*args &&(*args==':')) args++;
	//label ends at the first space
	char* non_space = args;
	while (*non_space) {
		if((*non_space == ' ') || (*non_space == '\t')) 
			*non_space = 0; 
		else non_space++;
	}
	if (!*args) {
		WriteOut(MSG_Get("SHELL_CMD_GOTO_MISSING_LABEL"));
		return;
	}
	if (!bf->Goto(args)) {
		WriteOut(MSG_Get("SHELL_CMD_GOTO_LABEL_NOT_FOUND"),args);
		return;
	}
}

void DOS_Shell::CMD_SHIFT(char * args ) {
	HELP("SHIFT");
	if(bf) bf->Shift();
}

void DOS_Shell::CMD_TYPE(char * args) {
	HELP("TYPE");
	StripSpaces(args);
	if (!*args) {
		WriteOut(MSG_Get("SHELL_SYNTAXERROR"));
		return;
	}
	Bit16u handle;
	char * word;
nextfile:
	word=StripArg(args);
	if (!DOS_OpenFile(word,0,&handle)) {
		WriteOut(MSG_Get("SHELL_CMD_FILE_NOT_FOUND"),word);
		return;
	}
	Bit16u code;
	Bit16u n;Bit8u c;
	do {
		n=1;
		DOS_ReadFile(handle,&c,&n);
		if (c==0x1a) break; // stop at EOF
		DOS_WriteFile(STDOUT,&c,&n);

		if(check_key(code)) {
			if(code == 0x2e03) {
				get_key(code);
				WriteOut("^C\n");
				return;
			}
		}
	} while (n);
	DOS_CloseFile(handle);
	if (*args) goto nextfile;
}

void DOS_Shell::CMD_REM(char * args) {
	HELP("REM");
}

void DOS_Shell::CMD_PAUSE(char * args){
	HELP("PAUSE");
	WriteOut(MSG_Get("SHELL_CMD_PAUSE"));
	Bit8u c;Bit16u n=1;
	DOS_ReadFile(STDIN,&c,&n);
	if (c==0) DOS_ReadFile(STDIN,&c,&n); // read extended key
}

void DOS_Shell::CMD_CALL(char * args){
	HELP("CALL");
	this->call=true; /* else the old batchfile will be closed first */
	this->ParseLine(args);
	this->call=false;
}

void DOS_Shell::CMD_CHCP(char * args) {
	HELP("CHCP");
	Bit32u active_cp, set_cp;
	if (sscanf(args, "%u", &set_cp) == 1) {
		reg_bx = static_cast<Bit16u>(set_cp);
		reg_ax = 0x6602;
		CALLBACK_RunRealInt(0x21);
		if (reg_al == 0xff) WriteOut(MSG_Get("SHELL_CMD_CHCP_ERROR"));
	}
	else {
		reg_ax = 0x6601;
		CALLBACK_RunRealInt(0x21);
	}
	active_cp = reg_bx;
	WriteOut("Active code page is %d", active_cp);
}

void DOS_Shell::CMD_DATE(char * args) {
	HELP("DATE");	
	if(ScanCMDBool(args,"H")) {
		// synchronize date with host parameter
		time_t curtime;
		struct tm *loctime;
		curtime = time (NULL);
		loctime = localtime (&curtime);
		
		reg_cx = loctime->tm_year+1900;
		reg_dh = loctime->tm_mon+1;
		reg_dl = loctime->tm_mday;

		reg_ah=0x2b; // set system date
		CALLBACK_RunRealInt(0x21);
		return;
	}
	// check if a date was passed in command line
	Bit32u newday,newmonth,newyear;
	if(sscanf(args,"%u-%u-%u",&newmonth,&newday,&newyear)==3) {
		if(IS_DOS_JAPANESE) {
			reg_cx = static_cast<Bit16u>(newmonth);
			reg_dh = static_cast<Bit8u>(newday);
			reg_dl = static_cast<Bit8u>(newyear);
		} else {
			reg_cx = static_cast<Bit16u>(newyear);
			reg_dh = static_cast<Bit8u>(newmonth);
			reg_dl = static_cast<Bit8u>(newday);
		}
		reg_ah=0x2b; // set system date
		CALLBACK_RunRealInt(0x21);
		if(reg_al==0xff) WriteOut(MSG_Get("SHELL_CMD_DATE_ERROR"));
		return;
	}
	// display the current date
	reg_ah=0x2a; // get system date
	CALLBACK_RunRealInt(0x21);

	const char* datestring = MSG_Get("SHELL_CMD_DATE_DAYS");
	Bit32u length;
	char day[6] = {0};
	if(sscanf(datestring,"%u",&length) && (length<5) && (strlen(datestring)==(length*7+1))) {
		// date string appears valid
		for(Bit32u i = 0; i < length; i++) day[i] = datestring[reg_al*length+1+i];
	}
	bool dateonly = ScanCMDBool(args,"T");
	if(!dateonly) WriteOut(MSG_Get("SHELL_CMD_DATE_NOW"));

	const char* formatstring = MSG_Get("SHELL_CMD_DATE_FORMAT");
	if(strlen(formatstring)!=5) return;
	char buffer[15] = {0};
	Bitu bufferptr=0;
	for(Bitu i = 0; i < 5; i++) {
		if(i==1 || i==3) {
			buffer[bufferptr] = formatstring[i];
			bufferptr++;
		} else {
			if(formatstring[i]=='M') bufferptr += sprintf(buffer+bufferptr,"%02u",(Bit8u) reg_dh);
			if(formatstring[i]=='D') bufferptr += sprintf(buffer+bufferptr,"%02u",(Bit8u) reg_dl);
			if(formatstring[i]=='Y') bufferptr += sprintf(buffer+bufferptr,"%04u",(Bit16u) reg_cx);
		}
	}
	if(IS_DOS_JAPANESE) {
		WriteOut("%s (%s)\n",buffer, day);
	} else {
		WriteOut("%s %s\n",day, buffer);
	}
	if(!dateonly) WriteOut(MSG_Get("SHELL_CMD_DATE_SETHLP"));
};

void DOS_Shell::CMD_TIME(char * args) {
	HELP("TIME");
	if(ScanCMDBool(args,"H")) {
		// synchronize time with host parameter
		time_t curtime;
		struct tm *loctime;
		curtime = time (NULL);
		loctime = localtime (&curtime);
		
		//reg_cx = loctime->;
		//reg_dh = loctime->;
		//reg_dl = loctime->;

		// reg_ah=0x2d; // set system time TODO
		// CALLBACK_RunRealInt(0x21);
		
		Bit32u ticks=(Bit32u)(((double)(loctime->tm_hour*3600+
										loctime->tm_min*60+
										loctime->tm_sec))*18.206481481);
		mem_writed(BIOS_TIMER,ticks);
		return;
	}
	bool timeonly = ScanCMDBool(args,"T");

	reg_ah=0x2c; // get system time
	CALLBACK_RunRealInt(0x21);
/*
		reg_dl= // 1/100 seconds
		reg_dh= // seconds
		reg_cl= // minutes
		reg_ch= // hours
*/
	if(timeonly) {
		WriteOut("%2u:%02u\n",reg_ch,reg_cl);
	} else {
		WriteOut(MSG_Get("SHELL_CMD_TIME_NOW"));
		WriteOut("%2u:%02u:%02u,%02u\n",reg_ch,reg_cl,reg_dh,reg_dl);
	}
};

void DOS_Shell::CMD_SUBST (char * args) {
/* If more that one type can be substed think of something else 
 * E.g. make basedir member dos_drive instead of localdrive
 */
	HELP("SUBST");
	localDrive* ldp=0;
	char mountstring[DOS_PATHLENGTH+CROSS_LEN+20];
	char temp_str[2] = { 0,0 };
	try {
		strcpy(mountstring,"MOUNT ");
		StripSpaces(args);
		std::string arg;
		CommandLine command(0,args);

		if (command.GetCount() != 2) throw 0 ;
  
		command.FindCommand(1,arg);
		if( (arg.size()>1) && arg[1] !=':')  throw(0);
		temp_str[0]=(char)toupper(args[0]);
		command.FindCommand(2,arg);
		if((arg=="/D") || (arg=="/d")) {
			if(!Drives[temp_str[0]-'A'] ) throw 1; //targetdrive not in use
			strcat(mountstring,"-u ");
			strcat(mountstring,temp_str);
			this->ParseLine(mountstring);
			return;
		}
		if(Drives[temp_str[0]-'A'] ) throw 0; //targetdrive in use
		strcat(mountstring,temp_str);
		strcat(mountstring," ");

  		Bit8u drive;char dir[DOS_PATHLENGTH+2],fulldir[DOS_PATHLENGTH];
   		if (strchr(arg.c_str(),'\"')==NULL)
	   		sprintf(dir,"\"%s\"",arg.c_str());
	   	else strcpy(dir,arg.c_str());
		if (!DOS_MakeName(dir,fulldir,&drive)) throw 0;
	
		if( ( ldp=dynamic_cast<localDrive*>(Drives[drive])) == 0 ) throw 0;
		char newname[CROSS_LEN];   
		strcpy(newname, ldp->basedir);	   
		strcat(newname,fulldir);
		CROSS_FILENAME(newname);
		ldp->dirCache.ExpandName(newname);
		strcat(mountstring,"\"");	   
		strcat(mountstring, newname);
		strcat(mountstring,"\"");	   
		this->ParseLine(mountstring);
	}
	catch(int a){
		if(a == 0) {
			WriteOut(MSG_Get("SHELL_CMD_SUBST_FAILURE"));
		} else {
		       	WriteOut(MSG_Get("SHELL_CMD_SUBST_NO_REMOVE"));
		}
		return;
	}
	catch(...) {		//dynamic cast failed =>so no localdrive
		WriteOut(MSG_Get("SHELL_CMD_SUBST_FAILURE"));
		return;
	}
   
	return;
}

void DOS_Shell::CMD_LOADHIGH(char *args){
	HELP("LOADHIGH");
	Bit16u umb_start=dos_infoblock.GetStartOfUMBChain();
	Bit8u umb_flag=dos_infoblock.GetUMBChainState();
	Bit8u old_memstrat=(Bit8u)(DOS_GetMemAllocStrategy()&0xff);
	if (umb_start==0x9fff) {
		if ((umb_flag&1)==0) DOS_LinkUMBsToMemChain(1);
		DOS_SetMemAllocStrategy(0x80);	// search in UMBs first
		this->ParseLine(args);
		Bit8u current_umb_flag=dos_infoblock.GetUMBChainState();
		if ((current_umb_flag&1)!=(umb_flag&1)) DOS_LinkUMBsToMemChain(umb_flag);
		DOS_SetMemAllocStrategy(old_memstrat);	// restore strategy
	} else this->ParseLine(args);
}

void DOS_Shell::CMD_CHOICE(char * args){
	HELP("CHOICE");
	static char defchoice[3] = {'y','n',0};
	char *rem = NULL, *ptr;
	bool optN = ScanCMDBool(args,"N");
	bool optS = ScanCMDBool(args,"S"); //Case-sensitive matching
	ScanCMDBool(args,"T"); //Default Choice after timeout
	if (args) {
		char *last = strchr(args,0);
		StripSpaces(args);
		rem = ScanCMDRemain(args);
		if (rem && *rem && (tolower(rem[1]) != 'c')) {
			WriteOut(MSG_Get("SHELL_ILLEGAL_SWITCH"),rem);
			return;
		}
		if (args == rem) args = strchr(rem,0)+1;
		if (rem) rem += 2;
		if(rem && rem[0]==':') rem++; /* optional : after /c */
		if (args > last) args = NULL;
	}
	if (!rem || !*rem) rem = defchoice; /* No choices specified use YN */
	ptr = rem;
	Bit8u c;
	if(!optS) while ((c = *ptr)) *ptr++ = (char)toupper(c); /* When in no case-sensitive mode. make everything upcase */
	if(args && *args ) {
		StripSpaces(args);
		size_t argslen = strlen(args);
		if(argslen>1 && args[0] == '"' && args[argslen-1] =='"') {
			args[argslen-1] = 0; //Remove quotes
			args++;
		}
		WriteOut(args);
	}
	/* Show question prompt of the form [a,b]? where a b are the choice values */
	if (!optN) {
		if(args && *args) WriteOut(" ");
		WriteOut("[");
		size_t len = strlen(rem);
		for(size_t t = 1; t < len; t++) {
			WriteOut("%c,",rem[t-1]);
		}
		WriteOut("%c]?",rem[len-1]);
	}

	Bit16u n=1;
	do {
		DOS_ReadFile (STDIN,&c,&n);
	} while (!c || !(ptr = strchr(rem,(optS?c:toupper(c)))));
	c = optS?c:(Bit8u)toupper(c);
	DOS_WriteFile (STDOUT,&c, &n);
	dos.return_code = (Bit8u)(ptr-rem+1);
}

static char *str_replace(char *orig, char *rep, char *with) {
    char *result; // the return string
    char *ins;    // the next insert point
    char *tmp;    // varies
    int len_rep;  // length of rep (the string to remove)
    int len_with; // length of with (the string to replace rep with)
    int len_front; // distance between rep and end of last rep
    int count;    // number of replacements

    // sanity checks and initialization
    if (!orig || !rep)
        return NULL;
    len_rep = strlen(rep);
    if (len_rep == 0)
        return NULL; // empty rep causes infinite loop during count
    if (!with)
        with = "";
    len_with = strlen(with);

    // count the number of replacements needed
    ins = orig;
    for (count = 0; tmp = strstr(ins, rep); ++count) {
        ins = tmp + len_rep;
    }

    tmp = result = (char *)malloc(strlen(orig) + (len_with - len_rep) * count + 1);

    if (!result)
        return NULL;
    while (count--) {
        ins = strstr(orig, rep);
        len_front = ins - orig;
        tmp = strncpy(tmp, orig, len_front) + len_front;
        tmp = strcpy(tmp, with) + len_with;
        orig += len_front + len_rep; // move to next "end of rep"
    }
    strcpy(tmp, orig);
    return result;
}

#ifndef WIN32
#define	_strnicmp	strncasecmp
#endif

void DOS_Shell::CMD_FOR(char *args){
	HELP("FOR");
	args = ltrim(args);
	if (strlen(args)<12){SyntaxError();return;}
	char s[3];
	strcpy(s,"%%");
	if (*args=='%' && (isalpha(args[1]) || isdigit(args[1]) || strchr("_-/*.;#$",args[1])) && isspace(args[2]))
		s[1]=*(args+1);
	else{SyntaxError();return;}
	args = ltrim(args+3);
	if (_strnicmp(args, "IN", 2) || !isspace(args[2])){SyntaxError();return;}
	args = ltrim(args+3);
	if (*args=='(')
		args = ltrim(args+1);
	else{SyntaxError();return;}
	char *p=strchr(args, ')');
	if (p==NULL||!isspace(*(p+1))){SyntaxError();return;}
	*p=0;
	char flist[260], *fp=flist;
	if (strlen(ltrim(args))<260)
		strcpy(flist, ltrim(args));
	else
		{
		strncpy(flist, args, 259);
		flist[259]=0;
		}
	*p=')';
	args=ltrim(p+2);
	if (_strnicmp(args, "DO", 2) || !isspace(args[2])){SyntaxError();return;}
	args = ltrim(args+3);
	bool lfn=uselfn;//&&lfnfor;
	while (*fp)
		{
		p=fp;
		int q=0;
		while (*p&&(q/2*2!=q||*p!=' '&&*p!=','&&*p!=';'))
			{
			if (*p=='"')
				q++;
			p++;
			}
		bool last=!!strlen(p);
		if (last) *p=0;
		if (strchr(fp, '?') || strchr(fp, '*'))
			{
			char name[DOS_NAMELENGTH_ASCII];												// File name and extension
			char lname[LFN_NAMELENGTH];
			char path[DOS_PATHLENGTH], tmp[DOS_PATHLENGTH+LFN_NAMELENGTH];
			char *r=strrchr_dbcs(fp, '\\');
			strcpy(path, "");
			if (r!=NULL) {
				*r=0;
				strcpy(path, fp);
				strcat(path, "\\");
				*r='\\';
			}
			if (strchr(path,'\"')) {
				int k=0;
				for (int i=0;i<(int)strlen(path);i++)
					if (path[i]!='\"')
						path[k++]=path[i];
				path[k]=0;
			}
			Bit32u size;
			Bit16u date, time;
			Bit8u attr;
			DOS_DTA dta(dos.dta());
			if (DOS_FindFirst(fp, ~(DOS_ATTR_VOLUME|DOS_ATTR_DIRECTORY|DOS_ATTR_DEVICE|DOS_ATTR_HIDDEN|DOS_ATTR_SYSTEM)))
				{
				dta.GetResult(name, lname, size, date, time, attr);
				strcpy(tmp,path);
				DoCommand(str_replace(args, s, strcat(tmp,lfn?lname:name)));
				while (DOS_FindNext())
					{
					dta.GetResult(name, lname, size, date, time, attr);
					strcpy(tmp,path);
					DoCommand(str_replace(args, s, strcat(tmp,lfn?lname:name)));
					}
				}
			}
		else
			DoCommand(str_replace(args, s, fp));
		if (last) *p=' ';
		fp=ltrim(p);
		}
}

void DOS_Shell::CMD_ATTRIB(char *args){
	HELP("ATTRIB");
	// No-Op for now.
}

void DOS_Shell::CMD_PROMPT(char *args){
	HELP("PROMPT");
	if (*args == '=')
		args = ltrim(args+1);
	SetEnv("PROMPT", *args ? args : "");
}

void DOS_Shell::CMD_PATH(char *args){
	HELP("PATH");
	if(args && *args && strlen(args)){
		char pathstring[DOS_PATHLENGTH+CROSS_LEN+20]={ 0 };
		strcpy(pathstring,"set PATH=");
		while(args && *args && (*args=='='|| *args==' ')) 
		     args++;
		strcat(pathstring,args);
		this->ParseLine(pathstring);
		return;
	} else {
		std::string line;
		if(GetEnvStr("PATH",line)) {
        		WriteOut("%s",line.c_str());
		} else {
			WriteOut("PATH=(null)");
		}
	}
}

void DOS_Shell::CMD_VER(char *args) {
	HELP("VER");
	if(args && *args) {
		char* word = StripWord(args);
		if(strcasecmp(word,"set")) return;
		word = StripWord(args);
		if (!*args && !*word) { //Reset
			dos.version.major = 5;
			dos.version.minor = 0;
		} else if (*args == 0 && *word && (strchr(word,'.') != 0)) { //Allow: ver set 5.1
			const char * p = strchr(word,'.');
			dos.version.major = (Bit8u)(atoi(word));
			dos.version.minor = (Bit8u)(atoi(p+1));
		} else { //Official syntax: ver set 5 2
			dos.version.major = (Bit8u)(atoi(word));
			dos.version.minor = (Bit8u)(atoi(args));
		}
	} else WriteOut(MSG_Get("SHELL_CMD_VER_VER"),VERSION,dos.version.major,dos.version.minor);
}

static void SetDbcsTable(bool japanese_flag)
{
	if(japanese_flag) {
		mem_writew(Real2Phys(dos.tables.dbcs) + 0x00, 0x0006); //Set DBCS table
		mem_writeb(Real2Phys(dos.tables.dbcs) + 0x02, 0x81); 
		mem_writeb(Real2Phys(dos.tables.dbcs) + 0x03, 0x9f);
		mem_writeb(Real2Phys(dos.tables.dbcs) + 0x04, 0xe0);
		mem_writeb(Real2Phys(dos.tables.dbcs) + 0x05, 0xfc);
		mem_writeb(Real2Phys(dos.tables.dbcs) + 0x06, 0x00);
		mem_writeb(Real2Phys(dos.tables.dbcs) + 0x07, 0x00);
		real_writew(dos.tables.country_seg, 5, 932);
	} else {
		mem_writew(Real2Phys(dos.tables.dbcs) + 0x00, 0x0000);
		mem_writew(Real2Phys(dos.tables.dbcs) + 0x02, 0x0000); 
		mem_writew(Real2Phys(dos.tables.dbcs) + 0x04, 0x0000);
		mem_writew(Real2Phys(dos.tables.dbcs) + 0x06, 0x0000);
		real_writew(dos.tables.country_seg, 5, 437);
	}
}

void DOS_Shell::CMD_CHEV(char *args)
{
	HELP("CHEV");

	bool japanese_flag = false;
	const char *status = "US";
	Bit8u new_mode = 0xff;
	Bit8u mode = real_readb(BIOSMEM_SEG, BIOSMEM_CURRENT_MODE);
	if(args && *args) {
		char *word  = StripWord(args);
		if(!strcasecmp(word, "jp")) {
			if(IS_AX_ARCH) {
				status = "AX";
				new_mode = 0x51;
			} else {
				status = "DOS/V";
				new_mode = 0x03;
			}
			japanese_flag = true;
		} else if(!strcasecmp(word, "vt") || !strcasecmp(word, "vt2")) {
			status = "DOS/V(V-Text)";
			japanese_flag = true;
			new_mode = !strcasecmp(word, "vt") ? 0x70 : 0x78;
		} else if(!strcasecmp(word, "j3")) {
			if(IS_J3_ARCH) {
				status = "J-3100";
				japanese_flag = true;
				new_mode = 0x74;
			}
		} else if(!strcasecmp(word, "us")) {
			if(IS_AX_ARCH) {
				new_mode = 0x01;
			} else {
				new_mode = 0x03;
			}
		}
		if(new_mode != 0xff) {
			SetDbcsTable(japanese_flag);
			if(IS_AX_ARCH) {
				if(INT10_AX_SetCRTBIOSMode(new_mode)) {
					new_mode = 0x03;
				}
			} else {
				reg_ax = new_mode;
				CALLBACK_RunRealInt(0x10);
			}
			if(new_mode == 0x78) {
				new_mode = 0x70;
			}
			WriteOut(MSG_Get("SHELL_CMD_CHEV_CHANGE"), status, new_mode);
		}
	} else {
		if(IS_DOS_JAPANESE) {
			if(mode == 0x03) {
				if(IS_AX_ARCH) {
					status = "AX";
				} else {
					status = "DOS/V";
				}
			} else if(mode == 0x70) {
				status = "DOS/V(V-Text)";
			} else if(mode == 0x72) {
				status = "DOS/V(Graphics)";
			} else if(mode == 0x74) {
				status = "J-3100";
			}
		}
		WriteOut(MSG_Get("SHELL_CMD_CHEV_STATUS"), status, mode);
	}
}

void DOS_Shell::CMD_BREAK(char *args)
{
	HELP("BREAK");
	args = trim(args);
	if (!*args)
		WriteOut("BREAK is %s\n", dos.breakcheck ? "on" : "off");
	else if (!strcasecmp(args, "OFF"))
		dos.breakcheck = false;
	else if (!strcasecmp(args, "ON"))
		dos.breakcheck = true;
	else
		WriteOut("Must specify ON or OFF\n");
}
