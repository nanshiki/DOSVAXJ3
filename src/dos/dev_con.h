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
 *  Wengier: DBCS support
 */


#include "dos_inc.h"
#include "../ints/int10.h"
#include "jega.h"
#include <string.h>

#define NUMBER_ANSI_DATA 10

extern bool CheckHat(Bit8u code);
extern bool LineInputFlag;

class device_CON : public DOS_Device {
public:
	device_CON();
	bool Read(Bit8u * data,Bit16u * size);
	bool Write(Bit8u * data,Bit16u * size);
	bool Seek(Bit32u * pos,Bit32u type);
	bool Close();
	void ClearAnsi(void);
	Bit16u GetInformation(void);
	bool ReadFromControlChannel(PhysPt /*bufptr*/,Bit16u /*size*/,Bit16u * /*retcode*/){return false;}
	bool WriteToControlChannel(PhysPt /*bufptr*/,Bit16u /*size*/,Bit16u * /*retcode*/){return false;}
	void ClearKeyMap() {
		key_map.clear();
	}
	void SetKeyMap(Bit16u src, Bit16u dst) {
		struct key_change key;
		key.src = src;
		key.dst = dst;
		key_map.push_back(key);
	}
private:
	Bit8u readcache;
	Bit8u lastwrite;
	struct ansi { /* should create a constructor, which would fill them with the appropriate values */
		bool esc;
		bool sci;
		bool enabled;
		Bit8u attr;
		Bit8u data[NUMBER_ANSI_DATA];
		Bit8u numberofarg;
		Bit8s savecol;
		Bit8s saverow;
		bool warned;
		bool key;
	} ansi;
	Bit16u keepcursor;

	struct key_change {
		Bit16u	src;
		Bit16u	dst;
	};
	std::vector<struct key_change> key_map;
	bool lasthat;
};

