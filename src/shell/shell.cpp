/*
 *  Copyright (C) 2002-2021  The DOSBox Team
 *  Copyright (C) 2016-2019 akm
 *  Copyright (C) 2019 takapyu
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
 *  Wengier: LFN and AUTO MOUNT support, MISC FIX
 */


#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "dosbox.h"
#include "regs.h"
#include "control.h"
#include "shell.h"
#include "callback.h"
#include "support.h"
#include "timer.h"
#include "jfont.h"

Bitu call_shellstop;
bool insert;
/* Larger scope so shell_del autoexec can use it to
 * remove things from the environment */
DOS_Shell * first_shell = 0;
Bit16u cmd_line_seg;
bool LineInputFlag;

static Bitu shellstop_handler(void) {
	return CBRET_STOP;
}

static void SHELL_ProgramStart(Program * * make) {
	*make = new DOS_Shell;
}
//Repeat it with the correct type, could do it in the function below, but this way it should be 
//clear that if the above function is changed, this function might need a change as well.
static void SHELL_ProgramStart_First_shell(DOS_Shell * * make) {
	*make = new DOS_Shell;
}

#define AUTOEXEC_SIZE 4096
static char autoexec_data[AUTOEXEC_SIZE] = { 0 };
static std::list<std::string> autoexec_strings;
typedef std::list<std::string>::iterator auto_it;

void VFILE_Remove(const char *name);

void AutoexecObject::Install(const std::string &in) {
	if(GCC_UNLIKELY(installed)) E_Exit("autoexec: already created %s",buf.c_str());
	installed = true;
	buf = in;
	autoexec_strings.push_back(buf);
	this->CreateAutoexec();

	//autoexec.bat is normally created AUTOEXEC_Init.
	//But if we are already running (first_shell)
	//we have to update the envirionment to display changes

	if(first_shell)	{
		//create a copy as the string will be modified
		std::string::size_type n = buf.size();
		char* buf2 = new char[n + 1];
		safe_strncpy(buf2, buf.c_str(), n + 1);
		if((strncasecmp(buf2,"set ",4) == 0) && (strlen(buf2) > 4)){
			char* after_set = buf2 + 4;//move to variable that is being set
			char* test = strpbrk(after_set,"=");
			if(!test) {first_shell->SetEnv(after_set,"");return;}
			*test++ = 0;
			//If the shell is running/exists update the environment
			first_shell->SetEnv(after_set,test);
		}
		delete [] buf2;
	}
}

void AutoexecObject::InstallBefore(const std::string &in) {
	if(GCC_UNLIKELY(installed)) E_Exit("autoexec: already created %s",buf.c_str());
	installed = true;
	buf = in;
	autoexec_strings.push_front(buf);
	this->CreateAutoexec();
}

void AutoexecObject::CreateAutoexec(void) {
	/* Remove old autoexec.bat if the shell exists */
	if(first_shell)	VFILE_Remove("AUTOEXEC.BAT");

	//Create a new autoexec.bat
	autoexec_data[0] = 0;
	size_t auto_len;
	for(auto_it it = autoexec_strings.begin(); it != autoexec_strings.end(); it++) {

		std::string linecopy = (*it);
		std::string::size_type offset = 0;
		//Lets have \r\n as line ends in autoexec.bat.
		while(offset < linecopy.length()) {
			std::string::size_type  n = linecopy.find("\n",offset);
			if ( n == std::string::npos ) break;
			std::string::size_type rn = linecopy.find("\r\n",offset);
			if ( rn != std::string::npos && rn + 1 == n) {offset = n + 1; continue;}
			// \n found without matching \r
			linecopy.replace(n,1,"\r\n");
			offset = n + 2;
		}

		auto_len = strlen(autoexec_data);
		if ((auto_len+linecopy.length() + 3) > AUTOEXEC_SIZE) {
			E_Exit("SYSTEM:Autoexec.bat file overflow");
		}
		sprintf((autoexec_data + auto_len),"%s\r\n",linecopy.c_str());
	}
	if (first_shell) VFILE_Register("AUTOEXEC.BAT",(Bit8u *)autoexec_data,(Bit32u)strlen(autoexec_data));
}

AutoexecObject::~AutoexecObject(){
	if(!installed) return;

	// Remove the line from the autoexecbuffer and update environment
	for(auto_it it = autoexec_strings.begin(); it != autoexec_strings.end(); ) {
		if ((*it) == buf) {
			std::string::size_type n = buf.size();
			char* buf2 = new char[n + 1];
			safe_strncpy(buf2, buf.c_str(), n + 1);
			bool stringset = false;
			// If it's a environment variable remove it from there as well
			if ((strncasecmp(buf2,"set ",4) == 0) && (strlen(buf2) > 4)){
				char* after_set = buf2 + 4;//move to variable that is being set
				char* test = strpbrk(after_set,"=");
				if (!test) {
					delete [] buf2;
					continue;
				}
				*test = 0;
				stringset = true;
				//If the shell is running/exists update the environment
				if (first_shell) first_shell->SetEnv(after_set,"");
			}
			delete [] buf2;
			if (stringset && first_shell && first_shell->bf && first_shell->bf->filename.find("AUTOEXEC.BAT") != std::string::npos) {
				//Replace entry with spaces if it is a set and from autoexec.bat, as else the location counter will be off.
				*it = buf.assign(buf.size(),' ');
				it++;
			} else {
				it = autoexec_strings.erase(it);
			}
		} else it++;
	}
	this->CreateAutoexec();
}

DOS_Shell::DOS_Shell():Program(){
	input_handle=STDIN;
	echo=true;
	exit=false;
	bf=0;
	call=false;
	completion_start = NULL;
}

