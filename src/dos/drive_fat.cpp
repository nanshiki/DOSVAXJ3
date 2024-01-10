/*
 *  Copyright (C) 2002-2017  The DOSBox Team
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
 *
 *  Wengier: LFN support
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "dosbox.h"
#include "dos_inc.h"
#include "regs.h"
#include "callback.h"
#include "drives.h"
#include "support.h"
#include "cross.h"
#include "bios.h"
#include "bios_disk.h"

#if defined(WIN32)
#define	fseek	_fseeki64
#define	ftell	_ftelli64
#endif

#define IMGTYPE_FLOPPY 0
#define IMGTYPE_ISO    1
#define IMGTYPE_HDD	   2

#define FAT12		   0
#define FAT16		   1
#define FAT32		   2

static Bit16u dpos[256];
static Bit32u dnum[256];
extern bool wpcolon, force;
extern bool isKanji1(Bit8u chr);
extern int lfn_filefind_handle;
void dos_ver_menu(bool start);
char sfn[DOS_NAMELENGTH_ASCII];

bool filename_not_8x3(const char *n) {
	bool lead;
	unsigned int i;

	i = 0;
	lead = false;
	while (*n != 0) {
		if (*n == '.') break;
		if ((*n&0xFF)<=32||*n==127||*n=='"'||*n=='+'||*n=='='||*n==','||*n==';'||*n==':'||*n=='<'||*n=='>'||(*n=='['||*n==']'||*n=='|')&&!lead||*n=='?'||*n=='*') return true;
		if (lead) lead = false;
		else if (dos.loaded_codepage == 932 && isKanji1(*n&0xFF)) lead = true;
		i++;
		n++;
	}
	if (i > 8) return true;
	if (*n == 0) return false; /* made it past 8 or less normal chars and end of string: normal */

	/* skip dot */
	if (*n != '.') return true;
	n++;

	i = 0;
	lead = false;
	while (*n != 0) {
		if (*n == '.') return true; /* another '.' means LFN */
		if ((*n&0xFF)<=32||*n==127||*n=='"'||*n=='+'||*n=='='||*n==','||*n==';'||*n==':'||*n=='<'||*n=='>'||(*n=='['||*n==']'||*n=='|')&&!lead||*n=='?'||*n=='*') return true;
		if (lead) lead = false;
		else if (dos.loaded_codepage == 932 && isKanji1(*n&0xFF)) lead = true;
		i++;
		n++;
	}
	if (i > 3) return true;

	return false; /* it is 8.3 case */
}

/* Assuming an LFN call, if the name is not strict 8.3 uppercase, return true.
 * If the name is strict 8.3 uppercase like "FILENAME.TXT" there is no point making an LFN because it is a waste of space */
bool filename_not_strict_8x3(const char *n) {
	if (filename_not_8x3(n)) return true;
	bool lead = false;
	for (unsigned int i=0; i<strlen(n); i++) {
		if (lead) lead = false;
		else if (dos.loaded_codepage == 932 && isKanji1(*n&0xFF)) lead = true;
		else if (n[i]>='a' && n[i]<='z') return true;
	}
	return false; /* it is strict 8.3 upper case */
}

void GenerateSFN(char *lfn, unsigned int k, unsigned int &i, unsigned int &t) {
    char *n=lfn;
    if (t>strlen(n)||k==1||k==10||k==100||k==1000) {
        i=0;
        *sfn=0;
        while (*n == '.'||*n == ' ') n++;
        while (strlen(n)&&(*(n+strlen(n)-1)=='.'||*(n+strlen(n)-1)==' ')) *(n+strlen(n)-1)=0;
        bool lead = false;
        unsigned int m = k<10?6u:(k<100?5u:(k<1000?4:3u));
        while (*n != 0 && *n != '.' && i < m) {
            if (*n == ' ') {
                n++;
                lead = false;
                continue;
            }
            if (!lead && (dos.loaded_codepage == 932 && isKanji1(*n & 0xFF))) {
                if (i==m-1) break;
                sfn[i++]=*(n++);
                lead = true;
            } else if (*n=='"'||*n=='+'||*n=='='||*n==','||*n==';'||*n==':'||*n=='<'||*n=='>'||(*n=='['||*n==']'||*n=='|')&&!lead||*n=='?'||*n=='*') {
                sfn[i++]='_';
                n++;
                lead = false;
            } else {
                sfn[i++]=lead?*n:toupper(*n);
                n++;
                lead = false;
            }
        }
        sfn[i++]='~';
        t=i;
    } else
        i=t;
    if (k<10)
        sfn[i++]='0'+k;
    else if (k<100) {
        sfn[i++]='0'+(k/10);
        sfn[i++]='0'+(k%10);
    } else if (k<1000) {
        sfn[i++]='0'+(k/100);
        sfn[i++]='0'+((k%100)/10);
        sfn[i++]='0'+(k%10);
    } else {
        sfn[i++]='0'+(k/1000);
        sfn[i++]='0'+((k%1000)/100);
        sfn[i++]='0'+((k%100)/10);
        sfn[i++]='0'+(k%10);
    }
    if (t>strlen(n)||k==1||k==10||k==100||k==1000) {
        char *p=strrchr(n, '.');
        if (p!=NULL) {
            sfn[i++]='.';
            n=p+1;
            while (*n == '.') n++;
            int j=0;
            bool lead = false;
            while (*n != 0 && j++<3) {
                if (*n == ' ') {
                    n++;
                    lead = false;
                    continue;
                }
                if (!lead && (dos.loaded_codepage == 932 && isKanji1(*n & 0xFF))) {
                    if (j==3) break;
                    sfn[i++]=*(n++);
                    lead = true;
                } else if (*n=='"'||*n=='+'||*n=='='||*n==','||*n==';'||*n==':'||*n=='<'||*n=='>'||(*n=='['||*n==']'||*n=='|')&&!lead||*n=='?'||*n=='*') {
                    sfn[i++]='_';
                    n++;
                    lead = false;
                } else {
                    sfn[i++]=lead?*n:toupper(*n);
                    n++;
                    lead = false;
                }
            }
        }
        sfn[i++]=0;
    }
}

/* Generate 8.3 names from LFNs, with tilde usage (from ~1 to ~9999). */
char* fatDrive::Generate_SFN(const char *path, const char *name) {
	if (!filename_not_8x3(name)) {
		strcpy(sfn, name);
		upcase(sfn);
		return sfn;
	}
	char lfn[LFN_NAMELENGTH+1], fullname[DOS_PATHLENGTH+DOS_NAMELENGTH_ASCII];
	if (name==NULL||!*name) return NULL;
	if (strlen(name)>LFN_NAMELENGTH) {
		strncpy(lfn, name, LFN_NAMELENGTH);
		lfn[LFN_NAMELENGTH]=0;
	} else
		strcpy(lfn, name);
	if (!strlen(lfn)) return NULL;
	direntry fileEntry = {};
	Bit32u dirClust, subEntry;
	unsigned int k=1, i, t=10000;
	while (k<10000) {
		GenerateSFN(lfn, k, i, t);
		strcpy(fullname, path);
		strcat(fullname, sfn);
		if(!getFileDirEntry(fullname, &fileEntry, &dirClust, &subEntry,/*dirOk*/true)) return sfn;
		k++;
	}
	return NULL;
}

class fatFile : public DOS_File {
public:
	fatFile(const char* name, Bit32u startCluster, Bit32u fileLen, fatDrive *useDrive);
	bool Read(Bit8u * data,Bit16u * size);
	bool Write(Bit8u * data,Bit16u * size);
	bool Seek(Bit32u * pos,Bit32u type);
	bool Close();
	Bit16u GetInformation(void);
	bool UpdateDateTimeFromHost(void);   
	bool SetDateTime(Bit16u ndate, Bit16u ntime);
public:
	Bit32u firstCluster;
	Bit32u seekpos;
	Bit32u filelength;
	Bit32u currentSector;
	Bit32u curSectOff;
	Bit8u sectorBuffer[512];
	/* Record of where in the directory structure this file is located */
	Bit32u dirCluster;
	Bit32u dirIndex;

	bool loadedSector;
	fatDrive *myDrive;
private:
	enum { NONE,READ,WRITE } last_action;
	Bit16u info;
	bool modified = false;
	bool newtime = false;
};


void time_t_to_DOS_DateTime(uint16_t &t,uint16_t &d,time_t unix_time) {
        struct tm time;
        time.tm_isdst = -1;

        uint16_t oldax=reg_ax, oldcx=reg_cx, olddx=reg_dx;
        reg_ah=0x2a; // get system date
        CALLBACK_RunRealInt(0x21);

        time.tm_year = reg_cx-1900;
        time.tm_mon = reg_dh-1;
        time.tm_mday = reg_dl;

        reg_ah=0x2c; // get system time
        CALLBACK_RunRealInt(0x21);

        time.tm_hour = reg_ch;
        time.tm_min = reg_cl;
        time.tm_sec = reg_dh;

        reg_ax=oldax;
        reg_cx=oldcx;
        reg_dx=olddx;

        time_t timet = mktime(&time);
        const struct tm *tm = localtime(timet == -1?&unix_time:&timet);
        if (tm == NULL) return;

        /* NTS: tm->tm_year = years since 1900,
         *      tm->tm_mon = months since January therefore January == 0
         *      tm->tm_mday = day of the month, starting with 1 */

        t = ((unsigned int)tm->tm_hour << 11u) + ((unsigned int)tm->tm_min << 5u) + ((unsigned int)tm->tm_sec >> 1u);
        d = (((unsigned int)tm->tm_year - 80u) << 9u) + (((unsigned int)tm->tm_mon + 1u) << 5u) + (unsigned int)tm->tm_mday;
}

/* IN - char * filename: Name in regular filename format, e.g. bob.txt */
/* OUT - char * filearray: Name in DOS directory format, eleven char, e.g. bob     txt */
static void convToDirFile(char *filename, char *filearray) {
	Bit32u charidx = 0;
	Bit32u flen,i;
	flen = (Bit32u)strlen(filename);
	memset(filearray, 32, 11);
	for(i=0;i<flen;i++) {
		if(charidx >= 11) break;
		if(filename[i] != '.') {
			filearray[charidx] = filename[i];
			charidx++;
		} else {
			charidx = 8;
		}
	}
}