bool device_CON::Read(Bit8u * data,Bit16u * size) {
	Bit16u oldax=reg_ax;
	Bit16u count=0;
	INT10_SetCurMode();
	if ((readcache) && (*size)) {
		data[count++]=readcache;
		if (dos.echo) {
			if(IS_J3_ARCH || dos.set_ax_enabled || IS_DOSV) {
				reg_al = readcache;
				CALLBACK_RunRealInt(0x29);
			} else {
				INT10_TeletypeOutput_viaRealInt(readcache, 7);
			}
		}
		readcache=0;
	}
	while (*size>count) {
		reg_ah=(IS_EGAVGA_ARCH)?0x10:0x0;
		CALLBACK_RunRealInt(0x16);
		switch(reg_al) {
		case 13:
			data[count++]=0x0D;
			if (*size>count) data[count++]=0x0A;    // it's only expanded if there is room for it. (NO cache)
			*size=count;
			reg_ax=oldax;
			if(dos.echo) { 
				if(IS_J3_ARCH || dos.set_ax_enabled || IS_DOSV) {
					reg_al = 13;
					CALLBACK_RunRealInt(0x29);
					reg_al = 10;
					CALLBACK_RunRealInt(0x29);
				} else {
					INT10_TeletypeOutput_viaRealInt(13, 7); //maybe don't do this ( no need for it actually ) (but it's compatible)
					INT10_TeletypeOutput_viaRealInt(10, 7);
				}
			}
			return true;
			break;
		case 8:
			if(*size==1) data[count++]=reg_al;  //one char at the time so give back that BS
			else if(count) {                    //Remove data if it exists (extended keys don't go right)
				Bit8u flag = 0;
				if(count > 1) {
					for(Bit16u pos = 0 ; pos < count ; pos++) {
						if(flag == 1) {
							flag = 2;
						} else {
							flag = 0;
							if(isKanji1(data[pos])) {
								flag = 1;
							}
						}
					}
				}
				if(flag == 2) {
					data[count--]=0;
					data[count--]=0;
					INT10_TeletypeOutput_viaRealInt(8, 7);
					INT10_TeletypeOutput_viaRealInt(8, 7);
					INT10_TeletypeOutput_viaRealInt(' ', 7);
					INT10_TeletypeOutput_viaRealInt(' ', 7);
					INT10_TeletypeOutput_viaRealInt(8, 7);
				} else {
					data[count--]=0;
					INT10_TeletypeOutput_viaRealInt(8, 7);
					INT10_TeletypeOutput_viaRealInt(' ', 7);
				}
			} else {
				continue;                       //no data read yet so restart whileloop.
			}
			break;
		case 0xe0: /* Extended keys in the  int 16 0x10 case */
			if((IS_J3_ARCH || isJEGAEnabled() || IS_DOSV) && (reg_ah == 0xf0 || reg_ah == 0xf1)) {
				data[count++]=reg_al;
			} else {
				if(!reg_ah) { /*extended key if reg_ah isn't 0 */
					data[count++] = reg_al;
				} else {
					data[count++] = 0;
					if (*size>count) data[count++] = reg_ah;
					else readcache = reg_ah;
				}
			}
			break;
		case 0: /* Extended keys in the int 16 0x0 case */
			for(std::vector<key_change>::iterator key = key_map.begin() ; key != key_map.end() ; ++key) {
				if(key->src == reg_ah << 8) {
					reg_al = (Bit8u)key->dst;
					reg_ah = (Bit8u)(key->dst >> 8);
					break;
				}
			}
			data[count++]=reg_al;
			if (*size>count) data[count++]=reg_ah;
			else readcache=reg_ah;
			break;
		default:
			for(std::vector<key_change>::iterator key = key_map.begin() ; key != key_map.end() ; ++key) {
				if(key->src == reg_al) {
					reg_al = (Bit8u)key->dst;
				}
			}
			data[count++]=reg_al;
			if (*size > 1 && reg_al == 3) {
				dos.errorcode=77;
				*size=count;
				reg_ax=oldax;
				return false;
			}
			break;
		}
		if(dos.echo) { //what to do if *size==1 and character is BS ?????
			if(IS_J3_ARCH || dos.set_ax_enabled || IS_DOSV) {
				if(LineInputFlag && CheckHat(reg_al)) {
					Bit8u ch = reg_al + 0x40;
					reg_al = '^';
					CALLBACK_RunRealInt(0x29);
					reg_al = ch;
					CALLBACK_RunRealInt(0x29);
					lasthat = true;
				} else {
					CALLBACK_RunRealInt(0x29);
					if(lasthat && reg_al == 0x08) {
						CALLBACK_RunRealInt(0x29);
						reg_al = 0x20;
						CALLBACK_RunRealInt(0x29);
						reg_al = 0x08;
						CALLBACK_RunRealInt(0x29);
					}
					lasthat = false;
				}
			} else {
				INT10_TeletypeOutput_viaRealInt(reg_al, 7);
			}
		}
	}
	*size=count;
	reg_ax=oldax;
	return true;
}

