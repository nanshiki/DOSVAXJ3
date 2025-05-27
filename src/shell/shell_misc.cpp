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
 *  Wengier: LFN and AUTO MOUNT support
 */


#include <stdlib.h>
#include <string.h>
#include <algorithm> //std::copy
#include <iterator>  //std::front_inserter
#include "shell.h"
#include "control.h"
#include "regs.h"
#include "callback.h"
#include "support.h"
#include "jega.h"
#include "inout.h"
#include "pic.h"
#include "jfont.h"
#include "../ints/int10.h"

extern Bit16u cmd_line_seg;
extern bool insert;
extern bool CheckHat(Bit8u code);

void DOS_Shell::ShowPrompt(void) {
	dos.save_dta();
	std::string line;
	char prompt[CMD_MAXLINE+2], pt[CMD_MAXLINE+16];
	char *envPrompt=pt;
	if (GetEnvStr("PROMPT",line))
		{
		std::string::size_type idx = line.find('=');
		std::string value=line.substr(idx +1 , std::string::npos);
		strcpy(envPrompt, value.c_str());
		envPrompt=ltrim(envPrompt);
		}
	else
		strcpy(envPrompt, "$P$G");													// Default
	int len = 0;
	while (*envPrompt && len < sizeof(prompt)-1)
		{
		if (*envPrompt == '$' && (envPrompt[1]|0x20) == 'p')						// Current drive and path
			{
			char dir[DOS_PATHLENGTH];
			DOS_GetCurrentDir(0, dir, uselfn);
			if (len+strlen(dir)+4 < sizeof(prompt))
				{
				sprintf(prompt+len, "%c:\\%s>", DOS_GetDefaultDrive()+'A', dir);
				len += (int)strlen(dir)+3;
				envPrompt++;
				}
			else
				prompt[len++] = *envPrompt;
			}
		else if (*envPrompt == '$' && (envPrompt[1]|0x20) == 'n')					// Current drive
			{
			prompt[len++] = DOS_GetDefaultDrive()+'A';
			envPrompt++;
			}
		else if (*envPrompt == '$' && (envPrompt[1]|0x20) == 'd')					// Current date
			{
			if (len+10 < sizeof(prompt))
				{
				reg_ah=0x2a; // get system date
				CALLBACK_RunRealInt(0x21);
				sprintf(prompt+len, "%4u-%02u-%02u", reg_cx,reg_dh,reg_dl);
				len += 10;
				envPrompt++;
				}
			else
				prompt[len++] = *envPrompt;
			}
		else if (*envPrompt == '$' && (envPrompt[1]|0x20) == 't')					// Current time
			{
			if (len+8 < sizeof(prompt))
				{
				reg_ah=0x2c; // get system time
				CALLBACK_RunRealInt(0x21);
				sprintf(prompt+len, "%2u:%02u:%02u", reg_ch,reg_cl,reg_dh);
				len += 8;
				envPrompt++;
				}
			else
				prompt[len++] = *envPrompt;
			}
		else if (*envPrompt == '$' && (envPrompt[1]|0x20) == 'v')					// DOSBox version
			{
			if (len+16 < sizeof(prompt))
				{
				sprintf(prompt+len, "vDosPlus SVN-lfn");
				len += 16;
				envPrompt++;
				}
			else
				prompt[len++] = *envPrompt;
			}
		else if (*envPrompt == '$' && (envPrompt[1]|0x20) == 'h')					// Backspace
			{
			if (len) prompt[--len] = 0;
			envPrompt++;
			}
		else if (*envPrompt == '$' && (envPrompt[1]|0x20) == 'e')					// Escape character
			{
			prompt[len++] = 27;
			envPrompt++;
			}
		else if (*envPrompt == '$' && (envPrompt[1]|0x20) == 'g')					// > (greater-than sign)
			{
			prompt[len++] = '>';
			envPrompt++;
			}
		else if (*envPrompt == '$' && (envPrompt[1]|0x20) == 'l')					// < (less-than sign)
			{
			prompt[len++] = '<';
			envPrompt++;
			}
		else if (*envPrompt == '$' && (envPrompt[1]|0x20) == 'b')					// |
			{
			prompt[len++] = '|';
			envPrompt++;
			}
		else if (*envPrompt == '$' && (envPrompt[1]|0x20) == 'q')					// =
			{
			prompt[len++] = '=';
			envPrompt++;
			}
		else if (*envPrompt == '$' && envPrompt[1] == '$')							// $
			{
			prompt[len++] = '$';
			envPrompt++;
			}
		else if (*envPrompt == '$' && envPrompt[1] == '_')							// _ 
			{
			prompt[len++] = 0x0d;
			prompt[len++] = 0x0a;
			envPrompt++;
			}
		else																		// Rest is just as
			prompt[len++] = *envPrompt;
		envPrompt++;
		}
	prompt[len] = 0;
	WriteOut_NoParsing(prompt);
	dos.restore_dta();
}