fatFile::fatFile(const char* /*name*/, Bit32u startCluster, Bit32u fileLen, fatDrive *useDrive) {
	Bit32u seekto = 0;
	firstCluster = startCluster;
	myDrive = useDrive;
	filelength = fileLen;
	open = true;
	loadedSector = false;
	curSectOff = 0;
	seekpos = 0;
	memset(&sectorBuffer[0], 0, sizeof(sectorBuffer));
	
	if(filelength > 0) {
		Seek(&seekto, DOS_SEEK_SET);
	}
}

bool fatFile::Read(Bit8u * data, Bit16u *size) {
	if ((this->flags & 0xf) == OPEN_WRITE) {	// check if file opened in write-only mode
		DOS_SetError(DOSERR_ACCESS_DENIED);
		return false;
	}
	Bit16u sizedec, sizecount;
	if(seekpos >= filelength) {
		*size = 0;
		return true;
	}

	if (!loadedSector) {
		currentSector = myDrive->getAbsoluteSectFromBytePos(firstCluster, seekpos);
		if(currentSector == 0) {
			/* EOC reached before EOF */
			*size = 0;
			loadedSector = false;
			return true;
		}
		curSectOff = seekpos % myDrive->getSectorSize();
		myDrive->readSector(currentSector, sectorBuffer);
		loadedSector = true;
	}

	sizedec = *size;
	sizecount = 0;
	while(sizedec != 0) {
		if(seekpos >= filelength) {
			*size = sizecount;
			return true; 
		}
		data[sizecount++] = sectorBuffer[curSectOff++];
		seekpos++;
		if(curSectOff >= myDrive->getSectorSize()) {
			currentSector = myDrive->getAbsoluteSectFromBytePos(firstCluster, seekpos);
			if(currentSector == 0) {
				/* EOC reached before EOF */
				//LOG_MSG("EOC reached before EOF, seekpos %d, filelen %d", seekpos, filelength);
				*size = sizecount;
				loadedSector = false;
				return true;
			}
			curSectOff = 0;
			myDrive->readSector(currentSector, sectorBuffer);
			loadedSector = true;
			//LOG_MSG("Reading absolute sector at %d for seekpos %d", currentSector, seekpos);
		}
		--sizedec;
	}
	*size =sizecount;
	return true;
}

bool fatFile::Write(Bit8u * data, Bit16u *size) {
	/* TODO: Check for read-only bit */

	if ((this->flags & 0xf) == OPEN_READ) {	// check if file opened in read-only mode
		DOS_SetError(DOSERR_ACCESS_DENIED);
		return false;
	}

	direntry tmpentry;
	Bit16u sizedec, sizecount;
	sizedec = *size;
	sizecount = 0;

	if(seekpos < filelength && *size == 0) {
		/* Truncate file to current position */
		if(firstCluster != 0) myDrive->deleteClustChain(firstCluster, seekpos);
		if(seekpos == 0) firstCluster = 0;
		filelength = seekpos;
		goto finalizeWrite;
	}

	if(seekpos > filelength) {
		/* Extend file to current position */
		Bit32u clustSize = myDrive->getClusterSize();
		if(filelength == 0) {
			firstCluster = myDrive->getFirstFreeClust();
			if(firstCluster == 0) goto finalizeWrite; // out of space
			myDrive->allocateCluster(firstCluster, 0);
			filelength = clustSize;
		}
		filelength = ((filelength - 1) / clustSize + 1) * clustSize;
		while(filelength < seekpos) {
			if(myDrive->appendCluster(firstCluster) == 0) goto finalizeWrite; // out of space
			filelength += clustSize;
		}
		if(filelength > seekpos) filelength = seekpos;
		if(*size == 0) goto finalizeWrite;
	}

	while(sizedec != 0) {
		/* Increase filesize if necessary */
		if(seekpos >= filelength) {
			if(filelength == 0) {
				firstCluster = myDrive->getFirstFreeClust();
				if(firstCluster == 0) goto finalizeWrite; // out of space
				myDrive->allocateCluster(firstCluster, 0);
				currentSector = myDrive->getAbsoluteSectFromBytePos(firstCluster, seekpos);
				myDrive->readSector(currentSector, sectorBuffer);
				loadedSector = true;
			}
			if (!loadedSector) {
				currentSector = myDrive->getAbsoluteSectFromBytePos(firstCluster, seekpos);
				if(currentSector == 0) {
					/* EOC reached before EOF - try to increase file allocation */
					myDrive->appendCluster(firstCluster);
					/* Try getting sector again */
					currentSector = myDrive->getAbsoluteSectFromBytePos(firstCluster, seekpos);
					if(currentSector == 0) {
						/* No can do. lets give up and go home.  We must be out of room */
						goto finalizeWrite;
					}
				}
				curSectOff = seekpos % myDrive->getSectorSize();
				myDrive->readSector(currentSector, sectorBuffer);
				loadedSector = true;
			}
			filelength = seekpos+1;
		}
		sectorBuffer[curSectOff++] = data[sizecount++];
		seekpos++;
		if(curSectOff >= myDrive->getSectorSize()) {
			if(loadedSector) myDrive->writeSector(currentSector, sectorBuffer);

			currentSector = myDrive->getAbsoluteSectFromBytePos(firstCluster, seekpos);
			if(currentSector == 0) loadedSector = false;
			else {
				curSectOff = 0;
				myDrive->readSector(currentSector, sectorBuffer);
				loadedSector = true;
			}
		}
		--sizedec;
		modified = true;
	}
	if(curSectOff>0 && loadedSector) myDrive->writeSector(currentSector, sectorBuffer);

finalizeWrite:
	myDrive->directoryBrowse(dirCluster, &tmpentry, dirIndex);
	tmpentry.entrysize = filelength;
	tmpentry.loFirstClust = (Bit16u)firstCluster;
	myDrive->directoryChange(dirCluster, &tmpentry, dirIndex);

	*size =sizecount;
	return true;
}

bool fatFile::Seek(Bit32u *pos, Bit32u type) {
	Bit32s seekto=0;
	
	switch(type) {
		case DOS_SEEK_SET:
			seekto = (Bit32s)*pos;
			break;
		case DOS_SEEK_CUR:
			/* Is this relative seek signed? */
			seekto = (Bit32s)*pos + (Bit32s)seekpos;
			break;
		case DOS_SEEK_END:
			seekto = (Bit32s)filelength + (Bit32s)*pos;
			break;
	}
//	LOG_MSG("Seek to %d with type %d (absolute value %d)", *pos, type, seekto);

	if(seekto<0) seekto = 0;
	seekpos = (Bit32u)seekto;
	currentSector = myDrive->getAbsoluteSectFromBytePos(firstCluster, seekpos);
	if (currentSector == 0) {
		/* not within file size, thus no sector is available */
		loadedSector = false;
	} else {
		curSectOff = seekpos % myDrive->getSectorSize();
		myDrive->readSector(currentSector, sectorBuffer);
		loadedSector = true;
	}
	*pos = seekpos;
	return true;
}

bool fatFile::Close() {
	/* Flush buffer */
	if (loadedSector) myDrive->writeSector(currentSector, sectorBuffer);

	if (modified || newtime) {
		direntry tmpentry = {};

		myDrive->directoryBrowse(dirCluster, &tmpentry, (int32_t)dirIndex);
		if (newtime) {
			tmpentry.modTime = time;
			tmpentry.modDate = date;
		} else {
			time_t tt = ::time(NULL);
			struct tm *tm = localtime(&tt);
			tmpentry.modTime = ((unsigned int)tm->tm_hour << 11u) + ((unsigned int)tm->tm_min << 5u) + ((unsigned int)tm->tm_sec >> 1u);
			tmpentry.modDate = (((unsigned int)tm->tm_year - 80u) << 9u) + (((unsigned int)tm->tm_mon + 1u) << 5u) + (unsigned int)tm->tm_mday;
		}

		myDrive->directoryChange(dirCluster, &tmpentry, (int32_t)dirIndex);
	}
	return false;
}

Bit16u fatFile::GetInformation(void) {
	return 0;
}

bool fatFile::UpdateDateTimeFromHost(void) {
	direntry tmpentry = {};
	myDrive->directoryBrowse(dirCluster, &tmpentry, (int32_t)dirIndex);
	time = tmpentry.modTime;
	date = tmpentry.modDate;
	return true;
}

bool fatFile::SetDateTime(Bit16u ndate, Bit16u ntime)
{
	time = ntime;
	date = ndate;
	newtime = true;
	return true;
}

Bit32u fatDrive::getClustFirstSect(Bit32u clustNum) {
	return ((clustNum - 2) * bootbuffer.sectorspercluster) + firstDataSector;
}

Bit32u fatDrive::getClusterValue(Bit32u clustNum) {
	Bit32u fatoffset=0;
	Bit32u fatsectnum;
	Bit32u fatentoff;
	Bit32u clustValue=0;

	switch(fattype) {
		case FAT12:
			fatoffset = clustNum + (clustNum / 2);
			break;
		case FAT16:
			fatoffset = clustNum * 2;
			break;
		case FAT32:
			fatoffset = clustNum * 4;
			break;
	}
	fatsectnum = bootbuffer.reservedsectors + (fatoffset / bootbuffer.bytespersector) + partSectOff;
	fatentoff = fatoffset % bootbuffer.bytespersector;

	if(curFatSect != fatsectnum) {
		/* Load two sectors at once for FAT12 */
		readSector(fatsectnum, &fatSectBuffer[0]);
		if (fattype==FAT12)
			readSector(fatsectnum+1, &fatSectBuffer[512]);
		curFatSect = fatsectnum;
	}

	switch(fattype) {
		case FAT12:
			clustValue = var_read((Bit16u *)&fatSectBuffer[fatentoff]);
			if(clustNum & 0x1) {
				clustValue >>= 4;
			} else {
				clustValue &= 0xfff;
			}
			break;
		case FAT16:
			clustValue = var_read((Bit16u *)&fatSectBuffer[fatentoff]);
			break;
		case FAT32:
			clustValue = var_read((Bit32u *)&fatSectBuffer[fatentoff]);
			break;
	}

	return clustValue;
}