Bitu DOS_Shell::GetRedirection(char *s, char **ifn, char **ofn, char **toc, bool * append) {

	char * lr=s;
	char * lw=s;
	char ch;
	Bitu num=0;
	bool quote = false;
	char* t;
	int q;

	while ( (ch=*lr++) ) {
		if(quote && ch != '"') { /* don't parse redirection within quotes. Not perfect yet. Escaped quotes will mess the count up */
			*lw++ = ch;
			continue;
		}

		switch (ch) {
		case '"':
			quote = !quote;
			break;
		case '>':
			*append=((*lr)=='>');
			if (*append) lr++;
			lr=ltrim(lr);
			if (*ofn) free(*ofn);
			*ofn=lr;
			q = 0;
			while (*lr && (q/2*2!=q || *lr != ' ') && *lr!='<' && *lr!='|') {
				if (*lr=='"')
					q++;
				lr++;
			}
			//if it ends on a : => remove it.
			if((*ofn != lr) && (lr[-1] == ':')) lr[-1] = 0;
//			if(*lr && *(lr+1))
//				*lr++=0;
//			else
//				*lr=0;
			t = (char*)malloc(lr-*ofn+1);
			safe_strncpy(t,*ofn,lr-*ofn+1);
			*ofn=t;
			continue;
		case '<':
			if (*ifn) free(*ifn);
			lr=ltrim(lr);
			*ifn=lr;
			q = 0;
			while (*lr && (q/2*2!=q || *lr != ' ') && *lr!='>' && *lr != '|') {
				if (*lr=='"')
					q++;
				lr++;
			}
			if((*ifn != lr) && (lr[-1] == ':')) lr[-1] = 0;
//			if(*lr && *(lr+1))
//				*lr++=0;
//			else
//				*lr=0;
			t = (char*)malloc(lr-*ifn+1);
			safe_strncpy(t,*ifn,lr-*ifn+1);
			*ifn=t;
			continue;
		case '|':
			if (*toc)
				free(*toc);
			lr = ltrim(lr);
			*toc = lr;
			q = 0;
			while (*lr)
				lr++;
			if ((*toc != lr) && (lr[-1] == ':'))
				lr[-1] = 0;
			t = (char*)malloc(lr-*toc+1);
			safe_strncpy(t, *toc, lr-*toc+1);
			*toc = t;
			continue;
			}
		*lw++=ch;
	}
	*lw=0;
	return num;
}

void DOS_Shell::ParseLine(char * line) {
	LOG(LOG_EXEC,LOG_ERROR)("Parsing command line: %s",line);
	/* Check for a leading @ */
 	if (line[0] == '@') line[0] = ' ';
	line = trim(line);

	/* Do redirection and pipe checks */

	char * in  = 0;
	char * out = 0;
	char * toc = 0;
	
	Bit16u dummy,dummy2;
	Bit32u bigdummy = 0;
	Bitu num = 0;		/* Number of commands in this line */
	bool append;
	bool normalstdin  = false;	/* wether stdin/out are open on start. */
	bool normalstdout = false;	/* Bug: Assumed is they are "con"      */
	
	num = GetRedirection(line, &in, &out, &toc, &append);
	if (num>1) LOG_MSG("SHELL: Multiple command on 1 line not supported");
	if (in || out || toc) {
		normalstdin  = (psp->GetFileHandle(0) != 0xff);
		normalstdout = (psp->GetFileHandle(1) != 0xff);
	}
	if (in) {
		if(DOS_OpenFile(in,OPEN_READ,&dummy)) {	//Test if file exists
			DOS_CloseFile(dummy);
			LOG_MSG("SHELL: Redirect input from %s",in);
			if(normalstdin) DOS_CloseFile(0);	//Close stdin
			DOS_OpenFile(in,OPEN_READ,&dummy);	//Open new stdin
		}
	}
	char pipetmp[15];
	bool fail = false;
	if (toc) {
		srand(GetTicks());
		sprintf(pipetmp, "pipe%d.tmp", rand()%10000);
	}
	if (out||toc){
		if (out) LOG_MSG("SHELL: Redirect output to %s",out);
		if (toc) LOG_MSG("SHELL: Piping output to %s",pipetmp);
		if(normalstdout) DOS_CloseFile(1);
		if(!normalstdin && !in) DOS_OpenFile("con",OPEN_READWRITE,&dummy);
		bool status = true;
		/* Create if not exist. Open if exist. Both in read/write mode */
		if(!toc&&append) {
			if( (status = DOS_OpenFile(out,OPEN_READWRITE,&dummy)) ) {
				 DOS_SeekFile(1,&bigdummy,DOS_SEEK_END);
			} else {
				status = DOS_CreateFile(out,DOS_ATTR_ARCHIVE,&dummy);	//Create if not exists.
			}
		} else {
			if (toc&&DOS_FindFirst(pipetmp, ~DOS_ATTR_VOLUME))
				if (!DOS_UnlinkFile(pipetmp))
					fail=true;
			status = DOS_OpenFileExtended(toc?pipetmp:out,OPEN_READWRITE,DOS_ATTR_ARCHIVE,0x12,&dummy,&dummy2);
		}

		if(!status && normalstdout) DOS_OpenFile("con",OPEN_READWRITE,&dummy); //Read only file, open con again
		if(!normalstdin && !in) DOS_CloseFile(0);
	}
	/* Run the actual command */
	DoCommand(line);
	/* Restore handles */
	if(in) {
		DOS_CloseFile(0);
		if(normalstdin) DOS_OpenFile("con",OPEN_READWRITE,&dummy);
		free(in);
	}
	if(out||toc) {
		DOS_CloseFile(1);
		if(!normalstdin) DOS_OpenFile("con",OPEN_READWRITE,&dummy);
		if(normalstdout) DOS_OpenFile("con",OPEN_READWRITE,&dummy);
		if(!normalstdin) DOS_CloseFile(0);
		free(out);
	}
	if (toc)
		{
		if (!fail&&DOS_OpenFile(pipetmp, OPEN_READ, &dummy))					// Test if file can be opened for reading
			{
			DOS_CloseFile(dummy);
			if (normalstdin)
				DOS_CloseFile(0);												// Close stdin
			DOS_OpenFile(pipetmp, OPEN_READ, &dummy);							// Open new stdin
			ParseLine(toc);
			DOS_CloseFile(0);
			if (normalstdin)
				DOS_OpenFile("con", OPEN_READWRITE, &dummy);
			}
		else
			WriteOut("Failed to create or open a temporary file for piping.\n");
		free(toc);
		if (DOS_FindFirst(pipetmp, ~DOS_ATTR_VOLUME)) DOS_UnlinkFile(pipetmp);
		}
}