static void outc(Bit8u c) {
	Bit16u n=1;
	DOS_WriteFile(STDOUT,&c,&n);
}

static void backone()
	{
	BIOS_NCOLS;
	Bit8u page=real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE);
	if (CURSOR_POS_COL(page)>0)
		outc(8);
	else if (CURSOR_POS_ROW(page)>0)
		INT10_SetCursorPos(CURSOR_POS_ROW(page)-1, ncols-1, page);
	}

static void beep() {
	IO_Write(0x43,0xb6);
	IO_Write(0x42,0x28);
	IO_Write(0x42,0x05);
	IO_Write(0x61,IO_Read(0x61)|3);
	double start;
	start=PIC_FullIndex();
	while ((PIC_FullIndex()-start)<333.0) CALLBACK_Idle();
	IO_Write(0x61,IO_Read(0x61)&~3);
}

static Bitu GetWideCount(char *line, Bitu str_index)
{
	int flag = 0;
	Bitu count = 1;
	for(Bitu pos = 0 ; pos < str_index ; pos++) {
		if(flag == 0) {
			if(isKanji1(line[pos])) {
				flag = 1;
			}
			count = 1;
		} else {
			if(isKanji2(line[pos])) {
				count = 2;
			}
			flag = 0;
		}
	}
	return count;
}

static void RemoveAllChar(char *line, Bitu str_index)
{
	for ( ; str_index > 0 ; str_index--) {
		// removes all characters
		if(CheckHat(line[str_index])) {
			backone(); backone(); outc(' '); outc(' '); backone(); backone();
		} else {
			backone(); outc(' '); backone();
		}
	}
	if(CheckHat(line[0])) {
		backone(); outc(' '); backone();
	}
}

static Bitu DeleteBackspace(bool delete_flag, char *line, Bitu &str_index, Bitu &str_len)
{
	Bitu count, pos;
	Bit16u len;

	pos = str_index;
	if(delete_flag) {
		if(isKanji1(line[pos])) {
			pos += 2;
		} else {
			pos += 1;
		}
	}
	count = GetWideCount(line, pos);

	pos = str_index;
	while(pos < str_len) {
		len = 1;
		DOS_WriteFile(STDOUT, (Bit8u *)&line[pos], &len);
		pos++;
	}
	RemoveAllChar(line, str_len);
	pos = delete_flag ? str_index : str_index - count;
	while(pos < str_len - count) {
		line[pos] = line[pos + count];
		pos++;
	}
	line[pos] = 0;
	if(!delete_flag) {
		str_index -= count;
	}
	str_len -= count;
	len = str_len;
	DOS_WriteFile(STDOUT, (Bit8u *)line, &len);
	pos = str_len;
	while(pos > str_index) {
		backone();
		if(CheckHat(line[pos - 1])) {
			backone();
		}
		pos--;
	}
	return count;
}