void fatDrive::setClusterValue(Bit32u clustNum, Bit32u clustValue) {
	Bit32u fatoffset=0;
	Bit32u fatsectnum;
	Bit32u fatentoff;

	switch(fattype) {
		case FAT12:
			fatoffset = clustNum + (clustNum / 2);
			break;
		case FAT16:
			fatoffset = clustNum * 2;
			break;
		case FAT32:
			fatoffset = clustNum * 4;
			break;
	}
	fatsectnum = bootbuffer.reservedsectors + (fatoffset / bootbuffer.bytespersector) + partSectOff;
	fatentoff = fatoffset % bootbuffer.bytespersector;

	if(curFatSect != fatsectnum) {
		/* Load two sectors at once for FAT12 */
		readSector(fatsectnum, &fatSectBuffer[0]);
		if (fattype==FAT12)
			readSector(fatsectnum+1, &fatSectBuffer[512]);
		curFatSect = fatsectnum;
	}

	switch(fattype) {
		case FAT12: {
			Bit16u tmpValue = var_read((Bit16u *)&fatSectBuffer[fatentoff]);
			if(clustNum & 0x1) {
				clustValue &= 0xfff;
				clustValue <<= 4;
				tmpValue &= 0xf;
				tmpValue |= (Bit16u)clustValue;

			} else {
				clustValue &= 0xfff;
				tmpValue &= 0xf000;
				tmpValue |= (Bit16u)clustValue;
			}
			var_write((Bit16u *)&fatSectBuffer[fatentoff], tmpValue);
			break;
			}
		case FAT16:
			var_write((Bit16u *)&fatSectBuffer[fatentoff], (Bit16u)clustValue);
			break;
		case FAT32:
			var_write((Bit32u *)&fatSectBuffer[fatentoff], clustValue);
			break;
	}
	for(int fc=0;fc<bootbuffer.fatcopies;fc++) {
		writeSector(fatsectnum + (fc * bootbuffer.sectorsperfat), &fatSectBuffer[0]);
		if (fattype==FAT12) {
			if (fatentoff>=511)
				writeSector(fatsectnum+1+(fc * bootbuffer.sectorsperfat), &fatSectBuffer[512]);
		}
	}
}

bool fatDrive::getEntryName(char *fullname, char *entname) {
	char dirtoken[DOS_PATHLENGTH];

	char * findDir;
	char * findFile;
	strcpy(dirtoken,fullname);

	//LOG_MSG("Testing for filename %s", fullname);
	findDir = strtok(dirtoken,"\\");
	if (findDir==NULL) {
		return true;	// root always exists
	}
	findFile = findDir;
	while(findDir != NULL) {
		findFile = findDir;
		findDir = strtok(NULL,"\\");
	}
	int j=0;
	for (int i=0; i<(int)strlen(findFile); i++)
		if (findFile[i]!=' '&&findFile[i]!='"'&&findFile[i]!='+'&&findFile[i]!='='&&findFile[i]!=','&&findFile[i]!=';'&&findFile[i]!=':'&&findFile[i]!='<'&&findFile[i]!='>'&&findFile[i]!='['&&findFile[i]!=']'&&findFile[i]!='|'&&findFile[i]!='?'&&findFile[i]!='*') findFile[j++]=findFile[i];
	findFile[j]=0;
	if (strlen(findFile)>12)
		strncpy(entname, findFile, 12);
	else
		strcpy(entname, findFile);
	upcase(entname);
	return true;
}

bool fatDrive::getFileDirEntry(char const * const filename, direntry * useEntry, Bit32u * dirClust, Bit32u * subEntry,bool dirOk) {
	size_t len = strlen(filename);
	char dirtoken[DOS_PATHLENGTH];
	Bit32u currentClust = 0; /* FAT12/FAT16 root directory */

	direntry foundEntry;
	char * findDir;
	char * findFile;
	strcpy(dirtoken,filename);
	findFile=dirtoken;

	int fbak=lfn_filefind_handle;
	lfn_filefind_handle=uselfn?LFN_FILEFIND_IMG:LFN_FILEFIND_NONE;
	/* Skip if testing in root directory */
	if ((len>0) && (filename[len-1]!='\\')) {
		//LOG_MSG("Testing for filename %s", filename);
		findDir = strtok(dirtoken,"\\");
		findFile = findDir;
		while(findDir != NULL) {
			imgDTA->SetupSearch(0,DOS_ATTR_DIRECTORY,findDir);
			imgDTA->SetDirID(0);
			
			findFile = findDir;
			if(!FindNextInternal(currentClust, *imgDTA, &foundEntry)) break;
			else {
				//Found something. See if it's a directory (findfirst always finds regular files)
                char find_name[DOS_NAMELENGTH_ASCII],lfind_name[LFN_NAMELENGTH];
                Bit16u find_date,find_time;Bit32u find_size;Bit8u find_attr;
                imgDTA->GetResult(find_name,lfind_name,find_size,find_date,find_time,find_attr);
				if(!(find_attr & DOS_ATTR_DIRECTORY)) break;

				char * findNext;
				findNext = strtok(NULL,"\\");
				if (findNext == NULL && dirOk) break; /* dirOk means that if the last element is a directory, then refer to the directory itself */
				findDir = findNext;
			}

			currentClust = foundEntry.loFirstClust;
		}
	} else {
		/* Set to root directory */
	}

	/* Search found directory for our file */
	imgDTA->SetupSearch(0,0x7 | (dirOk ? DOS_ATTR_DIRECTORY : 0),findFile);
	imgDTA->SetDirID(0);
	if(!FindNextInternal(currentClust, *imgDTA, &foundEntry)) {lfn_filefind_handle=fbak;return false;}
	lfn_filefind_handle=fbak;

	memcpy(useEntry, &foundEntry, sizeof(direntry));
	*dirClust = (Bit32u)currentClust;
	*subEntry = ((Bit32u)imgDTA->GetDirID()-1);
	return true;
}

bool fatDrive::getDirClustNum(char *dir, Bit32u *clustNum, bool parDir) {
	Bit32u len = (Bit32u)strlen(dir);
	char dirtoken[DOS_PATHLENGTH];
	Bit32u currentClust = 0;
	direntry foundEntry;
	char * findDir;
	strcpy(dirtoken,dir);

	int fbak=lfn_filefind_handle;
	/* Skip if testing for root directory */
	if ((len>0) && (dir[len-1]!='\\')) {
		//LOG_MSG("Testing for dir %s", dir);
		findDir = strtok(dirtoken,"\\");
		while(findDir != NULL) {
			lfn_filefind_handle=uselfn?LFN_FILEFIND_IMG:LFN_FILEFIND_NONE;
			imgDTA->SetupSearch(0,DOS_ATTR_DIRECTORY,findDir);
			imgDTA->SetDirID(0);
			findDir = strtok(NULL,"\\");
			if(parDir && (findDir == NULL)) {lfn_filefind_handle=fbak;break;}

			char find_name[DOS_NAMELENGTH_ASCII],lfind_name[LFN_NAMELENGTH];
			Bit16u find_date,find_time;Bit32u find_size;Bit8u find_attr;
			if(!FindNextInternal(currentClust, *imgDTA, &foundEntry)) {
				lfn_filefind_handle=fbak;
				return false;
			} else {
				imgDTA->GetResult(find_name,lfind_name,find_size,find_date,find_time,find_attr);
				lfn_filefind_handle=fbak;
				if(!(find_attr &DOS_ATTR_DIRECTORY)) return false;
			}
			currentClust = foundEntry.loFirstClust;

		}
		*clustNum = currentClust;
	} else {
		/* Set to root directory */
		*clustNum = 0;
	}
	return true;
}

Bit8u fatDrive::readSector(Bit32u sectnum, void * data) {
	if (absolute) return loadedDisk->Read_AbsoluteSector(sectnum, data);
	Bit32u cylindersize = bootbuffer.headcount * bootbuffer.sectorspertrack;
	Bit32u cylinder = sectnum / cylindersize;
	sectnum %= cylindersize;
	Bit32u head = sectnum / bootbuffer.sectorspertrack;
	Bit32u sector = sectnum % bootbuffer.sectorspertrack + 1L;
	return loadedDisk->Read_Sector(head, cylinder, sector, data);
}	

Bit8u fatDrive::writeSector(Bit32u sectnum, void * data) {
	if (absolute) return loadedDisk->Write_AbsoluteSector(sectnum, data);
	Bit32u cylindersize = bootbuffer.headcount * bootbuffer.sectorspertrack;
	Bit32u cylinder = sectnum / cylindersize;
	sectnum %= cylindersize;
	Bit32u head = sectnum / bootbuffer.sectorspertrack;
	Bit32u sector = sectnum % bootbuffer.sectorspertrack + 1L;
	return loadedDisk->Write_Sector(head, cylinder, sector, data);
}

Bit32u fatDrive::getSectorCount(void) {
	if (bootbuffer.totalsectorcount != 0)
		return (Bit32u)bootbuffer.totalsectorcount;
	else
		return bootbuffer.totalsecdword;
}

Bit32u fatDrive::getSectorSize(void) {
	return bootbuffer.bytespersector;
}

Bit32u fatDrive::getClusterSize(void) {
	return bootbuffer.sectorspercluster * bootbuffer.bytespersector;
}

Bit32u fatDrive::getAbsoluteSectFromBytePos(Bit32u startClustNum, Bit32u bytePos) {
	return  getAbsoluteSectFromChain(startClustNum, bytePos / bootbuffer.bytespersector);
}

Bit32u fatDrive::getAbsoluteSectFromChain(Bit32u startClustNum, Bit32u logicalSector) {
	Bit32s skipClust = logicalSector / bootbuffer.sectorspercluster;
	Bit32u sectClust = logicalSector % bootbuffer.sectorspercluster;

	Bit32u currentClust = startClustNum;
	Bit32u testvalue;

	while(skipClust!=0) {
		bool isEOF = false;
		testvalue = getClusterValue(currentClust);
		switch(fattype) {
			case FAT12:
				if(testvalue >= 0xff8) isEOF = true;
				break;
			case FAT16:
				if(testvalue >= 0xfff8) isEOF = true;
				break;
			case FAT32:
				if(testvalue >= 0xfffffff8) isEOF = true;
				break;
		}
		if((isEOF) && (skipClust>=1)) {
			//LOG_MSG("End of cluster chain reached before end of logical sector seek!");
			if (skipClust == 1 && fattype == FAT12) {
				//break;
				LOG(LOG_DOSMISC,LOG_ERROR)("End of cluster chain reached, but maybe good afterall ?");
			}
			return 0;
		}
		currentClust = testvalue;
		--skipClust;
	}

	return (getClustFirstSect(currentClust) + sectClust);
}