void DOS_Shell::RunInternal(void) {
	char input_line[CMD_MAXLINE] = {0};
	while (bf) {
		if (bf->ReadLine(input_line)) {
			if (echo) {
				if (input_line[0] != '@') {
					ShowPrompt();
					WriteOut_NoParsing(input_line);
					WriteOut_NoParsing("\n");
				}
			}
			ParseLine(input_line);
			if (echo) WriteOut_NoParsing("\n");
		}
	}
}

void DOS_Shell::Run(void) {
	char input_line[CMD_MAXLINE] = {0};
	std::string line;
	if (cmd->FindStringRemainBegin("/C",line)) {
		strcpy(input_line,line.c_str());
		char* sep = strpbrk(input_line,"\r\n"); //GTA installer
		if (sep) *sep = 0;
		DOS_Shell temp;
		temp.echo = echo;
		temp.ParseLine(input_line);		//for *.exe *.com  |*.bat creates the bf needed by runinternal;
		temp.RunInternal();				// exits when no bf is found.
		return;
	}
	/* Start a normal shell and check for a first command init */
	if (cmd->FindString("/INIT",line,true)) {
		if (IS_AX_ARCH) {
			if (dos.set_ax_enabled)
				WriteOut(MSG_Get("SHELL_STARTUP_AX_JEGA"), BUILD_VERSION);
			else
				WriteOut(MSG_Get("SHELL_STARTUP_AX_EGA"), BUILD_VERSION);
		}
		else if (IS_J3_ARCH) {
			WriteOut(MSG_Get("SHELL_STARTUP_J3"), BUILD_VERSION);
		}
		else if (IS_DOSV) {
			WriteOut(MSG_Get("SHELL_STARTUP_DOSV"), BUILD_VERSION);
		}
		else {
			WriteOut(MSG_Get("SHELL_STARTUP_BEGIN"), BUILD_VERSION);
#if C_DEBUG
			WriteOut(MSG_Get("SHELL_STARTUP_DEBUG"));
#endif
			if (machine == MCH_CGA) WriteOut(MSG_Get("SHELL_STARTUP_CGA"));
			if (machine == MCH_DCGA) WriteOut(MSG_Get("SHELL_STARTUP_DCGA"));
			if (machine == MCH_HERC) WriteOut(MSG_Get("SHELL_STARTUP_HERC"));
			WriteOut(MSG_Get("SHELL_STARTUP_END"));
		}

		strcpy(input_line,line.c_str());
		line.erase();
		ParseLine(input_line);
	} else {
		WriteOut(MSG_Get("SHELL_STARTUP_SUB"),VERSION);	
	}
	do {
		if (bf){
			if(bf->ReadLine(input_line)) {
				if (echo) {
					if (input_line[0]!='@') {
						ShowPrompt();
						WriteOut_NoParsing(input_line);
						WriteOut_NoParsing("\n");
					};
				};
				ParseLine(input_line);
				if (echo) WriteOut("\n");
			}
		} else {
			if (echo) ShowPrompt();
			LineInputFlag = true;
			InputCommand(input_line);
			LineInputFlag = false;
			ParseLine(input_line);
			if(input_line[0] != 0 || CheckStayVz()) {
				if (echo && !bf) WriteOut_NoParsing("\n");
			}
		}
	} while (!exit);
}

void DOS_Shell::SyntaxError(void) {
	WriteOut(MSG_Get("SHELL_SYNTAXERROR"));
}