void DOS_Shell::InputCommand(char * line) {
	Bitu size=CMD_MAXLINE-2; //lastcharacter+0
	Bit8u c;Bit16u n=1;
	Bitu str_len=0;Bitu str_index=0;
	Bit16u len=0;
	int diff=0;
	bool current_hist=false; // current command stored in history?

	if(CheckStayVz()) {
		real_writeb(cmd_line_seg, 0, 250);
		reg_dx = 0;
		reg_ah = 0x0a;
		SegSet16(ds, cmd_line_seg);
		CALLBACK_RunRealInt(0x21);
		Bitu len = real_readb(cmd_line_seg, 1);
		for(Bitu pos = 0 ; pos < len ; pos++) {
			line[pos] = real_readb(cmd_line_seg, 2 + pos);
		}
		line[len] = '\0';
		return;
	}

	line[0] = '\0';

	std::list<std::string>::iterator it_history = l_history.begin(), it_completion = l_completion.begin();

	while (size) {
		dos.echo=false;
		while(!DOS_ReadFile(input_handle,&c,&n)) {
			Bit16u dummy;
			DOS_CloseFile(input_handle);
			DOS_OpenFile("con",2,&dummy);
			LOG(LOG_MISC,LOG_ERROR)("Reopening the input handle. This is a bug!");
		}
		if (!n) {
			size=0;			//Kill the while loop
			continue;
		}
		switch (c) {
		case 0x00:				/* Extended Keys */
			{
				DOS_ReadFile(input_handle,&c,&n);
				switch (c) {

				case 0x3d:		/* F3 */
					if (!l_history.size()) break;
					it_history = l_history.begin();
					if (it_history != l_history.end() && it_history->length() > str_len) {
						const char *reader = &(it_history->c_str())[str_len];
						while ((c = *reader++)) {
							line[str_index ++] = c;
							DOS_WriteFile(STDOUT,&c,&n);
						}
						str_len = str_index = (Bitu)it_history->length();
						size = CMD_MAXLINE - str_index - 2;
						line[str_len] = 0;
					}
					break;

				case 0x4B:	/* LEFT */
					if (str_index) {
						Bitu count = GetWideCount(line, str_index);
						Bit8u ch = line[str_index - 1];
						while(count > 0) {
							backone();
							str_index --;
							count--;
						}
						if(CheckHat(ch)) {
							backone();
						}
					}
					break;

				case 0x4D:	/* RIGHT */
					if (str_index < str_len) {
						Bitu count = 1;
						if(str_index < str_len - 1) {
							count = GetWideCount(line, str_index + 2);
						}
						while(count > 0) {
							outc(line[str_index++]);
							count--;
						}
					}
					break;

				case 0x47:	/* HOME */
					while (str_index) {
						backone();
						str_index--;
						if(CheckHat(line[str_index])) {
							backone();
						}
					}
					break;

				case 0x4F:	/* END */
					while (str_index < str_len) {
						outc(line[str_index++]);
					}
					break;

				case 0x48:	/* UP */
					if (l_history.empty() || it_history == l_history.end()) break;

					// store current command in history if we are at beginning
					if(*line != 0) {
						if (it_history == l_history.begin() && !current_hist) {
							current_hist=true;
							l_history.push_front(line);
						}
					}
					RemoveAllChar(line, str_index);
					str_index = 0;
					strcpy(line, it_history->c_str());
					len = (Bit16u)it_history->length();
					str_len = str_index = len;
					size = CMD_MAXLINE - str_index - 2;
					DOS_WriteFile(STDOUT, (Bit8u *)line, &len);
					it_history ++;
					break;

				case 0x50:	/* DOWN */
					if (l_history.empty() || it_history == l_history.begin()) break;

					// not very nice but works ..
					it_history --;
					if (it_history == l_history.begin()) {
						// no previous commands in history
						it_history ++;

						// remove current command from history
						if (current_hist) {
							current_hist=false;
							l_history.pop_front();
						}
						break;
					} else it_history --;

					RemoveAllChar(line, str_index);
					str_index = 0;
					strcpy(line, it_history->c_str());
					len = (Bit16u)it_history->length();
					str_len = str_index = len;
					size = CMD_MAXLINE - str_index - 2;
					DOS_WriteFile(STDOUT, (Bit8u *)line, &len);
					it_history ++;

					break;
				case 0x53:/* DELETE */
					if(str_index < str_len) {
						size += DeleteBackspace(true, line, str_index, str_len);
					}
					break;
				case 15:		/* Shift-Tab */
					if (l_completion.size()) {
						if (it_completion == l_completion.begin()) it_completion = l_completion.end (); 
						it_completion--;
		
						if (it_completion->length()) {
							for (;str_index > completion_index; str_index--) {
								// removes all characters
								backone(); outc(' '); backone();
							}

							strcpy(&line[completion_index], it_completion->c_str());
							len = (Bit16u)it_completion->length();
							str_len = str_index = completion_index + len;
							size = CMD_MAXLINE - str_index - 2;
							DOS_WriteFile(STDOUT, (Bit8u *)it_completion->c_str(), &len);
						}
					}
				default:
					break;
				}
			};
			break;
		case 0x08:				/* BackSpace */
			if(str_index) {
				size += DeleteBackspace(false, line, str_index, str_len);
			}
			if (l_completion.size()) l_completion.clear();
			break;
		case 0x0a:				/* New Line not handled */
			outc('\n');
			break;
		case 0x0d:				/* Return */
			outc('\n');
			size=0;			//Kill the while loop
			break;
		case'\t':
			{
				if (l_completion.size()) {
					if (str_index < completion_index) {
						beep();
						break;
					}
					it_completion ++;
					if (it_completion == l_completion.end()) it_completion = l_completion.begin();
				} else {
					// build new completion list
					// Lines starting with CD will only get directories in the list
					bool dir_only = (strncasecmp(line,"CD ",3)==0)||(strncasecmp(line,"MD ",3)==0)||(strncasecmp(line,"RD ",3)==0)||
								(strncasecmp(line,"CHDIR ",6)==0)||(strncasecmp(line,"MKDIR ",3)==0)||(strncasecmp(line,"RMDIR ",6)==0);
					int q=0, r=0, k=0;

					// get completion mask
					char *p_completion_start = strrchr(line, ' ');
					while (p_completion_start) {
						q=0;
						char *i;
						for (i=line;i<p_completion_start;i++)
							if (*i=='\"') q++;
						if (q/2*2==q) break;
						*i=0;
						p_completion_start = strrchr(line, ' ');
						*i=' ';
					}
					char c[]={'<','>','|'};
					for (int j=0; j<sizeof(c); j++) {
						char *sp = strrchr(line, c[j]);
						while (sp) {
							q=0;
							char *i;
							for (i=line;i<sp;i++)
								if (*i=='\"') q++;
							if (q/2*2==q) break;
							*i=0;
							sp = strrchr(line, c[j]);
							*i=c[j];
						}
						if (!p_completion_start || p_completion_start<sp)
							p_completion_start = sp;
					}
					if (p_completion_start) {
						p_completion_start ++;
						completion_index = (Bit16u)(str_len - strlen(p_completion_start));
					} else {
						p_completion_start = line;
						completion_index = 0;
					}
					if (str_index < completion_index) {
						beep();
						break;
					}
					k=completion_index;

					char *path;
					if ((path = strrchr(line+completion_index,':'))) completion_index = (Bit16u)(path-line+1);
					if ((path = strrchr_dbcs(line+completion_index,'\\'))) completion_index = (Bit16u)(path-line+1);
					if ((path = strrchr(line+completion_index,'/'))) completion_index = (Bit16u)(path-line+1);

					// build the completion list
					char mask[DOS_PATHLENGTH+2],smask[DOS_PATHLENGTH];
					if (p_completion_start) {
						strcpy(mask, p_completion_start);
						char* dot_pos=strrchr(mask,'.');
						char* bs_pos=strrchr_dbcs(mask,'\\');
						char* fs_pos=strrchr(mask,'/');
						char* cl_pos=strrchr(mask,':');
						// not perfect when line already contains wildcards, but works
						if ((dot_pos-bs_pos>0) && (dot_pos-fs_pos>0) && (dot_pos-cl_pos>0))
							strcat(mask, "*");
						else strcat(mask, "*.*");
					} else {
						strcpy(mask, "*.*");
					}

					RealPt save_dta=dos.dta();
					dos.dta(dos.tables.tempdta);

					bool res = false;
					if (DOS_GetSFNPath(mask,smask,false)) {
						sprintf(mask,"\"%s\"",smask);
						res = DOS_FindFirst(mask, 0xffff & ~DOS_ATTR_VOLUME);
					}
					if (!res) {
						dos.dta(save_dta);
						beep();
						break;
					}

					DOS_DTA dta(dos.dta());
					char name[DOS_NAMELENGTH_ASCII], lname[LFN_NAMELENGTH], qlname[LFN_NAMELENGTH+2];
					Bit32u size,hsize;Bit16u date;Bit16u time;Bit8u att;

					std::list<std::string> executable;
					q=0;r=0;
					while (*p_completion_start) {
						k++;
						if (*p_completion_start++=='\"') {
							if (k<=completion_index)
								q++;
							else
								r++;
						}
					}
					while (res) {
						dta.GetResult(name,lname,size,hsize,date,time,att);
						if (strchr(uselfn?lname:name,' ')!=NULL||q/2*2!=q||r)
 							sprintf(qlname,q/2*2!=q?"%s\"":"\"%s\"",uselfn?lname:name);
						else
							strcpy(qlname,uselfn?lname:name);
						// add result to completion list

						char *ext;	// file extension
						if (strcmp(name, ".") && strcmp(name, "..")) {
							if (dir_only) { //Handle the dir only case different (line starts with cd)
								if(att & DOS_ATTR_DIRECTORY) l_completion.push_back(qlname);
							} else {
								ext = strrchr(name, '.');
								if (ext && (strcmp(ext, ".BAT") == 0 || strcmp(ext, ".COM") == 0 || strcmp(ext, ".EXE") == 0))
									// we add executables to the a seperate list and place that list infront of the normal files
									executable.push_front(qlname);
								else
									l_completion.push_back(qlname);
							}
						}
						res=DOS_FindNext();
					}
					/* Add executable list to front of completion list. */
					std::copy(executable.begin(),executable.end(),std::front_inserter(l_completion));
					it_completion = l_completion.begin();
					dos.dta(save_dta);
				}

				if (l_completion.size() && it_completion->length()) {
					diff = str_len - str_index;
					for (int i=0; i<diff; i++)
						outc(' ');
					for (int i=0; i<diff; i++)
						backone();
					for (;str_index > completion_index; str_index--) {
						// removes all characters
						backone(); outc(' '); backone();
					}

					strcpy(&line[completion_index], it_completion->c_str());
					len = (Bit16u)it_completion->length();
					str_len = str_index = completion_index + len;
					size = CMD_MAXLINE - str_index - 2;
					DOS_WriteFile(STDOUT, (Bit8u *)it_completion->c_str(), &len);
				}
			}
			break;
		case 0x1b:   /* ESC */
			//write a backslash and return to the next line
			outc('\\');
			outc('\n');
			*line = 0;      // reset the line.
			if (l_completion.size()) l_completion.clear(); //reset the completion list.
			this->InputCommand(line);	//Get the NEW line.
			size = 0;       // stop the next loop
			str_len = 0;    // prevent multiple adds of the same line
			break;
		case 0x03:				/* Ctrl-C */
			outc(0x03);
			outc('\n');
			outc('\n');
			*line = 0;
			if (l_completion.size()) l_completion.clear();
			size = 0;
			str_len = 0;
			break;
		case 0x07:
			if(IS_J3_ARCH || dos.set_ax_enabled || IS_DOSV) {
				outc(7);
				break;
			}
		default:
			{
				bool kanji_flag = false;
				Bitu pos = str_index;
				while(1) {
					if (l_completion.size()) l_completion.clear();
					if(str_index < str_len && true) { //mem_readb(BIOS_KEYBOARD_FLAGS1)&0x80) dev_con.h ?
						//outc(' ');//move cursor one to the right.
						//Bit16u a = str_len - str_index;
						//Bit8u* text=reinterpret_cast<Bit8u*>(&line[str_index]);
						//DOS_WriteFile(STDOUT,text,&a);//write buffer to screen
						//outc(8);//undo the cursor the right.
						for(Bitu i=str_len;i>str_index;i--) {
							line[i]=line[i-1]; //move internal buffer
							//outc(8); //move cursor back (from write buffer to screen)
						}
						line[++str_len]=0;//new end (as the internal buffer moved one place to the right
						size--;
					};
					line[str_index]=c;
					str_index ++;
					if (str_index > str_len){ 
						line[str_index] = '\0';
						str_len++;
						size--;
					}
					if(!isKanji1(c) || kanji_flag) {
						break;
					}
					DOS_ReadFile(input_handle,&c,&n);
					kanji_flag = true;
				}
				while(pos < str_len) {
					outc(line[pos]);
					pos++;
				}
				pos = str_len;
				while(pos > str_index) {
					backone();
					pos--;
					if (CheckHat(line[pos])) {
						backone();
					}
				}
			}
			break;
		}
	}

	if (!str_len) return;
	str_len++;

	if(*line != 0) {
		// remove current command from history if it's there
		if (current_hist) {
			current_hist=false;
			l_history.pop_front();
		}

		// add command line to history
		l_history.push_front(line); it_history = l_history.begin();
		if (l_completion.size()) l_completion.clear();
	}
}