void fatDrive::deleteClustChain(Bit32u startCluster, Bit32u bytePos) {
	Bit32u clustSize = getClusterSize();
	Bit32u endClust = (bytePos + clustSize - 1) / clustSize;
	Bit32u countClust = 1;

	Bit32u testvalue;
	Bit32u currentClust = startCluster;
	bool isEOF = false;
	while(!isEOF) {
		testvalue = getClusterValue(currentClust);
		if(testvalue == 0) {
			/* What the crap?  Cluster is already empty - BAIL! */
			break;
		}
		switch(fattype) {
			case FAT12:
				if(testvalue >= 0xff8) isEOF = true;
				break;
			case FAT16:
				if(testvalue >= 0xfff8) isEOF = true;
				break;
			case FAT32:
				if(testvalue >= 0xfffffff8) isEOF = true;
				break;
		}
		if(countClust == endClust && !isEOF) {
			/* Mark cluster as end */
			switch(fattype) {
				case FAT12:
					setClusterValue(currentClust, 0xfff);
					break;
				case FAT16:
					setClusterValue(currentClust, 0xffff);
					break;
				case FAT32:
					setClusterValue(currentClust, 0xffffffff);
					break;
			}
		} else if(countClust > endClust) {
			/* Mark cluster as empty */
			setClusterValue(currentClust, 0);
		}
		if(isEOF) break;
		currentClust = testvalue;
		countClust++;
	}
}

Bit32u fatDrive::appendCluster(Bit32u startCluster) {
	Bit32u testvalue;
	Bit32u currentClust = startCluster;
	bool isEOF = false;
	
	while(!isEOF) {
		testvalue = getClusterValue(currentClust);
		switch(fattype) {
			case FAT12:
				if(testvalue >= 0xff8) isEOF = true;
				break;
			case FAT16:
				if(testvalue >= 0xfff8) isEOF = true;
				break;
			case FAT32:
				if(testvalue >= 0xfffffff8) isEOF = true;
				break;
		}
		if(isEOF) break;
		currentClust = testvalue;
	}

	Bit32u newClust = getFirstFreeClust();
	/* Drive is full */
	if(newClust == 0) return 0;

	if(!allocateCluster(newClust, currentClust)) return 0;

	zeroOutCluster(newClust);

	return newClust;
}

bool fatDrive::allocateCluster(Bit32u useCluster, Bit32u prevCluster) {

	/* Can't allocate cluster #0 */
	if(useCluster == 0) return false;

	if(prevCluster != 0) {
		/* Refuse to allocate cluster if previous cluster value is zero (unallocated) */
		if(!getClusterValue(prevCluster)) return false;

		/* Point cluster to new cluster in chain */
		setClusterValue(prevCluster, useCluster);
		//LOG_MSG("Chaining cluser %d to %d", prevCluster, useCluster);
	} 

	switch(fattype) {
		case FAT12:
			setClusterValue(useCluster, 0xfff);
			break;
		case FAT16:
			setClusterValue(useCluster, 0xffff);
			break;
		case FAT32:
			setClusterValue(useCluster, 0xffffffff);
			break;
	}
	return true;
}

fatDrive::fatDrive(const char *sysFilename, Bit32u bytesector, Bit32u cylsector, Bit32u headscyl, Bit32u cylinders, Bit32u startSector) {
	created_successfully = true;
	FILE *diskfile;
	Bit32u filesize;
	bool is_hdd;
	struct partTable mbrData;
	
	if(imgDTASeg == 0) {
		imgDTASeg = DOS_GetMemory(4);
		imgDTAPtr = RealMake(imgDTASeg, 0);
		imgDTA    = new DOS_DTA(imgDTAPtr);
	}

	diskfile = fopen_wrap(sysFilename, "rb+");
	if(!diskfile) {created_successfully = false;return;}
	fseek(diskfile, 0L, SEEK_END);
	filesize = (Bit32u)ftell(diskfile) / 1024L;
	is_hdd = (filesize > 2880);

	/* Load disk image */
	loadedDisk = new imageDisk(diskfile, sysFilename, filesize, is_hdd);
	if(!loadedDisk) {
		created_successfully = false;
		return;
	}

	if(is_hdd) {
		/* Set user specified harddrive parameters */
		loadedDisk->Set_Geometry(headscyl, cylinders,cylsector, bytesector);

		loadedDisk->Read_Sector(0,0,1,&mbrData);

		if(mbrData.magic1!= 0x55 ||	mbrData.magic2!= 0xaa) LOG_MSG("Possibly invalid partition table in disk image.");

		startSector = 63;
		int m;
		for(m=0;m<4;m++) {
			/* Pick the first available partition */
			if(mbrData.pentry[m].partSize != 0x00) {
				mbrData.pentry[m].absSectStart = var_read(&mbrData.pentry[m].absSectStart);
				mbrData.pentry[m].partSize = var_read(&mbrData.pentry[m].partSize);
				LOG_MSG("Using partition %d on drive; skipping %d sectors", m, mbrData.pentry[m].absSectStart);
				startSector = mbrData.pentry[m].absSectStart;
				break;
			}
		}

		if(m==4) LOG_MSG("No good partition found in image.");

		partSectOff = startSector;
	} else {
		/* Get floppy disk parameters based on image size */
		loadedDisk->Get_Geometry(&headscyl, &cylinders, &cylsector, &bytesector);
		/* Floppy disks don't have partitions */
		partSectOff = 0;
	}

	if (bytesector != 512) {
		/* Non-standard sector sizes not implemented */
		created_successfully = false;
		return;
	}

	loadedDisk->Read_AbsoluteSector(0+partSectOff,&bootbuffer);

	bootbuffer.bytespersector = var_read(&bootbuffer.bytespersector);
	bootbuffer.reservedsectors = var_read(&bootbuffer.reservedsectors);
	bootbuffer.rootdirentries = var_read(&bootbuffer.rootdirentries);
	bootbuffer.totalsectorcount = var_read(&bootbuffer.totalsectorcount);
	bootbuffer.sectorsperfat = var_read(&bootbuffer.sectorsperfat);
	bootbuffer.sectorspertrack = var_read(&bootbuffer.sectorspertrack);
	bootbuffer.headcount = var_read(&bootbuffer.headcount);
	bootbuffer.hiddensectorcount = var_read(&bootbuffer.hiddensectorcount);
	bootbuffer.totalsecdword = var_read(&bootbuffer.totalsecdword);

	if (!is_hdd) {
		/* Identify floppy format */
		if ((bootbuffer.nearjmp[0] == 0x69 || bootbuffer.nearjmp[0] == 0xe9 ||
			(bootbuffer.nearjmp[0] == 0xeb && bootbuffer.nearjmp[2] == 0x90)) &&
			(bootbuffer.mediadescriptor & 0xf0) == 0xf0) {
			/* DOS 2.x or later format, BPB assumed valid */

			if ((bootbuffer.mediadescriptor != 0xf0 && !(bootbuffer.mediadescriptor & 0x1)) &&
				(bootbuffer.oemname[5] != '3' || bootbuffer.oemname[6] != '.' || bootbuffer.oemname[7] < '2')) {
				/* Fix pre-DOS 3.2 single-sided floppy */
				bootbuffer.sectorspercluster = 1;
			}
		} else {
			/* Read media descriptor in FAT */
			Bit8u sectorBuffer[512];
			loadedDisk->Read_AbsoluteSector(1,&sectorBuffer);
			Bit8u mdesc = sectorBuffer[0];

			if (mdesc >= 0xf8) {
				/* DOS 1.x format, create BPB for 160kB floppy */
				bootbuffer.bytespersector = 512;
				bootbuffer.sectorspercluster = 1;
				bootbuffer.reservedsectors = 1;
				bootbuffer.fatcopies = 2;
				bootbuffer.rootdirentries = 64;
				bootbuffer.totalsectorcount = 320;
				bootbuffer.mediadescriptor = mdesc;
				bootbuffer.sectorsperfat = 1;
				bootbuffer.sectorspertrack = 8;
				bootbuffer.headcount = 1;
				bootbuffer.magic1 = 0x55;	// to silence warning
				bootbuffer.magic2 = 0xaa;
				if (!(mdesc & 0x2)) {
					/* Adjust for 9 sectors per track */
					bootbuffer.totalsectorcount = 360;
					bootbuffer.sectorsperfat = 2;
					bootbuffer.sectorspertrack = 9;
				}
				if (mdesc & 0x1) {
					/* Adjust for 2 sides */
					bootbuffer.sectorspercluster = 2;
					bootbuffer.rootdirentries = 112;
					bootbuffer.totalsectorcount *= 2;
					bootbuffer.headcount = 2;
				}
			} else {
				/* Unknown format */
				created_successfully = false;
				return;
			}
		}
	}

	if ((bootbuffer.magic1 != 0x55) || (bootbuffer.magic2 != 0xaa)) {
		/* Not a FAT filesystem */
		LOG_MSG("Loaded image has no valid magicnumbers at the end!");
	}

	/* Sanity checks */
	if ((bootbuffer.sectorsperfat == 0) || // FAT32 not implemented yet
		(bootbuffer.bytespersector != 512) || // non-standard sector sizes not implemented
		(bootbuffer.sectorspercluster == 0) ||
		(bootbuffer.rootdirentries == 0) ||
		(bootbuffer.fatcopies == 0) ||
		(bootbuffer.headcount == 0) ||
		(bootbuffer.headcount > headscyl) ||
		(bootbuffer.sectorspertrack == 0) ||
		(bootbuffer.sectorspertrack > cylsector)) {
		created_successfully = false;
		return;
	}

	/* Filesystem must be contiguous to use absolute sectors, otherwise CHS will be used */
	absolute = ((bootbuffer.headcount == headscyl) && (bootbuffer.sectorspertrack == cylsector));

	/* Determine FAT format, 12, 16 or 32 */

	/* Get size of root dir in sectors */
	Bit32u RootDirSectors = ((bootbuffer.rootdirentries * 32) + (bootbuffer.bytespersector - 1)) / bootbuffer.bytespersector;
	Bit32u DataSectors;
	if(bootbuffer.totalsectorcount != 0) {
		DataSectors = bootbuffer.totalsectorcount - (bootbuffer.reservedsectors + (bootbuffer.fatcopies * bootbuffer.sectorsperfat) + RootDirSectors);
	} else {
		DataSectors = bootbuffer.totalsecdword - (bootbuffer.reservedsectors + (bootbuffer.fatcopies * bootbuffer.sectorsperfat) + RootDirSectors);

	}
	CountOfClusters = DataSectors / bootbuffer.sectorspercluster;

	firstDataSector = (bootbuffer.reservedsectors + (bootbuffer.fatcopies * bootbuffer.sectorsperfat) + RootDirSectors) + partSectOff;
	firstRootDirSect = bootbuffer.reservedsectors + (bootbuffer.fatcopies * bootbuffer.sectorsperfat) + partSectOff;

	if(CountOfClusters < 4085) {
		/* Volume is FAT12 */
		LOG_MSG("Mounted FAT volume is FAT12 with %d clusters", CountOfClusters);
		fattype = FAT12;
	} else if (CountOfClusters < 65525) {
		LOG_MSG("Mounted FAT volume is FAT16 with %d clusters", CountOfClusters);
		fattype = FAT16;
	} else {
		LOG_MSG("Mounted FAT volume is FAT32 with %d clusters", CountOfClusters);
		fattype = FAT32;
	}

	/* There is no cluster 0, this means we are in the root directory */
	cwdDirCluster = 0;

	memset(fatSectBuffer,0,1024);
	curFatSect = 0xffffffff;

	strcpy(info, "fatDrive ");
	strcat(info, sysFilename);
}