class AUTOEXEC:public Module_base {
private:
	AutoexecObject autoexec[17];
	AutoexecObject autoexec_echo;
public:
	AUTOEXEC(Section* configuration):Module_base(configuration) {
		/* Register a virtual AUOEXEC.BAT file */
		std::string line;
		Section_line * section=static_cast<Section_line *>(configuration);

		/* Check -securemode switch to disable mount/imgmount/boot after running autoexec.bat */
		bool secure = control->cmdline->FindExist("-securemode",true);

		/* add stuff from the configfile unless -noautexec or -securemode is specified. */
		char * extra = const_cast<char*>(section->data.c_str());
		if (extra && !secure && !control->cmdline->FindExist("-noautoexec",true)) {
			/* detect if "echo off" is the first line */
			size_t firstline_length = strcspn(extra,"\r\n");
			bool echo_off  = !strncasecmp(extra,"echo off",8);
			if (echo_off && firstline_length == 8) extra += 8;
			else {
				echo_off = !strncasecmp(extra,"@echo off",9);
				if (echo_off && firstline_length == 9) extra += 9;
				else echo_off = false;
			}

			/* if "echo off" move it to the front of autoexec.bat */
			if (echo_off)  { 
				autoexec_echo.InstallBefore("@echo off");
				if (*extra == '\r') extra++; //It can point to \0
				if (*extra == '\n') extra++; //same
			}

			/* Install the stuff from the configfile if anything left after moving echo off */

			if (*extra) autoexec[0].Install(std::string(extra));
		}

		/* Check to see for extra command line options to be added (before the command specified on commandline) */
		/* Maximum of extra commands: 10 */
		Bitu i = 1;
		while (control->cmdline->FindString("-c",line,true) && (i <= 11)) {
#if defined (WIN32) || defined (OS2)
			//replace single with double quotes so that mount commands can contain spaces
			for(Bitu temp = 0;temp < line.size();++temp) if(line[temp] == '\'') line[temp]='\"';
#endif //Linux users can simply use \" in their shell
			autoexec[i++].Install(line);
		}

		/* Check for the -exit switch which causes dosbox to when the command on the commandline has finished */
		bool addexit = control->cmdline->FindExist("-exit",true);

		/* Check for first command being a directory or file */
		char buffer[CROSS_LEN+1];
		char orig[CROSS_LEN+1];
		char cross_filesplit[2] = {CROSS_FILESPLIT , 0};

		Bitu dummy = 1;
		bool command_found = false;
		while (control->cmdline->FindCommand(dummy++,line) && !command_found) {
			struct stat test;
			if (line.length() > CROSS_LEN) continue;
			strcpy(buffer,line.c_str());
			if (stat(buffer,&test)) {
				if (getcwd(buffer,CROSS_LEN) == NULL) continue;
				if (strlen(buffer) + line.length() + 1 > CROSS_LEN) continue;
				strcat(buffer,cross_filesplit);
				strcat(buffer,line.c_str());
				if (stat(buffer,&test)) continue;
			}
			if (test.st_mode & S_IFDIR) {
				autoexec[12].Install(std::string("MOUNT C \"") + buffer + "\"");
				autoexec[13].Install("C:");
				if(secure) autoexec[14].Install("z:\\config.com -securemode");
				command_found = true;
			} else {
				char* name = strrchr_dbcs(buffer,CROSS_FILESPLIT);
				if (!name) { //Only a filename
					line = buffer;
					if (getcwd(buffer,CROSS_LEN) == NULL) continue;
					if (strlen(buffer) + line.length() + 1 > CROSS_LEN) continue;
					strcat(buffer,cross_filesplit);
					strcat(buffer,line.c_str());
					if(stat(buffer,&test)) continue;
					name = strrchr_dbcs(buffer,CROSS_FILESPLIT);
					if(!name) continue;
				}
				*name++ = 0;
				if (access(buffer,F_OK)) continue;
				autoexec[12].Install(std::string("MOUNT C \"") + buffer + "\"");
				autoexec[13].Install("C:");
				/* Save the non-modified filename (so boot and imgmount can use it (long filenames, case sensivitive)) */
				strcpy(orig,name);
				upcase(name);
				if(strstr(name,".BAT") != 0) {
					if(secure) autoexec[14].Install("z:\\config.com -securemode");
					/* BATch files are called else exit will not work */
					autoexec[15].Install(std::string("CALL ") + name);
					if(addexit) autoexec[16].Install("exit");
				} else if((strstr(name,".IMG") != 0) || (strstr(name,".IMA") !=0 )) {
					//No secure mode here as boot is destructive and enabling securemode disables boot
					/* Boot image files */
					autoexec[15].Install(std::string("BOOT ") + orig);
				} else if((strstr(name,".ISO") != 0) || (strstr(name,".CUE") !=0 )) {
					/* imgmount CD image files */
					/* securemode gets a different number from the previous branches! */
					autoexec[14].Install(std::string("IMGMOUNT D \"") + orig + std::string("\" -t iso"));
					//autoexec[16].Install("D:");
					if(secure) autoexec[15].Install("z:\\config.com -securemode");
					/* Makes no sense to exit here */
				} else {
					if(secure) autoexec[14].Install("z:\\config.com -securemode");
					autoexec[15].Install(name);
					if(addexit) autoexec[16].Install("exit");
				}
				command_found = true;
			}
		}

		/* Combining -securemode, noautoexec and no parameters leaves you with a lovely Z:\. */
		if ( !command_found ) {
			if ( secure ) autoexec[12].Install("z:\\config.com -securemode");
		}
		VFILE_Register("AUTOEXEC.BAT",(Bit8u *)autoexec_data,(Bit32u)strlen(autoexec_data));
	}
};

static AUTOEXEC* test;

void AUTOEXEC_Init(Section * sec) {
	test = new AUTOEXEC(sec);
}

static Bitu INT2E_Handler(void) {
	/* Save return address and current process */
	RealPt save_ret=real_readd(SegValue(ss),reg_sp);
	Bit16u save_psp=dos.psp();

	/* Set first shell as process and copy command */
	dos.psp(DOS_FIRST_SHELL);
	DOS_PSP psp(DOS_FIRST_SHELL);
	psp.SetCommandTail(RealMakeSeg(ds,reg_si));
	SegSet16(ss,RealSeg(psp.GetStack()));
	reg_sp=2046;

	/* Read and fix up command string */
	CommandTail tail;
	MEM_BlockRead(PhysMake(dos.psp(),CTBUF+1),&tail,CTBUF+1);
	if (tail.count<CTBUF) tail.buffer[tail.count]=0;
	else tail.buffer[CTBUF-1]=0;
	char* crlf=strpbrk(tail.buffer,"\r\n");
	if (crlf) *crlf=0;

	/* Execute command */
	if (strlen(tail.buffer)) {
		DOS_Shell temp;
		temp.ParseLine(tail.buffer);
		temp.RunInternal();
	}

	/* Restore process and "return" to caller */
	dos.psp(save_psp);
	SegSet16(cs,RealSeg(save_ret));
	reg_ip=RealOff(save_ret);
	reg_ax=0;
	return CBRET_NONE;
}

static char const * const path_string="PATH=Z:\\";
static char const * const prompt_string="PROMPT=$P$G";
static char const * const comspec_string="COMSPEC=Z:\\COMMAND.COM";
static char const * const full_name="Z:\\COMMAND.COM";
static char const * const init_line="/INIT AUTOEXEC.BAT";