std::string full_arguments = "";
bool DOS_Shell::Execute(char * name,char * args) {
/* return true  => don't check for hardware changes in do_command 
 * return false =>       check for hardware changes in do_command */
	char fullname[DOS_PATHLENGTH+4]; //stores results from Which
	char* p_fullname;
	char line[CMD_MAXLINE];
	if(strlen(args)!= 0){
		if(*args != ' '){ //put a space in front
			line[0]=' ';line[1]=0;
			strncat(line,args,CMD_MAXLINE-2);
			line[CMD_MAXLINE-1]=0;
		}
		else
		{
			safe_strncpy(line,args,CMD_MAXLINE);
		}
	}else{
		line[0]=0;
	};

	/* check for a drive change */
	if (((strcmp(name + 1, ":") == 0) || (strcmp(name + 1, ":\\") == 0)) && isalpha(*name))
	{
		if (strrchr_dbcs(name,'\\')) { WriteOut(MSG_Get("SHELL_EXECUTE_ILLEGAL_COMMAND"),name); return true; }
		if (!DOS_SetDrive(toupper(name[0])-'A')) {
#ifdef WIN32
			Section_prop * sec=0; sec=static_cast<Section_prop *>(control->GetSection("dos"));
			const char *auto_string = sec->Get_string("automount");

			//if(!sec->Get_bool("automount")) { WriteOut(MSG_Get("SHELL_EXECUTE_DRIVE_NOT_FOUND"),toupper(name[0])); return true; }
			if(strcmp(auto_string, "true") && strcmp(auto_string, "auto")) {
				WriteOut(MSG_Get("SHELL_EXECUTE_DRIVE_NOT_FOUND"),toupper(name[0]));
				return true;
			}
			// automount: attempt direct letter to drive map.
first_1:
			int drvtype=GetDriveType(name);
			if(drvtype==1 && strlen(name)==2 && name[1]==':') {
				char names[4];
				strcpy(names,name);
				strcat(names,"\\");
				drvtype=GetDriveType(names);
			}
			if(drvtype==2) {
				if(strcmp(auto_string, "auto")) {
					WriteOut(MSG_Get("SHELL_EXECUTE_AUTOMOUNT"));
					WriteOut("\n");
					WriteOut(MSG_Get("SHELL_EXECUTE_DRIVE_ACCESS_REMOVABLE"),toupper(name[0]));
				}
			} else if(drvtype==4) {
				if(strcmp(auto_string, "auto")) {
					WriteOut(MSG_Get("SHELL_EXECUTE_AUTOMOUNT"));
					WriteOut("\n");
					WriteOut(MSG_Get("SHELL_EXECUTE_DRIVE_ACCESS_NETWORK"),toupper(name[0]));
				}
			} else if(drvtype==5) {
				if(strcmp(auto_string, "auto")) {
					WriteOut(MSG_Get("SHELL_EXECUTE_AUTOMOUNT"));
					WriteOut("\n");
					WriteOut(MSG_Get("SHELL_EXECUTE_DRIVE_ACCESS_OPTICAL"),toupper(name[0]));
				}
			} else if(drvtype==3||drvtype==6) {
				if(strcmp(auto_string, "auto")) {
					WriteOut(MSG_Get("SHELL_EXECUTE_AUTOMOUNT"));
					if(drvtype==3 && strcasecmp(name,"c:")==0)
						WriteOut(MSG_Get("SHELL_EXECUTE_DRIVE_ACCESS_WARNING_WIN"));
					WriteOut("\n");
					WriteOut(MSG_Get("SHELL_EXECUTE_DRIVE_ACCESS_LOCAL"),toupper(name[0]));
				}
			} else {
				WriteOut(MSG_Get("SHELL_EXECUTE_DRIVE_NOT_FOUND"),toupper(name[0]));
				return true;
			}

first_2:
		Bit8u c;Bit16u n=1;
		if(strcmp(auto_string, "auto")) {
			DOS_ReadFile (STDIN,&c,&n);
			do switch (c) {
				case 'n':			case 'N':
				{
					DOS_WriteFile (STDOUT,&c, &n);
					DOS_ReadFile (STDIN,&c,&n);
					do switch (c) {
						case 0xD: WriteOut("\n\n"); WriteOut(MSG_Get("SHELL_EXECUTE_DRIVE_NOT_FOUND"),toupper(name[0])); return true;
						case 0x08: WriteOut("\b \b"); goto first_2;
					} while (DOS_ReadFile (STDIN,&c,&n));
				}
				case 'y':			case 'Y':
				{
					DOS_WriteFile (STDOUT,&c, &n);
					DOS_ReadFile (STDIN,&c,&n);
					do switch (c) {
						case 0xD: WriteOut("\n"); goto continue_1;
						case 0x08: WriteOut("\b \b"); goto first_2;
					} while (DOS_ReadFile (STDIN,&c,&n));
				}
				case 0xD: WriteOut("\n"); goto first_1;
				case '\t': case 0x08: goto first_2;
				default:
				{
					DOS_WriteFile (STDOUT,&c, &n);
					DOS_ReadFile (STDIN,&c,&n);
					do switch (c) {
						case 0xD: WriteOut("\n");goto first_1;
						case 0x08: WriteOut("\b \b"); goto first_2;
					} while (DOS_ReadFile (STDIN,&c,&n));
					goto first_2;
				}
			} while (DOS_ReadFile (STDIN,&c,&n));
		}
continue_1:

			char mountstring[DOS_PATHLENGTH+CROSS_LEN+20];
			sprintf(mountstring,"MOUNT %s ",name);

			if(GetDriveType(name)==5) strcat(mountstring,"-t cdrom ");
			else if(GetDriveType(name)==2) strcat(mountstring,"-t floppy ");
			strcat(mountstring,name);
			strcat(mountstring,"\\");
//			if(GetDriveType(name)==5) strcat(mountstring," -ioctl");
			
			this->ParseLine(mountstring);
			if (!DOS_SetDrive(toupper(name[0])-'A'))
#endif
			WriteOut(MSG_Get("SHELL_EXECUTE_DRIVE_NOT_FOUND"),toupper(name[0]));
		}
		return true;
	}
	/* Check for a full name */
	p_fullname = Which(name);
	if (!p_fullname) return false;
	strcpy(fullname,p_fullname);
	const char* extension = strrchr(fullname,'.');
	
	/*always disallow files without extension from being executed. */
	/*only internal commands can be run this way and they never get in this handler */
	if(extension == 0)
	{
		//Check if the result will fit in the parameters. Else abort
		if(strlen(fullname) >( DOS_PATHLENGTH - 1) ) return false;
		char temp_name[DOS_PATHLENGTH+4],* temp_fullname;
		//try to add .com, .exe and .bat extensions to filename
		
		strcpy(temp_name,fullname);
		strcat(temp_name,".COM");
		temp_fullname=Which(temp_name);
		if (temp_fullname) { extension=".com";strcpy(fullname,temp_fullname); }

		else 
		{
			strcpy(temp_name,fullname);
			strcat(temp_name,".EXE");
			temp_fullname=Which(temp_name);
		 	if (temp_fullname) { extension=".exe";strcpy(fullname,temp_fullname);}

			else 
			{
				strcpy(temp_name,fullname);
				strcat(temp_name,".BAT");
				temp_fullname=Which(temp_name);
		 		if (temp_fullname) { extension=".bat";strcpy(fullname,temp_fullname);}

				else  
				{
		 			return false;
				}
			
			}	
		}
	}
	
	if (strcasecmp(extension, ".bat") == 0) 
	{	/* Run the .bat file */
		/* delete old batch file if call is not active*/
		bool temp_echo=echo; /*keep the current echostate (as delete bf might change it )*/
		if(bf && !call) delete bf;
		bf=new BatchFile(this,fullname,name,line);
		echo=temp_echo; //restore it.
	} 
	else 
	{	/* only .bat .exe .com extensions maybe be executed by the shell */
		if(strcasecmp(extension, ".com") !=0) 
		{
			if(strcasecmp(extension, ".exe") !=0) return false;
		}
		/* Run the .exe or .com file from the shell */
		/* Allocate some stack space for tables in physical memory */
		reg_sp-=0x200;
		//Add Parameter block
		DOS_ParamBlock block(SegPhys(ss)+reg_sp);
		block.Clear();
		//Add a filename
		RealPt file_name=RealMakeSeg(ss,reg_sp+0x20);
		MEM_BlockWrite(Real2Phys(file_name),fullname,(Bitu)(strlen(fullname)+1));

		/* HACK: Store full commandline for mount and imgmount */
		full_arguments.assign(line);

		/* Fill the command line */
		CommandTail cmdtail;
		cmdtail.count = 0;
		memset(&cmdtail.buffer,0,CTBUF); //Else some part of the string is unitialized (valgrind)
		if (strlen(line)>=CTBUF) line[CTBUF-1]=0;
		cmdtail.count=(Bit8u)strlen(line);
		memcpy(cmdtail.buffer,line,strlen(line));
		cmdtail.buffer[strlen(line)]=0xd;
		/* Copy command line in stack block too */
		MEM_BlockWrite(SegPhys(ss)+reg_sp+0x100,&cmdtail,CTBUF+1);

		
		/* Split input line up into parameters, using a few special rules, most notable the one for /AAA => A\0AA
		 * Qbix: It is extremly messy, but this was the only way I could get things like /:aa and :/aa to work correctly */
		
		//Prepare string first
		char parseline[258] = { 0 };
		for(char *pl = line,*q = parseline; *pl ;pl++,q++) {
			if (*pl == '=' || *pl == ';' || *pl ==',' || *pl == '\t' || *pl == ' ') *q = 0; else *q = *pl; //Replace command seperators with 0.
		} //No end of string \0 needed as parseline is larger than line

		for(char* p = parseline; (p-parseline) < 250 ;p++) { //Stay relaxed within boundaries as we have plenty of room
			if (*p == '/') { //Transform /Hello into H\0ello
				*p = 0;
				p++;
				while ( *p == 0 && (p-parseline) < 250) p++; //Skip empty fields
				if ((p-parseline) < 250) { //Found something. Lets get the first letter and break it up
					p++;
					memmove(static_cast<void*>(p + 1),static_cast<void*>(p),(250-(p-parseline)));
					if ((p-parseline) < 250) *p = 0;
				}
			}
		}
		parseline[255] = parseline[256] = parseline[257] = 0; //Just to be safe.

		/* Parse FCB (first two parameters) and put them into the current DOS_PSP */
		Bit8u add;
		Bit16u skip = 0;
		//find first argument, we end up at parseline[256] if there is only one argument (similar for the second), which exists and is 0.
		while(skip < 256 && parseline[skip] == 0) skip++;
		FCB_Parsename(dos.psp(),0x5C,0x01,parseline + skip,&add);
		skip += add;
		
		//Move to next argument if it exists
		while(parseline[skip] != 0) skip++;  //This is safe as there is always a 0 in parseline at the end.
		while(skip < 256 && parseline[skip] == 0) skip++; //Which is higher than 256
		FCB_Parsename(dos.psp(),0x6C,0x01,parseline + skip,&add); 

		block.exec.fcb1=RealMake(dos.psp(),0x5C);
		block.exec.fcb2=RealMake(dos.psp(),0x6C);
		/* Set the command line in the block and save it */
		block.exec.cmdtail=RealMakeSeg(ss,reg_sp+0x100);
		block.SaveData();
#if 0
		/* Save CS:IP to some point where i can return them from */
		Bit32u oldeip=reg_eip;
		Bit16u oldcs=SegValue(cs);
		RealPt newcsip=CALLBACK_RealPointer(call_shellstop);
		SegSet16(cs,RealSeg(newcsip));
		reg_ip=RealOff(newcsip);
#endif
		/* Start up a dos execute interrupt */
		reg_ax=0x4b00;
		//Filename pointer
		SegSet16(ds,SegValue(ss));
		reg_dx=RealOff(file_name);
		//Paramblock
		SegSet16(es,SegValue(ss));
		reg_bx=reg_sp;
		SETFLAGBIT(IF,false);
		CALLBACK_RunRealInt(0x21);
		/* Restore CS:IP and the stack */
		reg_sp+=0x200;
#if 0
		reg_eip=oldeip;
		SegSet16(cs,oldcs);
#endif
	}
	return true; //Executable started
}