static Bit8u get_shift_count(Bit8u data)
{
	if(data != 0) {
		int count = 7;
		while(!(data & (1 << count))) {
			count--;
		}
		return count;
	}
	return 0xff;
}

void fatDrive::UpdateDPB(unsigned char dos_drive) {
    if (dos_drive >= DOS_DRIVES) return;
    PhysPt ptr = PhysMake(dos.tables.dpb,dos_drive*dos.tables.dpb_size);
    if (ptr != PhysPt(0)) {
        mem_writew(ptr+0x02,bootbuffer.bytespersector);                  // +2 = bytes per sector
        mem_writeb(ptr+0x04,bootbuffer.sectorspercluster-1);             // +4 = highest sector within a cluster
        mem_writeb(ptr+0x05,get_shift_count(bootbuffer.sectorspercluster));  // +5 = shift count to convert clusters to sectors
        mem_writew(ptr+0x06,bootbuffer.reservedsectors);                 // +6 = number of reserved sectors at start of partition
        mem_writeb(ptr+0x08,bootbuffer.fatcopies);                       // +8 = number of FATs (file allocation tables)
        mem_writew(ptr+0x09,bootbuffer.rootdirentries);                  // +9 = number of root directory entries
        mem_writew(ptr+0x0B,(uint16_t)(firstDataSector-partSectOff));    // +11 = number of first sector containing user data

        mem_writew(ptr+0x0D,(uint16_t)CountOfClusters + 1);              // +13 = highest cluster number

        mem_writew(ptr+0x0F,(uint16_t)bootbuffer.sectorsperfat);         // +15 = sectors per FAT

        mem_writew(ptr+0x11,(uint16_t)(firstRootDirSect-partSectOff));   // +17 = sector number of first directory sector

        mem_writed(ptr+0x13,0xFFFFFFFF);                            // +19 = address of device driver header (NOT IMPLEMENTED) Windows 98 behavior
        mem_writeb(ptr+0x17,bootbuffer.mediadescriptor);            // +23 = media ID byte
        mem_writeb(ptr+0x18,0x00);                                  // +24 = disk accessed
        mem_writew(ptr+0x1F,0xFFFF);                                // +31 = number of free clusters or 0xFFFF if unknown
        // other fields, not implemented
    }
}

bool fatDrive::AllocationInfo(Bit16u *_bytes_sector, Bit8u *_sectors_cluster, Bit16u *_total_clusters, Bit16u *_free_clusters) {
	Bit32u hs, cy, sect,sectsize;
	Bit32u countFree = 0;
	Bit32u i;
	
	loadedDisk->Get_Geometry(&hs, &cy, &sect, &sectsize);
	*_bytes_sector = (Bit16u)sectsize;
	*_sectors_cluster = bootbuffer.sectorspercluster;
	if (CountOfClusters<65536) *_total_clusters = (Bit16u)CountOfClusters;
	else {
		// maybe some special handling needed for fat32
		*_total_clusters = 65535;
	}
	for(i=0;i<CountOfClusters;i++) if(!getClusterValue(i+2)) countFree++;
	if (countFree<65536) *_free_clusters = (Bit16u)countFree;
	else {
		// maybe some special handling needed for fat32
		*_free_clusters = 65535;
	}
	
	return true;
}

Bit32u fatDrive::getFirstFreeClust(void) {
	Bit32u i;
	for(i=0;i<CountOfClusters;i++) {
		if(!getClusterValue(i+2)) return (i+2);
	}

	/* No free cluster found */
	return 0;
}

bool fatDrive::isRemote(void) {	return false; }
bool fatDrive::isRemovable(void) { return false; }

Bits fatDrive::UnMount(void) {
	delete this;
	return 0;
}

Bit8u fatDrive::GetMediaByte(void) { return loadedDisk->GetBiosType(); }

bool fatDrive::FileCreate(DOS_File **file, char *name, Bit16u attributes) {
	const char *lfn = NULL;
	direntry fileEntry;
	Bit32u dirClust, subEntry;
	char dirName[DOS_NAMELENGTH_ASCII];
	char pathName[11], path[LFN_NAMELENGTH+2];

	Bit16u save_errorcode=dos.errorcode;

	/* Check if file already exists */
	if(getFileDirEntry(name, &fileEntry, &dirClust, &subEntry)) {
		/* Truncate file */
		if(fileEntry.loFirstClust != 0) {
			deleteClustChain(fileEntry.loFirstClust, 0);
			fileEntry.loFirstClust = 0;
		}
		fileEntry.entrysize = 0;
		directoryChange(dirClust, &fileEntry, subEntry);
	} else {
		/* Can we even get the name of the file itself? */
		if(!getEntryName(name, &dirName[0])||!strlen(trim(dirName))) return false;
		convToDirFile(&dirName[0], &pathName[0]);

		/* Can we find the base directory? */
		if(!getDirClustNum(name, &dirClust, true)) return false;
		/* NTS: "name" is the full relative path. For LFN creation to work we need only the final element of the path */
		if (uselfn && !force) {
			lfn = strrchr(name,'\\');

			if (lfn != NULL) {
				lfn++; /* step past '\' */
				strcpy(path, name);
				*(strrchr(path,'\\')+1)=0;
			} else {
				lfn = name; /* no path elements */
				*path=0;
			}

			if (filename_not_strict_8x3(lfn)) {
				char *sfn=Generate_SFN(path, lfn);
				if (sfn!=NULL) convToDirFile(sfn, &pathName[0]);
			} else
				lfn = NULL;
		}
		memset(&fileEntry, 0, sizeof(direntry));
		memcpy(&fileEntry.entryname, &pathName[0], 11);
		fileEntry.attrib = (Bit8u)(attributes & 0xff);
		addDirectoryEntry(dirClust, fileEntry, lfn);

		/* Check if file exists now */
		if(!getFileDirEntry(name, &fileEntry, &dirClust, &subEntry)) return false;
	}

	/* Empty file created, now lets open it */
	/* TODO: check for read-only flag and requested write access */
	*file = new fatFile(name, fileEntry.loFirstClust, fileEntry.entrysize, this);
	(*file)->flags=OPEN_READWRITE;
	((fatFile *)(*file))->dirCluster = dirClust;
	((fatFile *)(*file))->dirIndex = subEntry;
	/* Maybe modTime and date should be used ? (crt matches findnext) */
	((fatFile *)(*file))->time = fileEntry.crtTime;
	((fatFile *)(*file))->date = fileEntry.crtDate;

	dos.errorcode=save_errorcode;
	return true;
}

bool fatDrive::FileExists(const char *name) {
	direntry fileEntry;
	Bit32u dummy1, dummy2;
	Bit16u save_errorcode = dos.errorcode;
	bool found = getFileDirEntry(name, &fileEntry, &dummy1, &dummy2);
	dos.errorcode = save_errorcode;
	return found;
}

bool fatDrive::FileOpen(DOS_File **file, char *name, Bit32u flags) {
	direntry fileEntry;
	Bit32u dirClust, subEntry;
	if(!getFileDirEntry(name, &fileEntry, &dirClust, &subEntry)) return false;
	/* TODO: check for read-only flag and requested write access */
	*file = new fatFile(name, fileEntry.loFirstClust, fileEntry.entrysize, this);
	(*file)->flags = flags;
	((fatFile *)(*file))->dirCluster = dirClust;
	((fatFile *)(*file))->dirIndex = subEntry;
	/* Maybe modTime and date should be used ? (crt matches findnext) */
	((fatFile *)(*file))->time = fileEntry.crtTime;
	((fatFile *)(*file))->date = fileEntry.crtDate;
	return true;
}

bool fatDrive::FileStat(const char * /*name*/, FileStat_Block *const /*stat_block*/) {
	/* TODO: Stub */
	return false;
}

bool fatDrive::FileUnlink(char * name) {
	direntry tmpentry = {};
	direntry fileEntry = {};
	Bit32u dirClust, subEntry;

/* you cannot delete root directory */
	if (*name == 0) {
		DOS_SetError(DOSERR_ACCESS_DENIED);
		return false;
	}

	lfnRange.clear();
	if(!getFileDirEntry(name, &fileEntry, &dirClust, &subEntry)) {return false;}
	lfnRange_t dir_lfn_range = lfnRange; /* copy down LFN results before they are obliterated by the next call to FindNextInternal. */

	/* delete LFNs */
	if (!dir_lfn_range.empty() && (dos.version.major >= 7 || uselfn)) {
		/* last LFN entry should be fileidx */
		if (dir_lfn_range.dirPos_start >= dir_lfn_range.dirPos_end) return false;
		if (dir_lfn_range.dirPos_end != subEntry) LOG_MSG("FAT warning: LFN dirPos_end=%u fileidx=%u (mismatch)",dir_lfn_range.dirPos_end,subEntry);
		for (unsigned int didx=dir_lfn_range.dirPos_start;didx < dir_lfn_range.dirPos_end;didx++) {
			if (directoryBrowse(dirClust,&tmpentry,didx)) {
				tmpentry.entryname[0] = 0xe5;
				directoryChange(dirClust,&tmpentry,didx);
			}
		}
	}
    /* remove primary 8.3 SFN */
	fileEntry.entryname[0] = 0xe5;
	directoryChange(dirClust, &fileEntry, subEntry);

	if(fileEntry.loFirstClust != 0) deleteClustChain(fileEntry.loFirstClust, 0);

	return true;
}