bool device_CON::Write(Bit8u * data,Bit16u * size) {
	Bit16u count = 0;
	if(IS_J3_ARCH || dos.set_ax_enabled || IS_DOSV) {
		while (*size > count) {
			reg_al = data[count];
			if(reg_al == 0x07) {
				INT10_TeletypeOutput(reg_al, 7);
			} else {
				if(LineInputFlag && CheckHat(reg_al)) {
					Bit8u ch = reg_al + 0x40;
					reg_al = '^';
					CALLBACK_RunRealInt(0x29);
					reg_al = ch;
					CALLBACK_RunRealInt(0x29);
				} else {
					CALLBACK_RunRealInt(0x29);
				}
			}
			count++;
		}
	} else {
		Bitu i;
		Bit8u col,row,page;
		Bit16u ncols,nrows;
		Bit8u tempdata;
		INT10_SetCurMode();


		while (*size>count) {
			if (!ansi.esc){
				if(data[count]=='\033') {
					/*clear the datastructure */
					ClearAnsi();
					/* start the sequence */
					ansi.esc=true;
					count++;
					continue;
				} else if(data[count] == '\t' && !dos.direct_output) {
					/* expand tab if not direct output */
					page = real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE);
					do {
						if(ansi.enabled) INT10_TeletypeOutputAttr(' ',ansi.attr,true);
						else INT10_TeletypeOutput(' ',7);
						col=CURSOR_POS_COL(page);
					} while(col%8);
					lastwrite = data[count++];
					continue;
				} else { 
					/* Some sort of "hack" now that '\n' doesn't set col to 0 (int10_char.cpp old chessgame) */
					if((data[count] == '\n') && (lastwrite != '\r')) {
						if(ansi.enabled) INT10_TeletypeOutputAttr_viaRealInt('\r', ansi.attr, ansi.enabled);
						else {
							INT10_TeletypeOutput('\r',7);
						}
					}
					/* use ansi attribute if ansi is enabled, otherwise use DOS default attribute*/
					if(ansi.enabled) INT10_TeletypeOutputAttr_viaRealInt(data[count], ansi.attr, ansi.enabled);
					else {
						INT10_TeletypeOutput(data[count],7);
					}
					lastwrite = data[count++];
					continue;
			}
		}

		if(!ansi.sci){
			switch(data[count]){
			case '[': 
				ansi.sci=true;
				break;
			case '7': /* save cursor pos + attr */
			case '8': /* restore this  (Wonder if this is actually used) */
			case 'D':/* scrolling DOWN*/
			case 'M':/* scrolling UP*/ 
			default:
				LOG(LOG_IOCTL,LOG_NORMAL)("ANSI: unknown char %c after a esc",data[count]); /*prob () */
				ClearAnsi();
				break;
			}
			count++;
			continue;
		}
		/*ansi.esc and ansi.sci are true */
		page = real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE);
		if(ansi.key) {
			if(data[count] == '"') {
				ansi.key = false;
				count++;
			} else {
				if(ansi.numberofarg < NUMBER_ANSI_DATA) {
					ansi.data[ansi.numberofarg++] = data[count++];
				}
			}
		} else {
		  switch(data[count]){
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				ansi.data[ansi.numberofarg]=10*ansi.data[ansi.numberofarg]+(data[count]-'0');
				break;
			case ';': /* till a max of NUMBER_ANSI_DATA */
				ansi.numberofarg++;
				break;
			case 'm':               /* SGR */
				for(i=0;i<=ansi.numberofarg;i++){ 
					ansi.enabled=true;
					switch(ansi.data[i]){
					case 0: /* normal */
						ansi.attr=0x07;//Real ansi does this as well. (should do current defaults)
						ansi.enabled=false;
						break;
					case 1: /* bold mode on*/
						ansi.attr|=0x08;
						break;
					case 4: /* underline */
						LOG(LOG_IOCTL,LOG_NORMAL)("ANSI:no support for underline yet");
						break;
					case 5: /* blinking */
						ansi.attr|=0x80;
						break;
					case 7: /* reverse */
						ansi.attr=0x70;//Just like real ansi. (should do use current colors reversed)
						break;
					case 30: /* fg color black */
						ansi.attr&=0xf8;
						ansi.attr|=0x0;
						break;
					case 31:  /* fg color red */
						ansi.attr&=0xf8;
						ansi.attr|=0x4;
						break;
					case 32:  /* fg color green */
						ansi.attr&=0xf8;
						ansi.attr|=0x2;
						break;
					case 33: /* fg color yellow */
						ansi.attr&=0xf8;
						ansi.attr|=0x6;
						break;
					case 34: /* fg color blue */
						ansi.attr&=0xf8;
						ansi.attr|=0x1;
						break;
					case 35: /* fg color magenta */
						ansi.attr&=0xf8;
						ansi.attr|=0x5;
						break;
					case 36: /* fg color cyan */
						ansi.attr&=0xf8;
						ansi.attr|=0x3;
						break;
					case 37: /* fg color white */
						ansi.attr&=0xf8;
						ansi.attr|=0x7;
						break;
					case 40:
						ansi.attr&=0x8f;
						ansi.attr|=0x0;
						break;
					case 41:
						ansi.attr&=0x8f;
						ansi.attr|=0x40;
						break;
					case 42:
						ansi.attr&=0x8f;
						ansi.attr|=0x20;
						break;
					case 43:
						ansi.attr&=0x8f;
						ansi.attr|=0x60;
						break;
					case 44:
						ansi.attr&=0x8f;
						ansi.attr|=0x10;
						break;
					case 45:
						ansi.attr&=0x8f;
						ansi.attr|=0x50;
						break;
					case 46:
						ansi.attr&=0x8f;
						ansi.attr|=0x30;
						break;	
					case 47:
						ansi.attr&=0x8f;
						ansi.attr|=0x70;
						break;
					default:
						break;
					}
				}
				ClearAnsi();
				break;
			case 'f':
			case 'H':/* Cursor Pos*/
				if(!ansi.warned) { //Inform the debugger that ansi is used.
					ansi.warned = true;
					LOG(LOG_IOCTL,LOG_WARN)("ANSI SEQUENCES USED");
				}
				ncols = real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS);
				nrows = IS_EGAVGA_ARCH ? (real_readb(BIOSMEM_SEG,BIOSMEM_NB_ROWS) + 1) : 25;
				/* Turn them into positions that are on the screen */
				if(ansi.data[0] == 0) ansi.data[0] = 1;
				if(ansi.data[1] == 0) ansi.data[1] = 1;
				if(ansi.data[0] > nrows) ansi.data[0] = (Bit8u)nrows;
				if(ansi.data[1] > ncols) ansi.data[1] = (Bit8u)ncols;
				INT10_SetCursorPos_viaRealInt(--(ansi.data[0]), --(ansi.data[1]), page); /*ansi=1 based, int10 is 0 based */
				ClearAnsi();
				break;
				/* cursor up down and forward and backward only change the row or the col not both */
			case 'A': /* cursor up*/
				col=CURSOR_POS_COL(page) ;
				row=CURSOR_POS_ROW(page) ;
				tempdata = (ansi.data[0]? ansi.data[0] : 1);
				if(tempdata > row) { row=0; } 
				else { row-=tempdata;}
				INT10_SetCursorPos_viaRealInt(row, col, page);
				ClearAnsi();
				break;
			case 'B': /*cursor Down */
				col=CURSOR_POS_COL(page) ;
				row=CURSOR_POS_ROW(page) ;
				nrows = IS_EGAVGA_ARCH ? (real_readb(BIOSMEM_SEG,BIOSMEM_NB_ROWS) + 1) : 25;
				tempdata = (ansi.data[0]? ansi.data[0] : 1);
				if(tempdata + static_cast<Bitu>(row) >= nrows)
					{ row = nrows - 1;}
				else	{ row += tempdata; }
				INT10_SetCursorPos_viaRealInt(row, col, page);
				ClearAnsi();
				break;
			case 'C': /*cursor forward */
				col=CURSOR_POS_COL(page);
				row=CURSOR_POS_ROW(page);
				ncols = real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS);
				tempdata=(ansi.data[0]? ansi.data[0] : 1);
				if(tempdata + static_cast<Bitu>(col) >= ncols) 
					{ col = ncols - 1;} 
				else	{ col += tempdata;}
				INT10_SetCursorPos_viaRealInt(row, col, page);
				ClearAnsi();
				break;
			case 'D': /*Cursor Backward  */
				col=CURSOR_POS_COL(page);
				row=CURSOR_POS_ROW(page);
				tempdata=(ansi.data[0]? ansi.data[0] : 1);
				if(tempdata > col) {col = 0;}
				else { col -= tempdata;}
				INT10_SetCursorPos_viaRealInt(row, col, page);
				ClearAnsi();
				break;
			case 'J': /*erase screen and move cursor home*/
				if(ansi.data[0]==0) ansi.data[0]=2;
				if(ansi.data[0]!=2) {/* every version behaves like type 2 */
					LOG(LOG_IOCTL,LOG_NORMAL)("ANSI: esc[%dJ called : not supported handling as 2",ansi.data[0]);
				}
				INT10_ScrollWindow_viaRealInt(0, 0, 255, 255, 0, ansi.attr, page);
				ClearAnsi();
				INT10_SetCursorPos_viaRealInt(0, 0, page);
				break;
			case 'h': /* SET   MODE (if code =7 enable linewrap) */
				if(IS_J3_ARCH) {
					if(ansi.data[ansi.numberofarg] == 5) {
						// disable cursor
						keepcursor = real_readw(BIOSMEM_SEG, BIOSMEM_CURSOR_TYPE);
						INT10_SetCursorShape(0x20, 0x0f);
					}
				}
			case 'I': /* RESET MODE */
				LOG(LOG_IOCTL,LOG_NORMAL)("ANSI: set/reset mode called(not supported)");
				ClearAnsi();
				break;
			case 'u': /* Restore Cursor Pos */
				INT10_SetCursorPos_viaRealInt(ansi.saverow, ansi.savecol, page);
				ClearAnsi();
				break;
			case 's': /* SAVE CURSOR POS */
				ansi.savecol=CURSOR_POS_COL(page);
				ansi.saverow=CURSOR_POS_ROW(page);
				ClearAnsi();
				break;
			case 'K': /* erase till end of line (don't touch cursor) */
				col = CURSOR_POS_COL(page);
				row = CURSOR_POS_ROW(page);
				ncols = real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS);
				INT10_WriteChar_viaRealInt(' ', ansi.attr, page, ncols-col, true); //Use this one to prevent scrolling when end of screen is reached
				//for(i = col;i<(Bitu) ncols; i++) INT10_TeletypeOutputAttr(' ',ansi.attr,true);
				INT10_SetCursorPos_viaRealInt(row, col, page);
				ClearAnsi();
				break;
			case 'M': /* delete line (NANSI) */
				row = CURSOR_POS_ROW(page);
				ncols = real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS);
				nrows = IS_EGAVGA_ARCH ? (real_readb(BIOSMEM_SEG,BIOSMEM_NB_ROWS) + 1) : 25;
				INT10_ScrollWindow_viaRealInt(row, 0, nrows-1, ncols - 1, ansi.data[0] ? -ansi.data[0] : -1, ansi.attr, 0xFF);
				ClearAnsi();
				break;
			case '>':
				break;
			case 'l':/* (if code =7) disable linewrap */
				if(IS_J3_ARCH) {
					if(ansi.data[ansi.numberofarg] == 5) {
						// enable cursor
						INT10_SetCursorShape(keepcursor >> 8, keepcursor & 0xff);
					}
					ClearAnsi();
					break;
				}
			case 'p':/* reassign keys (needs strings) */
				{
					struct key_change key;
					i = 0;
					if(ansi.data[i] == 0) {
						i++;
						key.src = ansi.data[i++] << 8;
					} else {
						key.src = ansi.data[i++];
					}
					if(ansi.data[i] == 0) {
						i++;
						key.dst = ansi.data[i++] << 8;
					} else {
						key.dst = ansi.data[i++];
					}
					key_map.push_back(key);
					ClearAnsi();
				}
				break;
			case '"':
				if(!ansi.key) {
					ansi.key = true;
					ansi.numberofarg = 0;
				}
				break;
			case 'i':/* printer stuff */
			default:
				LOG(LOG_IOCTL,LOG_NORMAL)("ANSI: unhandled char %c in esc[",data[count]);
				ClearAnsi();
				break;
			}
			count++;
		  }
		}
	}
	*size = count;
	return true;
}