static const char * bat_ext=".BAT";
static const char * com_ext=".COM";
static const char * exe_ext=".EXE";
static char which_ret[DOS_PATHLENGTH+4];

char * DOS_Shell::Which(char * name) {
	size_t name_len = strlen(name);
	if(name_len >= DOS_PATHLENGTH) return 0;

	/* Parse through the Path to find the correct entry */
	/* Check if name is already ok but just misses an extension */

	if (DOS_FileExists(name)) return name;
	/* try to find .com .exe .bat */
	strcpy(which_ret,name);
	strcat(which_ret,com_ext);
	if (DOS_FileExists(which_ret)) return which_ret;
	strcpy(which_ret,name);
	strcat(which_ret,exe_ext);
	if (DOS_FileExists(which_ret)) return which_ret;
	strcpy(which_ret,name);
	strcat(which_ret,bat_ext);
	if (DOS_FileExists(which_ret)) return which_ret;


	/* No Path in filename look through path environment string */
	char path[DOS_PATHLENGTH];std::string temp;
	if (!GetEnvStr("PATH",temp)) return 0;
	const char * pathenv=temp.c_str();
	if (!pathenv) return 0;
	pathenv = strchr(pathenv,'=');
	if (!pathenv) return 0;
	pathenv++;
	Bitu i_path = 0;
	while (*pathenv) {
		/* remove ; and ;; at the beginning. (and from the second entry etc) */
		while(*pathenv == ';')
			pathenv++;

		/* get next entry */
		i_path = 0; /* reset writer */
		while(*pathenv && (*pathenv !=';') && (i_path < DOS_PATHLENGTH) )
			path[i_path++] = *pathenv++;

		if(i_path == DOS_PATHLENGTH) {
			/* If max size. move till next ; and terminate path */
			while(*pathenv && (*pathenv != ';')) 
				pathenv++;
			path[DOS_PATHLENGTH - 1] = 0;
		} else path[i_path] = 0;


		/* check entry */
		if(size_t len = strlen(path)){
			if(len >= (DOS_PATHLENGTH - 2)) continue;

			if(path[len - 1] != '\\') {
				strcat(path,"\\"); 
				len++;
			}

			//If name too long =>next
			if((name_len + len + 1) >= DOS_PATHLENGTH) continue;
			strcat(path,name);

			strcpy(which_ret,path);
			if (DOS_FileExists(which_ret)) return which_ret;
			strcpy(which_ret,path);
			strcat(which_ret,com_ext);
			if (DOS_FileExists(which_ret)) return which_ret;
			strcpy(which_ret,path);
			strcat(which_ret,exe_ext);
			if (DOS_FileExists(which_ret)) return which_ret;
			strcpy(which_ret,path);
			strcat(which_ret,bat_ext);
			if (DOS_FileExists(which_ret)) return which_ret;
		}
	}
	return 0;
}