bool fatDrive::FindFirst(char *_dir, DOS_DTA &dta,bool /*fcb_findfirst*/) {
	direntry dummyClust;
#if 0
	Bit8u attr;char pattern[CROSS_LEN];
	dta.GetSearchParams(attr,pattern,false);
	if(attr==DOS_ATTR_VOLUME) {
		if (strcmp(GetLabel(), "") == 0 ) {
			DOS_SetError(DOSERR_NO_MORE_FILES);
			return false;
		}
		dta.SetResult(GetLabel(),GetLabel(),0,0,DOS_ATTR_VOLUME);
		return true;
	}
	if(attr & DOS_ATTR_VOLUME) //check for root dir or fcb_findfirst
		LOG(LOG_DOSMISC,LOG_WARN)("findfirst for volumelabel used on fatDrive. Unhandled!!!!!");
#endif
	if(!getDirClustNum(_dir, &cwdDirCluster, false)) {
		DOS_SetError(DOSERR_PATH_NOT_FOUND);
		return false;
	}
	if (lfn_filefind_handle>=LFN_FILEFIND_MAX) {
		dta.SetDirID(0);
		dta.SetDirIDCluster(cwdDirCluster);
	} else {
		dpos[lfn_filefind_handle]=0;
		dnum[lfn_filefind_handle]=cwdDirCluster;
	}
	return FindNextInternal(cwdDirCluster, dta, &dummyClust);
}

char* removeTrailingSpaces(char* str) {
	char* end = str + strlen(str);
	while((*--end == ' ') && (end > str)) {};
	*++end = '\0';
	return str;
}

char* removeLeadingSpaces(char* str) {
	size_t len = strlen(str);
	size_t pos = strspn(str," ");
	memmove(str,str + pos,len - pos + 1);
	return str;
}

char* trimString(char* str) {
	return removeTrailingSpaces(removeLeadingSpaces(str));
}

Bit32u fatDrive::GetSectorCount(void) {
    return (loadedDisk->heads * loadedDisk->sectors * loadedDisk->cylinders) - partSectOff;
}

Bit32u fatDrive::GetSectorSize(void) {
    return getSectorSize();
}

Bit8u fatDrive::Read_AbsoluteSector_INT25(Bit32u sectnum, void * data) {
    return readSector(sectnum+partSectOff,data);
}

Bit8u fatDrive::Write_AbsoluteSector_INT25(Bit32u sectnum, void * data) {
    return writeSector(sectnum+partSectOff,data);
}

bool fatDrive::FindNextInternal(Bit32u dirClustNumber, DOS_DTA &dta, direntry *foundEntry) {
	direntry sectbuf[16]; /* 16 directory entries per sector */
	Bit32u logentsector; /* Logical entry sector */
	Bit32u entryoffset;  /* Index offset within sector */
	Bit32u tmpsector;
	Bit8u attrs;
	Bit16u dirPos;
	char srch_pattern[CROSS_LEN];
	char find_name[DOS_NAMELENGTH_ASCII];
	char lfind_name[LFN_NAMELENGTH+1];
	unsigned int lfn_max_ord = 0;
	unsigned char lfn_checksum = 0;
	bool lfn_ord_found[0x40];
	char extension[4];

	dta.GetSearchParams(attrs, srch_pattern, false);
	dirPos = lfn_filefind_handle>=LFN_FILEFIND_MAX?dta.GetDirID():dpos[lfn_filefind_handle];

nextfile:
	logentsector = dirPos / 16;
	entryoffset = dirPos % 16;

	if(dirClustNumber==0) {
		if(dirPos >= bootbuffer.rootdirentries) {
			if (lfn_filefind_handle<LFN_FILEFIND_MAX) {
				dpos[lfn_filefind_handle]=0;
				dnum[lfn_filefind_handle]=0;
			}
			DOS_SetError(DOSERR_NO_MORE_FILES);
			return false;
		}
		readSector(firstRootDirSect+logentsector,sectbuf);
	} else {
		tmpsector = getAbsoluteSectFromChain(dirClustNumber, logentsector);
		/* A zero sector number can't happen */
		if(tmpsector == 0) {
			if (lfn_filefind_handle<LFN_FILEFIND_MAX) {
				dpos[lfn_filefind_handle]=0;
				dnum[lfn_filefind_handle]=0;
			}
			DOS_SetError(DOSERR_NO_MORE_FILES);
			return false;
		}
		readSector(tmpsector,sectbuf);
	}
	dirPos++;
	if (lfn_filefind_handle>=LFN_FILEFIND_MAX) dta.SetDirID(dirPos);
	else dpos[lfn_filefind_handle]=dirPos;

	/* Deleted file entry */
	if (sectbuf[entryoffset].entryname[0] == 0xe5) {
        lfind_name[0] = 0; /* LFN code will memset() it in full upon next dirent */
        lfn_max_ord = 0;
        lfnRange.clear();
		goto nextfile;
	}

	/* End of directory list */
	if (sectbuf[entryoffset].entryname[0] == 0x00) {
		if (lfn_filefind_handle<LFN_FILEFIND_MAX) {
			dpos[lfn_filefind_handle]=0;
			dnum[lfn_filefind_handle]=0;
		}
		DOS_SetError(DOSERR_NO_MORE_FILES);
		return false;
	}
	memset(find_name,0,DOS_NAMELENGTH_ASCII);
	memset(extension,0,4);
	memcpy(find_name,&sectbuf[entryoffset].entryname[0],8);
	memcpy(extension,&sectbuf[entryoffset].entryname[8],3);
	
    if (!(sectbuf[entryoffset].attrib & DOS_ATTR_VOLUME)) {
        trimString(&find_name[0]);
        trimString(&extension[0]);
    }

	if (extension[0]!=0) {
		if (!(sectbuf[entryoffset].attrib & DOS_ATTR_VOLUME)) {
			strcat(find_name, ".");
		}
		strcat(find_name, extension);
	}

	if (sectbuf[entryoffset].attrib & DOS_ATTR_VOLUME)
        trimString(find_name);

	/* Compare attributes to search attributes */

	//TODO What about attrs = DOS_ATTR_VOLUME|DOS_ATTR_DIRECTORY ?
	if (attrs == DOS_ATTR_VOLUME) {
		if (dos.version.major >= 7 || uselfn) {
			/* skip LFN entries */
			if ((sectbuf[entryoffset].attrib & 0x3F) == 0x0F)
				goto nextfile;
		}

		if (!(sectbuf[entryoffset].attrib & DOS_ATTR_VOLUME)) goto nextfile;
		dirCache.SetLabel(find_name, false, true);
	} else if ((dos.version.major >= 7 || uselfn) && (sectbuf[entryoffset].attrib & 0x3F) == 0x0F) { /* long filename piece */
		struct direntry_lfn *dlfn = (struct direntry_lfn*)(&sectbuf[entryoffset]);

		/* assume last entry comes first, because that's how Windows 9x does it and that is how you're supposed to do it according to Microsoft */
		if (dlfn->LDIR_Ord & 0x40) {
			lfn_max_ord = (dlfn->LDIR_Ord & 0x3F); /* NTS: First entry has ordinal 1, this is the HIGHEST ordinal in the LFN. The other entries follow in descending ordinal. */
			for (unsigned int i=0;i < 0x40;i++) lfn_ord_found[i] = false;
			lfn_checksum = dlfn->LDIR_Chksum;
			memset(lfind_name,0,LFN_NAMELENGTH);
			lfnRange.clear();
			lfnRange.dirPos_start = dirPos - 1; /* NTS: The code above has already incremented dirPos */
		}

		if (lfn_max_ord != 0 && (dlfn->LDIR_Ord & 0x3F) > 0 && (dlfn->LDIR_Ord & 0x3Fu) <= lfn_max_ord && dlfn->LDIR_Chksum == lfn_checksum) {
			unsigned int oidx = (dlfn->LDIR_Ord & 0x3Fu) - 1u;
			unsigned int stridx = oidx * 13u;

			if ((stridx+13u) <= LFN_NAMELENGTH) {
				for (unsigned int i=0;i < 5;i++)
					lfind_name[stridx+i+0] = (char)(dlfn->LDIR_Name1[i] & 0xFF);
				for (unsigned int i=0;i < 6;i++)
					lfind_name[stridx+i+5] = (char)(dlfn->LDIR_Name2[i] & 0xFF);
				for (unsigned int i=0;i < 2;i++)
					lfind_name[stridx+i+11] = (char)(dlfn->LDIR_Name3[i] & 0xFF);

				lfn_ord_found[oidx] = true;
			}
		}

		goto nextfile;
	} else {
		if (~attrs & sectbuf[entryoffset].attrib & (DOS_ATTR_DIRECTORY | DOS_ATTR_VOLUME | DOS_ATTR_SYSTEM | DOS_ATTR_HIDDEN) ) {
            lfind_name[0] = 0; /* LFN code will memset() it in full upon next dirent */
            lfn_max_ord = 0;
            lfnRange.clear();
			goto nextfile;
		}
	}

	if (lfn_max_ord != 0) {
		bool ok = false;
		unsigned int complete = 0;
		for (unsigned int i=0;i < lfn_max_ord;i++) complete += lfn_ord_found[i]?1:0;

		if (complete == lfn_max_ord) {
			unsigned char chk = 0;
			for (unsigned int i=0;i < 11;i++) {
				chk = ((chk & 1u) ? 0x80u : 0x00u) + (chk >> 1u) + sectbuf[entryoffset].entryname[i];
			}

			if (lfn_checksum == chk) {
				lfnRange.dirPos_end = dirPos - 1; /* NTS: The code above has already incremented dirPos */
				ok = true;
			}
		}

		if (!ok) {
			lfind_name[0] = 0; /* LFN code will memset() it in full upon next dirent */
			lfn_max_ord = 0;
			lfnRange.clear();
		}
	}
	else {
		lfind_name[0] = 0; /* LFN code will memset() it in full upon next dirent */
		lfn_max_ord = 0;
		lfnRange.clear();
	}

	/* Compare name to search pattern. Skip long filename match if no long filename given. */
	if (!(WildFileCmp(find_name,srch_pattern) || (lfn_max_ord != 0 && lfind_name[0] != 0 && LWildFileCmp(lfind_name,srch_pattern)))) {
		lfind_name[0] = 0; /* LFN code will memset() it in full upon next dirent */
		lfn_max_ord = 0;
		lfnRange.clear();
		goto nextfile;
	}

	// Drive emulation does not need to require a LFN in case there is no corresponding 8.3 names.
	if (lfind_name[0] == 0) strcpy(lfind_name,find_name);

	//copyDirEntry(&sectbuf[entryoffset], foundEntry);

	dta.SetResult(find_name, lfind_name, sectbuf[entryoffset].entrysize, sectbuf[entryoffset].modDate, sectbuf[entryoffset].modTime, sectbuf[entryoffset].attrib);

	memcpy(foundEntry, &sectbuf[entryoffset], sizeof(direntry));

	return true;
}