bool device_CON::Seek(Bit32u * pos,Bit32u /*type*/) {
	// seek is valid
	*pos = 0;
	return true;
}

bool device_CON::Close() {
	return true;
}

Bit16u device_CON::GetInformation(void) {
	Bit16u head=mem_readw(BIOS_KEYBOARD_BUFFER_HEAD);
	Bit16u tail=mem_readw(BIOS_KEYBOARD_BUFFER_TAIL);

	if ((head==tail) && !readcache) return 0x80D3;	/* No Key Available */
	if (readcache || real_readw(0x40,head)) return 0x8093;		/* Key Available */

	/* remove the zero from keyboard buffer */
	Bit16u start=mem_readw(BIOS_KEYBOARD_BUFFER_START);
	Bit16u end	=mem_readw(BIOS_KEYBOARD_BUFFER_END);
	head+=2;
	if (head>=end) head=start;
	mem_writew(BIOS_KEYBOARD_BUFFER_HEAD,head);
	return 0x80D3; /* No Key Available */
}

device_CON::device_CON() {
	SetName("CON");
	readcache=0;
	lastwrite=0;
	ansi.enabled=false;
	ansi.attr=0x7;
	ansi.saverow=0;
	ansi.savecol=0;
	ansi.warned=false;
	ansi.key=false;
	ClearAnsi();
}

void device_CON::ClearAnsi(void){
	for(Bit8u i=0; i<NUMBER_ANSI_DATA;i++) ansi.data[i]=0;
	ansi.esc=false;
	ansi.sci=false;
	ansi.numberofarg=0;
	ansi.key=false;
}