void SHELL_Init() {
	/* Add messages */
	MSG_Add("SHELL_ILLEGAL_PATH","Illegal Path.\n");
	MSG_Add("SHELL_CMD_HELP","If you want a list of all supported commands type \033[33;1mhelp /all\033[0m .\nA short list of the most often used commands:\n");
	MSG_Add("SHELL_CMD_ECHO_ON","ECHO is on.\n");
	MSG_Add("SHELL_CMD_ECHO_OFF","ECHO is off.\n");
	MSG_Add("SHELL_ILLEGAL_SWITCH","Illegal switch: %s.\n");
	MSG_Add("SHELL_MISSING_PARAMETER","Required parameter missing.\n");
	MSG_Add("SHELL_CMD_CHDIR_ERROR","Unable to change to: %s.\n");
	MSG_Add("SHELL_CMD_CHDIR_HINT","Hint: To change to different drive type \033[31m%c:\033[0m\n");
	MSG_Add("SHELL_CMD_CHDIR_HINT_2","directoryname contains unquoted spaces.\nTry \033[31mcd %s\033[0m or properly quote them with quotation marks.\n");
	MSG_Add("SHELL_CMD_CHDIR_HINT_3","You are still on drive Z:, change to a mounted drive with \033[31mC:\033[0m.\n");
	MSG_Add("SHELL_CMD_DATE_HELP","Displays or changes the internal date.\n");
	MSG_Add("SHELL_CMD_DATE_ERROR","The specified date is not correct.\n");
	MSG_Add("SHELL_CMD_DATE_DAYS","3SunMonTueWedThuFriSat"); // "2SoMoDiMiDoFrSa"
	MSG_Add("SHELL_CMD_DATE_NOW","Current date: ");
	MSG_Add("SHELL_CMD_DATE_SETHLP","Type 'date MM-DD-YYYY' to change.\n");
	MSG_Add("SHELL_CMD_DATE_FORMAT","M/D/Y");
	MSG_Add("SHELL_CMD_DATE_HELP_LONG","DATE [[/T] [/H] [/S] | MM-DD-YYYY]\n"\
									"  MM-DD-YYYY: new date to set\n"\
									"  /S:         Permanently use host time and date as DOS time\n"\
                                    "  /F:         Switch back to DOSBox internal time (opposite of /S)\n"\
									"  /T:         Only display date\n"\
									"  /H:         Synchronize with host\n");
	MSG_Add("SHELL_CMD_TIME_HELP","Displays the internal time.\n");
	MSG_Add("SHELL_CMD_TIME_NOW","Current time: ");
	MSG_Add("SHELL_CMD_TIME_HELP_LONG","TIME [/T] [/H]\n"\
									"  /T:         Display simple time\n"\
									"  /H:         Synchronize with host\n");
	MSG_Add("SHELL_CMD_MKDIR_ERROR","Unable to make: %s.\n");
	MSG_Add("SHELL_CMD_RMDIR_ERROR","Unable to remove: %s.\n");
	MSG_Add("SHELL_CMD_RENAME_ERROR","Unable to rename: %s.\n");
	MSG_Add("SHELL_CMD_DEL_ERROR","Unable to delete: %s.\n");
	MSG_Add("SHELL_SYNTAXERROR","The syntax of the command is incorrect.\n");
	MSG_Add("SHELL_CMD_SET_NOT_SET","Environment variable %s not defined.\n");
	MSG_Add("SHELL_CMD_SET_OUT_OF_SPACE","Not enough environment space left.\n");
	MSG_Add("SHELL_CMD_IF_EXIST_MISSING_FILENAME","IF EXIST: Missing filename.\n");
	MSG_Add("SHELL_CMD_IF_ERRORLEVEL_MISSING_NUMBER","IF ERRORLEVEL: Missing number.\n");
	MSG_Add("SHELL_CMD_IF_ERRORLEVEL_INVALID_NUMBER","IF ERRORLEVEL: Invalid number.\n");
	MSG_Add("SHELL_CMD_GOTO_MISSING_LABEL","No label supplied to GOTO command.\n");
	MSG_Add("SHELL_CMD_GOTO_LABEL_NOT_FOUND","GOTO: Label %s not found.\n");
	MSG_Add("SHELL_CMD_FILE_NOT_FOUND","File %s not found.\n");
	MSG_Add("SHELL_CMD_FILE_EXISTS","File %s already exists.\n");
	MSG_Add("SHELL_CMD_DIR_INTRO","Directory of %s.\n");
	MSG_Add("SHELL_CMD_DIR_BYTES_USED","%5d File(s) %17s Bytes.\n");
	MSG_Add("SHELL_CMD_DIR_BYTES_FREE","%5d Dir(s)  %17s Bytes free.\n");
	MSG_Add("SHELL_EXECUTE_DRIVE_NOT_FOUND","Drive %c does not exist!\nYou must \033[31mmount\033[0m it first. Type \033[1;33mintro\033[0m or \033[1;33mintro mount\033[0m for more information.\n");
	MSG_Add("SHELL_EXECUTE_AUTOMOUNT","Automatic drive mounting is turned on.");
	MSG_Add("SHELL_EXECUTE_DRIVE_ACCESS_REMOVABLE","Do you want to give DOSBox access to your real removable drive %c [Y/N]?");
	MSG_Add("SHELL_EXECUTE_DRIVE_ACCESS_NETWORK","Do you want to give DOSBox access to your real network drive %c [Y/N]?");
	MSG_Add("SHELL_EXECUTE_DRIVE_ACCESS_OPTICAL","Do you want to give DOSBox access to your real optical drive %c [Y/N]?");
	MSG_Add("SHELL_EXECUTE_DRIVE_ACCESS_LOCAL","Do you want to give DOSBox access to your real local drive %c [Y/N]?");
	MSG_Add("SHELL_EXECUTE_DRIVE_ACCESS_WARNING_WIN"," But mounting c:\\ is NOT recommended.");
	MSG_Add("SHELL_EXECUTE_ILLEGAL_COMMAND","Illegal command: %s.\n");
	MSG_Add("SHELL_CMD_PAUSE","Press any key to continue.\n");
	MSG_Add("SHELL_CMD_PAUSE_HELP","Waits for 1 keystroke to continue.\n");
	MSG_Add("SHELL_CMD_PROMPT_HELP","Changes the DOSBox SVN-lfn command prompt.\n");
	MSG_Add("SHELL_CMD_COPY_FAILURE","Copy failure : %s.\n");
	MSG_Add("SHELL_CMD_COPY_SUCCESS","   %d File(s) copied.\n");
	MSG_Add("SHELL_CMD_COPY_ERROR","Error in copying file %s\n");
	MSG_Add("SHELL_CMD_SUBST_NO_REMOVE","Unable to remove, drive not in use.\n");
	MSG_Add("SHELL_CMD_SUBST_FAILURE","SUBST failed. You either made an error in your commandline or the target drive is already used.\nIt's only possible to use SUBST on Local drives");

	MSG_Add("SHELL_STARTUP_BEGIN",
		"\033[44;1m\xC9\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
		"\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
		"\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xBB\n"
		"\xBA \033[32mWelcome to DOSBox %-8s\033[37m                                         \xBA\n"
		"\xBA                                                                    \xBA\n"
//		"\xBA DOSBox runs real and protected mode games.                         \xBA\n"
		"\xBA For a short introduction for new users type: \033[33mINTRO\033[37m                 \xBA\n"
		"\xBA For supported shell commands type: \033[33mHELP\033[37m                            \xBA\n"
		"\xBA                                                                    \xBA\n"
		"\xBA To adjust the emulated CPU speed, use \033[31mctrl-F11\033[37m and \033[31mctrl-F12\033[37m.       \xBA\n"
		"\xBA To activate the keymapper \033[31mctrl-F1\033[37m.                                 \xBA\n"
		"\xBA For more information read the \033[36mREADME\033[37m file in the DOSBox directory. \xBA\n"
		"\xBA                                                                    \xBA\n"
	);
	MSG_Add("SHELL_STARTUP_CGA","\xBA DOSBox supports Composite CGA mode.                                \xBA\n"
	        "\xBA Use \033[31mF12\033[37m to set composite output ON, OFF, or AUTO (default).        \xBA\n"
	        "\xBA \033[31m(Alt-)F11\033[37m changes hue; \033[31mctrl-alt-F11\033[37m selects early/late CGA model.  \xBA\n"
	        "\xBA                                                                    \xBA\n"
	);
	MSG_Add("SHELL_STARTUP_HERC","\xBA Use \033[31mF11\033[37m to cycle through white, amber, and green monochrome color. \xBA\n"
	        "\xBA                                                                    \xBA\n"
	);
	MSG_Add("SHELL_STARTUP_DEBUG",
	        "\xBA Press \033[31malt-Pause\033[37m to enter the debugger or start the exe with \033[33mDEBUG\033[37m. \xBA\n"
	        "\xBA                                                                    \xBA\n"
	);
	MSG_Add("SHELL_STARTUP_END",
	        "\xBA \033[32mHAVE FUN!\033[37m                                                          \xBA\n"
	        "\xBA \033[32mThe DOSBox Team \033[33mhttp://www.dosbox.com\033[37m                              \xBA\n"
	        "\xC8\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
	        "\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
	        "\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xBC\033[0m\n"
	        //"\n" //Breaks the startup message if you type a mount and a drive change.
	);
	//for AX (machine selected EGA and VGA)
	MSG_Add("SHELL_STARTUP_AX_EGA", "\033[44;1m\xC9\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
		"\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
		"\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xBB\n"
		"\xBA \033[32mWelcome to DOSVAX build %-8s\033[37m                                   \xBA\n"
		"\xBA                                                                    \xBA\n"
		"\xBA \033[36m    //  ///  ////\033[37m  This is a modified DOSBox emulates AX machine.  \xBA\n"
		"\xBA \033[36m   ////  /// //  \033[37m  You need the AX version of MS-DOS.              \xBA\n"
		"\xBA \033[36m  / ////  ///    \033[37m                                                  \xBA\n"
		"\xBA \033[36m //  ////  ///   \033[37m  To boot from a floppy image, use \033[31mboot\033[37m.          \xBA\n"
		"\xBA \033[36m////  /////////  \033[37m  You can also map AX special keys \033[31mctrl-F1\033[37m.       \xBA\n"
		"\xBA                                                                    \xBA\n"
		"\xBA \033[32mHAVE FUN!\033[37m                                                          \xBA\n"
		"\xBA \033[32makm \033[33mhttp://diarywind.com\033[37m                                           \xBA\n"
		"\xC8\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
		"\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
		"\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xBC\033[0m\n");
	//for AX (machine selected EGA and VGA)
	MSG_Add("SHELL_STARTUP_AX_JEGA", 
		"\033[44;1m\x084\x0a1\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x0a2\n"
		"\x084\x0a0 \033[32mWelcome to DOSVAX build %-8s\033[37m                                   \x084\x0a0\n"
		"\x084\x0a0                                                                    \x084\x0a0\n"
		"\x084\x0a0 \033[36m    //  ///  ////\033[37m   This is a modified DOSBox emulates AX machine. \x084\x0a0\n"
		"\x084\x0a0 \033[36m   ////  /// //  \033[37m   You need not have the AX version of MS-DOS,    \x084\x0a0\n"
		"\x084\x0a0 \033[36m  / ////  ///    \033[37m  but some applications won't run. If you need,   \x084\x0a0\n"
		"\x084\x0a0 \033[36m //  ////  ///   \033[37m  set \033[31mmachine=ega\033[37m and use the real MS-DOS.        \x084\x0a0\n"
		"\x084\x0a0 \033[36m////  /////////  \033[37m   You can map AX special keys \033[31mctrl-F1\033[37m.           \x084\x0a0\n"
		"\x084\x0a0                                                                    \x084\x0a0\n"
		"\x084\x0a0 \033[32mHAVE FUN!\033[37m                                                          \x084\x0a0\n"
		"\x084\x0a0 \033[32makm \033[33mhttp://diarywind.com\033[37m                                           \x084\x0a0\n"
		"\x084\x0a4\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x0a3\033[0m\n");
	MSG_Add("SHELL_STARTUP_J3",
		"\x084\x0a1\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x0a2\n"
		"\x084\x0a0 Welcome to DOSVAXJ3 build %-8s                                 \x084\x0a0\n"
		"\x084\x0a0                                                                    \x084\x0a0\n"
		"\x084\x0a0                 This is a modified DOSBox emulates J-3100 machine. \x084\x0a0\n"
		"\x084\x0a0                                  J-3100\x083\x082\x081\x05b\x083\x068\x082\x0c5\x082\x0cc\x08b\x04e\x093\x0ae machine=dcga \x084\x0a0\n"
		"\x084\x0a0                                   DOS/V\x083\x082\x081\x05b\x083\x068\x082\x0c5\x082\x0cc\x08b\x04e\x093\x0ae machine=dosv \x084\x0a0\n"
		"\x084\x0a0               \x089\x070\x08c\x0ea/\x093\x0fa\x096\x07b\x08c\x0ea/V-text/J-3100 \x090\x0d8\x091\x0d6 chev [us][jp][vt][j3] \x084\x0a0\n"
		"\x084\x0a4\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x0a3\n");
	MSG_Add("SHELL_STARTUP_DOSV",
		"\033[44;1m\x084\x0a1\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x0a2\n"
		"\x084\x0a0 Welcome to DOSVAXJ3 build %-8s                                 \x084\x0a0\n"
		"\x084\x0a0                                                                    \x084\x0a0\n"
		"\x084\x0a0                  This is a modified DOSBox emulates DOS/V machine. \x084\x0a0\n"
		"\x084\x0a0                                  J-3100\x083\x082\x081\x05b\x083\x068\x082\x0c5\x082\x0cc\x08b\x04e\x093\x0ae \033[31mmachine=dcga\033[37m \x084\x0a0\n"
		"\x084\x0a0                                   DOS/V\x083\x082\x081\x05b\x083\x068\x082\x0c5\x082\x0cc\x08b\x04e\x093\x0ae \033[31mmachine=dosv\033[37m \x084\x0a0\n"
		"\x084\x0a0                     \x089\x070\x08c\x0ea/\x093\x0fa\x096\x07b\x08c\x0ea/V-Text \x090\x0d8\x091\x0d6 \033[33mchev [us][jp][vt][vt2]\033[37m \x084\x0a0\n"
		"\x084\x0a4\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x09f\x084\x0a3\033[0m\n");
	MSG_Add("SHELL_STARTUP_SUB","\n\n\033[32;1mDOSBox %s Command Shell\033[0m\n\n");
	MSG_Add("SHELL_CMD_CHDIR_HELP","Displays/changes the current directory.\n");
	MSG_Add("SHELL_CMD_CHDIR_HELP_LONG","CHDIR [drive:][path]\n"
	        "CHDIR [..]\n"
	        "CD [drive:][path]\n"
	        "CD [..]\n\n"
	        "  ..   Specifies that you want to change to the parent directory.\n\n"
	        "Type CD drive: to display the current directory in the specified drive.\n"
	        "Type CD without parameters to display the current drive and directory.\n");
	MSG_Add("SHELL_CMD_CLS_HELP","Clear screen.\n");
	MSG_Add("SHELL_CMD_DIR_HELP","Directory View.\n");
	MSG_Add("SHELL_CMD_ECHO_HELP","Display messages and enable/disable command echoing.\n");
	MSG_Add("SHELL_CMD_EXIT_HELP","Exit from the shell.\n");
	MSG_Add("SHELL_CMD_HELP_HELP","Show help.\n");
	MSG_Add("SHELL_CMD_MKDIR_HELP","Make Directory.\n");
	MSG_Add("SHELL_CMD_MKDIR_HELP_LONG","MKDIR [drive:][path]\n"
	        "MD [drive:][path]\n");
	MSG_Add("SHELL_CMD_RMDIR_HELP","Remove Directory.\n");
	MSG_Add("SHELL_CMD_RMDIR_HELP_LONG","RMDIR [drive:][path]\n"
	        "RD [drive:][path]\n");
	MSG_Add("SHELL_CMD_SET_HELP","Change environment variables.\n");
	MSG_Add("SHELL_CMD_IF_HELP","Performs conditional processing in batch programs.\n");
	MSG_Add("SHELL_CMD_FOR_HELP","Runs a specified command for each item in a set of items.\n");
	MSG_Add("SHELL_CMD_GOTO_HELP","Jump to a labeled line in a batch script.\n");
	MSG_Add("SHELL_CMD_SHIFT_HELP","Leftshift commandline parameters in a batch script.\n");
	MSG_Add("SHELL_CMD_TYPE_HELP","Display the contents of a text-file.\n");
	MSG_Add("SHELL_CMD_TYPE_HELP_LONG","TYPE [drive:][path][filename]\n");
	MSG_Add("SHELL_CMD_REM_HELP","Add comments in a batch file.\n");
	MSG_Add("SHELL_CMD_REM_HELP_LONG","REM [comment]\n");
	MSG_Add("SHELL_CMD_NO_WILD","This is a simple version of the command, no wildcards allowed!\n");
	MSG_Add("SHELL_CMD_RENAME_HELP","Renames one or more files.\n");
	MSG_Add("SHELL_CMD_RENAME_HELP_LONG","RENAME [drive:][path]filename1 filename2.\n"
	        "REN [drive:][path]filename1 filename2.\n\n"
	        "Note that you can not specify a new drive or path for your destination file.\n");
	MSG_Add("SHELL_CMD_DELETE_HELP","Removes one or more files.\n");
	MSG_Add("SHELL_CMD_COPY_HELP","Copy files.\n");
	MSG_Add("SHELL_CMD_CALL_HELP","Start a batch file from within another batch file.\n");
	MSG_Add("SHELL_CMD_SUBST_HELP","Assign an internal directory to a drive.\n");
	MSG_Add("SHELL_CMD_LOADHIGH_HELP","Loads a program into upper memory (requires xms=true,umb=true).\n");
	MSG_Add("SHELL_CMD_CHOICE_HELP","Waits for a keypress and sets ERRORLEVEL.\n");
	MSG_Add("SHELL_CMD_CHOICE_HELP_LONG","CHOICE [/C:choices] [/N] [/S] text\n"
	        "  /C[:]choices  -  Specifies allowable keys.  Default is: yn.\n"
	        "  /N  -  Do not display the choices at end of prompt.\n"
	        "  /S  -  Enables case-sensitive choices to be selected.\n"
	        "  text  -  The text to display as a prompt.\n");
	MSG_Add("SHELL_CMD_ATTRIB_HELP","Does nothing. Provided for compatibility.\n");
	MSG_Add("SHELL_CMD_PATH_HELP","Provided for compatibility.\n");
	MSG_Add("SHELL_CMD_VER_HELP","View and set the reported DOS version.\n");
	MSG_Add("SHELL_CMD_VER_VER","DOSBox version %s. Reported DOS version %d.%02d.\n");
	MSG_Add("SHELL_CMD_CHEV_HELP","Change environment.\n");
	MSG_Add("SHELL_CMD_CHEV_HELP_LONG","CHEV [[JP][VT][J3][US]]\n"
	        "JP: Japanese DOS/V,AX\n"
	        "VT: Japanese DOS/V(V-Text)\n"
	        "J3: Japanese J-3100\n"
	        "US: English\n");
	MSG_Add("SHELL_CMD_CHEV_STATUS","This is the %s environment.\nCurrent video mode is %02xh");
	MSG_Add("SHELL_CMD_CHEV_CHANGE","Change to the %s environment.\nCurrent video mode is %02xh");
	MSG_Add("SHELL_CMD_BREAK_HELP","Sets or clears extended CTRL+C checking.\n");
	MSG_Add("SHELL_CMD_BREAK_HELP_LONG","BREAK [ON | OFF]\n\nType BREAK without a parameter to display the current BREAK setting.\n");

	/* Regular startup */
	call_shellstop=CALLBACK_Allocate();
	/* Setup the startup CS:IP to kill the last running machine when exitted */
	RealPt newcsip=CALLBACK_RealPointer(call_shellstop);
	SegSet16(cs,RealSeg(newcsip));
	reg_ip=RealOff(newcsip);

	CALLBACK_Setup(call_shellstop,shellstop_handler,CB_IRET,"shell stop");
	PROGRAMS_MakeFile("COMMAND.COM",SHELL_ProgramStart);

	/* Now call up the shell for the first time */
	Bit16u psp_seg=DOS_FIRST_SHELL;
	Bit16u env_seg=DOS_FIRST_SHELL+19; //DOS_GetMemory(1+(4096/16))+1;
	Bit16u stack_seg=DOS_GetMemory(2048/16);
	SegSet16(ss,stack_seg);
	reg_sp=2046;

	/* Set up int 24 and psp (Telarium games) */
	real_writeb(psp_seg+16+1,0,0xea);		/* far jmp */
	real_writed(psp_seg+16+1,1,real_readd(0,0x24*4));
	real_writed(0,0x24*4,((Bit32u)psp_seg<<16) | ((16+1)<<4));

	/* Set up int 23 to "int 20" in the psp. Fixes what.exe */
	real_writed(0,0x23*4,((Bit32u)psp_seg<<16));

	/* Set up int 2e handler */
	Bitu call_int2e=CALLBACK_Allocate();
	//RealPt addr_int2e=RealMake(psp_seg+16+1,8);
	RealPt addr_int2e=RealMake(psp_seg,((16+1)<<4) + 8);
	CALLBACK_Setup(call_int2e,&INT2E_Handler,CB_IRET_STI,Real2Phys(addr_int2e),"Shell Int 2e");
	RealSetVec(0x2e,addr_int2e);

	/* Setup MCBs */
	DOS_MCB pspmcb((Bit16u)(psp_seg-1));
	pspmcb.SetPSPSeg(psp_seg);	// MCB of the command shell psp
	pspmcb.SetSize(0x10+2);
	pspmcb.SetType(0x4d);
	DOS_MCB envmcb((Bit16u)(env_seg-1));
	envmcb.SetPSPSeg(psp_seg);	// MCB of the command shell environment
	envmcb.SetSize(DOS_MEM_START-env_seg);
	envmcb.SetType(0x4d);

	/* Setup environment */
	PhysPt env_write=PhysMake(env_seg,0);
	MEM_BlockWrite(env_write,path_string,(Bitu)(strlen(path_string)+1));
	env_write += (PhysPt)(strlen(path_string)+1);
	MEM_BlockWrite(env_write,prompt_string,(Bitu)(strlen(prompt_string)+1));
	env_write += (PhysPt)(strlen(prompt_string)+1);
	MEM_BlockWrite(env_write,comspec_string,(Bitu)(strlen(comspec_string)+1));
	env_write += (PhysPt)(strlen(comspec_string)+1);
	mem_writeb(env_write++,0);
	mem_writew(env_write,1);
	env_write+=2;
	MEM_BlockWrite(env_write,full_name,(Bitu)(strlen(full_name)+1));

	DOS_PSP psp(psp_seg);
	psp.MakeNew(0);
	dos.psp(psp_seg);

	/* The start of the filetable in the psp must look like this:
	 * 01 01 01 00 02
	 * In order to achieve this: First open 2 files. Close the first and
	 * duplicate the second (so the entries get 01) */
	Bit16u dummy=0;
	DOS_OpenFile("CON",OPEN_READWRITE,&dummy);	/* STDIN  */
	DOS_OpenFile("CON",OPEN_READWRITE,&dummy);	/* STDOUT */
	DOS_CloseFile(0);							/* Close STDIN */
	DOS_ForceDuplicateEntry(1,0);				/* "new" STDIN */
	DOS_ForceDuplicateEntry(1,2);				/* STDERR */
	DOS_OpenFile("CON",OPEN_READWRITE,&dummy);	/* STDAUX */
	DOS_OpenFile("PRN",OPEN_READWRITE,&dummy);	/* STDPRN */

	/* Create appearance of handle inheritance by first shell */
	for (Bit16u i=0;i<5;i++) {
		Bit8u handle=psp.GetFileHandle(i);
		if (Files[handle]) Files[handle]->AddRef();
	}

	psp.SetParent(psp_seg);
	/* Set the environment */
	psp.SetEnvironment(env_seg);
	/* Set the command line for the shell start up */
	CommandTail tail;
	tail.count=(Bit8u)strlen(init_line);
	memset(&tail.buffer,0,CTBUF);
	strcpy(tail.buffer,init_line);
	MEM_BlockWrite(PhysMake(psp_seg,CTBUF+1),&tail,CTBUF+1);
	
	/* Setup internal DOS Variables */
	dos.dta(RealMake(psp_seg,CTBUF+1));
	dos.psp(psp_seg);

	cmd_line_seg = DOS_GetMemory(16);

	SHELL_ProgramStart_First_shell(&first_shell);
	first_shell->Run();
	delete first_shell;
	first_shell = 0;//Make clear that it shouldn't be used anymore
}