bool fatDrive::FindNext(DOS_DTA &dta) {
	direntry dummyClust = {};

	return FindNextInternal(lfn_filefind_handle>=LFN_FILEFIND_MAX?dta.GetDirIDCluster():(dnum[lfn_filefind_handle]?dnum[lfn_filefind_handle]:0), dta, &dummyClust);
}

bool fatDrive::SetFileAttr(const char *name, Bit16u attr) {
    direntry fileEntry = {};
	Bit32u dirClust, subEntry;

	/* you cannot set file attr root directory (right?) */
	if (*name == 0) {
		DOS_SetError(DOSERR_ACCESS_DENIED);
		return false;
	}

	if(!getFileDirEntry(name, &fileEntry, &dirClust, &subEntry, /*dirOk*/true)) {
		return false;
	} else {
		fileEntry.attrib=(uint8_t)attr;
		directoryChange(dirClust, &fileEntry, (int32_t)subEntry);
	}
	return true;
}

bool fatDrive::GetFileAttr(char *name, Bit16u *attr) {
    direntry fileEntry = {};
	Bit32u dirClust, subEntry;
	if(!getFileDirEntry(name, &fileEntry, &dirClust, &subEntry)) {
		char dirName[DOS_NAMELENGTH_ASCII];
		char pathName[11];

		/* Can we even get the name of the directory itself? */
		if(!getEntryName(name, &dirName[0])) return false;
		convToDirFile(&dirName[0], &pathName[0]);

		/* Get parent directory starting cluster */
		if(!getDirClustNum(name, &dirClust, true)) return false;

		/* Find directory entry in parent directory */
		Bit32s fileidx = 2;
		if (dirClust==0) fileidx = 0;	// root directory
		Bit32s last_idx=0;
		while(directoryBrowse(dirClust, &fileEntry, fileidx, last_idx)) {
			if(memcmp(&fileEntry.entryname, &pathName[0], 11) == 0) {
				*attr=fileEntry.attrib;
				return true;
			}
			last_idx=fileidx;
			fileidx++;
		}
		return false;
	} else *attr=fileEntry.attrib;
	return true;
}

bool fatDrive::GetFileAttrEx(char* name, struct stat *status) {
	return false;
}

DWORD fatDrive::GetCompressedSize(char* name) {
	return 0;
}

HANDLE fatDrive::CreateOpenFile(const char* name) {
	DOS_SetError(1);
	return INVALID_HANDLE_VALUE;
}

bool fatDrive::directoryBrowse(Bit32u dirClustNumber, direntry *useEntry, Bit32s entNum, Bit32s start/*=0*/) {
	direntry sectbuf[16];	/* 16 directory entries per sector */
	Bit32u logentsector;	/* Logical entry sector */
	Bit32u entryoffset = 0;	/* Index offset within sector */
	Bit32u tmpsector;
	if ((start<0) || (start>65535)) return false;
	Bit16u dirPos = (Bit16u)start;
	if (entNum<start) return false;
	entNum-=start;

	while(entNum>=0) {

		logentsector = dirPos / 16;
		entryoffset = dirPos % 16;

		if(dirClustNumber==0) {
			if(dirPos >= bootbuffer.rootdirentries) return false;
			tmpsector = firstRootDirSect+logentsector;
			readSector(tmpsector,sectbuf);
		} else {
			tmpsector = getAbsoluteSectFromChain(dirClustNumber, logentsector);
			/* A zero sector number can't happen */
			if(tmpsector == 0) return false;
			readSector(tmpsector,sectbuf);
		}
		dirPos++;


		/* End of directory list */
		if (sectbuf[entryoffset].entryname[0] == 0x00) return false;
		--entNum;
	}

	memcpy(useEntry, &sectbuf[entryoffset],sizeof(direntry));
	return true;
}

bool fatDrive::directoryChange(Bit32u dirClustNumber, direntry *useEntry, Bit32s entNum) {
	direntry sectbuf[16];	/* 16 directory entries per sector */
	Bit32u logentsector;	/* Logical entry sector */
	Bit32u entryoffset = 0;	/* Index offset within sector */
	Bit32u tmpsector = 0;
	Bit16u dirPos = 0;
	
	while(entNum>=0) {
		
		logentsector = dirPos / 16;
		entryoffset = dirPos % 16;

		if(dirClustNumber==0) {
			if(dirPos >= bootbuffer.rootdirentries) return false;
			tmpsector = firstRootDirSect+logentsector;
			readSector(tmpsector,sectbuf);
		} else {
			tmpsector = getAbsoluteSectFromChain(dirClustNumber, logentsector);
			/* A zero sector number can't happen */
			if(tmpsector == 0) return false;
			readSector(tmpsector,sectbuf);
		}
		dirPos++;


		/* End of directory list */
		if (sectbuf[entryoffset].entryname[0] == 0x00) return false;
		--entNum;
	}
	if(tmpsector != 0) {
        memcpy(&sectbuf[entryoffset], useEntry, sizeof(direntry));
		writeSector(tmpsector, sectbuf);
        return true;
	} else {
		return false;
	}
}

bool fatDrive::addDirectoryEntry(Bit32u dirClustNumber, direntry useEntry,const char *lfn) {
	direntry sectbuf[16]; /* 16 directory entries per 512 byte sector */
	Bit32u logentsector; /* Logical entry sector */
	Bit32u entryoffset;  /* Index offset within sector */
	Bit32u tmpsector;
	Bit16u dirPos = 0;
	unsigned int need = 1;
	unsigned int found = 0;
	Bit16u dirPosFound = 0;

	if (lfn != NULL && *lfn != 0) {
		/* 13 characters per LFN entry.
		 * FIXME: When we convert the LFN to wchar using code page, strlen() prior to conversion will not work,
		 *        convert first then count wchar_t characters. */
		need = (unsigned int)(1 + ((strlen(lfn) + 12) / 13))/*round up*/;
	}

	for(;;) {
		logentsector = ((Bit32u)((size_t)dirPos / 16)); /* Logical entry sector */
		entryoffset = ((Bit32u)((size_t)dirPos % 16)); /* Index offset within sector */

		if(dirClustNumber==0) {
			if(dirPos >= bootbuffer.rootdirentries) return false;
			tmpsector = firstRootDirSect+logentsector;
		} else {
			tmpsector = getAbsoluteSectFromChain(dirClustNumber, logentsector);
			/* A zero sector number can't happen - we need to allocate more room for this directory*/
			if(tmpsector == 0) {
				Bit32u newClust;
				newClust = appendCluster(dirClustNumber);
				if(newClust == 0) return false;
				zeroOutCluster(newClust);
				/* Try again to get tmpsector */
				tmpsector = getAbsoluteSectFromChain(dirClustNumber, logentsector);
				if(tmpsector == 0) return false; /* Give up if still can't get more room for directory */
			}
		}
		readSector(tmpsector,sectbuf);

		/* Deleted file entry or end of directory list */
		if ((sectbuf[entryoffset].entryname[0] == 0xe5) || (sectbuf[entryoffset].entryname[0] == 0x00)) {
			if (found == 0) dirPosFound = dirPos;

			if ((++found) >= need) {
				sectbuf[entryoffset] = useEntry;
				writeSector(tmpsector,sectbuf);

				/* Add LFN entries */
				if (need != 1/*LFN*/) {
					Bit16u lfnbuf[LFN_NAMELENGTH+13]; /* on disk, LFNs are WCHAR unicode (UCS-16) */

					if (lfn == NULL || !*lfn) return false;

					/* TODO: ANSI LFN convert to wchar here according to code page */

					unsigned int o = 0;
					const char *scan = lfn;

					while (*scan) {
						if (o >= LFN_NAMELENGTH) return false; /* Nope! */
						lfnbuf[o++] = (Bit16u)((unsigned char)(*scan++));
					}

					/* on disk, LFNs are padded with 0x0000 followed by a run of 0xFFFF to fill the dirent */
					lfnbuf[o++] = 0x0000;
					for (unsigned int i=0;i < 13;i++) lfnbuf[o++] = 0xFFFF;
					if (o > LFN_NAMELENGTH+13) return false;

					unsigned char chk = 0;
					for (unsigned int i=0;i < 11;i++) {
						chk = ((chk & 1u) ? 0x80u : 0x00u) + (chk >> 1u) + useEntry.entryname[i];
					}

					dirPos = dirPosFound;
					for (unsigned int s=0;s < (need-1u);s++) {
						unsigned int lfnsrci = (need-2u-s);
						unsigned int lfnsrc = lfnsrci * 13;

						logentsector = ((Bit32u)((size_t)dirPos / 16)); /* Logical entry sector */
						entryoffset = ((Bit32u)((size_t)dirPos % 16)); /* Index offset within sector */

						if(dirClustNumber==0) {
							if(dirPos >= bootbuffer.rootdirentries) return false;
							tmpsector = firstRootDirSect+logentsector;
						} else {
							tmpsector = getAbsoluteSectFromChain(dirClustNumber, logentsector);
							/* A zero sector number can't happen - we need to allocate more room for this directory*/
							if(tmpsector == 0) return false;
						}
						readSector(tmpsector,sectbuf);

						direntry_lfn *dlfn = (direntry_lfn*)(&sectbuf[entryoffset]);

						memset(dlfn,0,sizeof(*dlfn));

						dlfn->LDIR_Ord = (s == 0 ? 0x40 : 0x00) + lfnsrci + 1;
						dlfn->LDIR_Chksum = chk;
						dlfn->attrib = 0x0F;

						for (unsigned int i=0;i < 5;i++) dlfn->LDIR_Name1[i] = lfnbuf[lfnsrc++];
						for (unsigned int i=0;i < 6;i++) dlfn->LDIR_Name2[i] = lfnbuf[lfnsrc++];
						for (unsigned int i=0;i < 2;i++) dlfn->LDIR_Name3[i] = lfnbuf[lfnsrc++];

						loadedDisk->Write_AbsoluteSector(tmpsector,sectbuf);
						dirPos++;
					}
				}

				break;
			}
		}
		else {
			found = 0;
		}

		dirPos++;
	}

	return true;
}

void fatDrive::zeroOutCluster(Bit32u clustNumber) {
	Bit8u secBuffer[512];

	memset(&secBuffer[0], 0, 512);

	int i;
	for(i=0;i<bootbuffer.sectorspercluster;i++) {
		writeSector(getAbsoluteSectFromChain(clustNumber,i), &secBuffer[0]);
	}
}

bool fatDrive::MakeDir(char *dir) {
	const char *lfn = NULL;
	Bit32u dummyClust, dirClust, subEntry;
	direntry tmpentry;
	char dirName[DOS_NAMELENGTH_ASCII];
	char pathName[11], path[LFN_NAMELENGTH+2];
    uint16_t ct,cd;

	/* Can we even get the name of the directory itself? */
	if(!getEntryName(dir, &dirName[0])) return false;
	convToDirFile(&dirName[0], &pathName[0]);

	/* Fail to make directory if something of that name already exists */
	if(getFileDirEntry(dir,&tmpentry,&dummyClust,&subEntry,/*dirOk*/true)) return false;

	/* Can we find the base directory? */
	if(!getDirClustNum(dir, &dirClust, true)) return false;

	dummyClust = getFirstFreeClust();
	/* No more space */
	if(dummyClust == 0) return false;
	
	if(!allocateCluster(dummyClust, 0)) return false;

	/* NTS: "dir" is the full relative path. For LFN creation to work we need only the final element of the path */
	if (uselfn && !force) {
		lfn = strrchr(dir,'\\');

		if (lfn != NULL) {
			lfn++; /* step past '\' */
			strcpy(path, dir);
			*(strrchr(path,'\\')+1)=0;
		} else {
			lfn = dir; /* no path elements */
			*path=0;
		}

		if (filename_not_strict_8x3(lfn)) {
			char *sfn=Generate_SFN(path, lfn);
			if (sfn!=NULL) convToDirFile(sfn, &pathName[0]);
		} else
			lfn = NULL;
	}

	zeroOutCluster(dummyClust);

	time_t_to_DOS_DateTime(/*&*/ct,/*&*/cd,::time(NULL));

	/* Can we find the base directory? */
	//if(!getDirClustNum(dir, &dirClust, true)) return false;
	
	/* Add the new directory to the base directory */
	memset(&tmpentry,0, sizeof(direntry));
	memcpy(&tmpentry.entryname, &pathName[0], 11);
	tmpentry.loFirstClust = (Bit16u)(dummyClust & 0xffff);
	tmpentry.hiFirstClust = (Bit16u)(dummyClust >> 16);
	tmpentry.attrib = DOS_ATTR_DIRECTORY;
    tmpentry.modTime = ct;
    tmpentry.modDate = cd;
	addDirectoryEntry(dirClust, tmpentry, lfn);

	/* Add the [.] and [..] entries to our new directory*/
	/* [.] entry */
	memset(&tmpentry,0, sizeof(direntry));
	memcpy(&tmpentry.entryname, ".          ", 11);
	tmpentry.loFirstClust = (Bit16u)(dummyClust & 0xffff);
	tmpentry.hiFirstClust = (Bit16u)(dummyClust >> 16);
	tmpentry.attrib = DOS_ATTR_DIRECTORY;
    tmpentry.modTime = ct;
    tmpentry.modDate = cd;
	addDirectoryEntry(dummyClust, tmpentry);

	/* [..] entry */
	memset(&tmpentry,0, sizeof(direntry));
	memcpy(&tmpentry.entryname, "..         ", 11);
	tmpentry.loFirstClust = (Bit16u)(dirClust & 0xffff);
	tmpentry.hiFirstClust = (Bit16u)(dirClust >> 16);
	tmpentry.attrib = DOS_ATTR_DIRECTORY;
    tmpentry.modTime = ct;
    tmpentry.modDate = cd;
	addDirectoryEntry(dummyClust, tmpentry);

	return true;
}

bool fatDrive::RemoveDir(char *dir) {
	Bit32u dummyClust, dirClust, subEntry;
	direntry tmpentry;
	char dirName[DOS_NAMELENGTH_ASCII];
	char pathName[11];

	/* you cannot rmdir root directory */
	if (*dir == 0) {
		DOS_SetError(DOSERR_ACCESS_DENIED);
		return false;
	}

	/* Can we even get the name of the directory itself? */
	if(!getEntryName(dir, &dirName[0])) return false;
	convToDirFile(&dirName[0], &pathName[0]);

	/* Get directory starting cluster */
	lfnRange.clear();
	if(!getFileDirEntry(dir,&tmpentry,&dirClust,&subEntry,/*dirOk*/true)) return false; /* dirClust is parent dir of directory */
	if (!(tmpentry.attrib & DOS_ATTR_DIRECTORY)) return false;
	dummyClust = tmpentry.loFirstClust;
	lfnRange_t dir_lfn_range = lfnRange; /* copy down LFN results before they are obliterated by the next call to FindNextInternal. */

	/* Can't remove root directory */
	if(dummyClust == 0) return false;

	/* Check to make sure directory is empty */
	Bit32u filecount = 0;
	/* Set to 2 to skip first 2 entries, [.] and [..] */
	Bit32s fileidx = 2;
	while(directoryBrowse(dummyClust, &tmpentry, fileidx)) {
		/* Check for non-deleted files */
		if(tmpentry.entryname[0] != 0xe5) filecount++;
		fileidx++;
	}

	/* Return if directory is not empty */
	if(filecount > 0) return false;

	/* delete LFNs */
	if (!dir_lfn_range.empty() && (dos.version.major >= 7 || uselfn)) {
		/* last LFN entry should be fileidx */
		if (dir_lfn_range.dirPos_start >= dir_lfn_range.dirPos_end) return false;
		if (dir_lfn_range.dirPos_end != subEntry) LOG_MSG("FAT warning: LFN dirPos_end=%u fileidx=%u (mismatch)",dir_lfn_range.dirPos_end,subEntry);
		for (unsigned int didx=dir_lfn_range.dirPos_start;didx < dir_lfn_range.dirPos_end;didx++) {
			if (directoryBrowse(dirClust,&tmpentry,didx)) {
				tmpentry.entryname[0] = 0xe5;
				directoryChange(dirClust,&tmpentry,didx);
			}
		}
	}

	/* remove primary 8.3 entry */
	if (!directoryBrowse(dirClust, &tmpentry, subEntry)) return false;
	tmpentry.entryname[0] = 0xe5;
	if (!directoryChange(dirClust, &tmpentry, subEntry)) return false;

	/* delete allocation chain */
	deleteClustChain(dummyClust, 0);
	return true;
}

bool fatDrive::Rename(char * oldname, char * newname) {
	const char *lfn = NULL;
	/* you cannot rename root directory */
	if (*oldname == 0 || *newname == 0) {
		DOS_SetError(DOSERR_ACCESS_DENIED);
		return false;
	}

    direntry fileEntry1 = {}, fileEntry2 = {};
	Bit32u dirClust1, subEntry1, dirClust2, subEntry2;
	char dirName2[DOS_NAMELENGTH_ASCII];
	char pathName2[11], path[LFN_NAMELENGTH+2];
	lfnRange_t dir_lfn_range;

	/* Check that old name exists (file or directory) */
	lfnRange.clear();
	if(!getFileDirEntry(oldname, &fileEntry1, &dirClust1, &subEntry1, /*dirOk*/true)) return false;
	dir_lfn_range = lfnRange;

	/* Check if new name (file or directory) already exists, fail if so */
	if(getFileDirEntry(newname, &fileEntry2, &dirClust2, &subEntry2, /*dirOk*/true)&&!(uselfn&&!force&&strcmp(oldname, newname)&&!strcasecmp(oldname, newname))) return false;

	/* Can we even get the name of the file itself? */
	if(!getEntryName(newname, &dirName2[0])||!strlen(trim(dirName2))) return false;
	convToDirFile(&dirName2[0], &pathName2[0]);

	/* Can we find the base directory of the new name? (we know the parent dir of oldname in dirClust1) */
	if(!getDirClustNum(newname, &dirClust2, true)) return false;

	/* NTS: "newname" is the full relative path. For LFN creation to work we need only the final element of the path */
	if (uselfn && !force) {
		lfn = strrchr(newname,'\\');

		if (lfn != NULL) {
			lfn++; /* step past '\' */
			strcpy(path, newname);
			*(strrchr(path,'\\')+1)=0;
		} else {
			lfn = newname; /* no path elements */
			*path=0;
		}

		if (filename_not_strict_8x3(lfn)) {
			char oldchar=fileEntry1.entryname[0];
			fileEntry1.entryname[0] = 0xe5;
			directoryChange(dirClust1, &fileEntry1, (Bit32s)subEntry1);
			char *sfn=Generate_SFN(path, lfn);
			if (sfn!=NULL) convToDirFile(sfn, &pathName2[0]);
			fileEntry1.entryname[0] = oldchar;
			directoryChange(dirClust1, &fileEntry1, (Bit32s)subEntry1);
		} else
			lfn = NULL;
	}

	/* add new dirent */
	memcpy(&fileEntry2, &fileEntry1, sizeof(direntry));
	memcpy(&fileEntry2.entryname, &pathName2[0], 11);
	addDirectoryEntry(dirClust2, fileEntry2, lfn);

	/* Remove old 8.3 SFN entry */
	fileEntry1.entryname[0] = 0xe5;
	directoryChange(dirClust1, &fileEntry1, (Bit32s)subEntry1);

	/* remove LFNs of old entry only if emulating LFNs or DOS version 7.0.
	 * Earlier DOS versions ignore LFNs. */
	if (!dir_lfn_range.empty() && (dos.version.major >= 7 || uselfn)) {
		/* last LFN entry should be fileidx */
		if (dir_lfn_range.dirPos_start >= dir_lfn_range.dirPos_end) return false;
		if (dir_lfn_range.dirPos_end != subEntry1) LOG_MSG("FAT warning: LFN dirPos_end=%u fileidx=%u (mismatch)",dir_lfn_range.dirPos_end,subEntry1);
		for (unsigned int didx=dir_lfn_range.dirPos_start;didx < dir_lfn_range.dirPos_end;didx++) {
			if (directoryBrowse(dirClust1,&fileEntry1,didx)) {
				fileEntry1.entryname[0] = 0xe5;
				directoryChange(dirClust1,&fileEntry1,didx);
			}
		}
	}

	return true;
}

bool fatDrive::TestDir(char *dir) {
	Bit32u dummyClust;
	return getDirClustNum(dir, &dummyClust, false);
}

