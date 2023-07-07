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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <limits.h>

#include "dosbox.h"
#include "dos_inc.h"
#include "drives.h"
#include "support.h"
#include "cross.h"
#include "inout.h"
#include "jega.h"
#include "cp437_uni.h"
#include "cp932_uni.h"

#if defined(WIN32)
#define	fseek	_fseeki64
#define	ftell	_ftelli64
#endif

#if defined(WIN32)
#define host_cnv_use_wchar
typedef wchar_t host_cnv_char_t;
#else
typedef char host_cnv_char_t;
#endif

static uint16_t ldid[256];
static std::string ldir[256];
extern int lfn_filefind_handle;
static host_cnv_char_t cpcnv_temp[4096], cpcnv_ltemp[4096];

class localFile : public DOS_File {
public:
	localFile(const char* name, FILE * handle);
	bool Read(Bit8u * data,Bit16u * size);
	bool Write(Bit8u * data,Bit16u * size);
	bool Seek(Bit32u * pos,Bit32u type);
	bool Close();
	Bit16u GetInformation(void);
	bool SetDateTime(Bit16u ndate, Bit16u ntime);
	bool UpdateDateTimeFromHost(void);   
	void FlagReadOnlyMedium(void);
	void Flush(void);
#if defined(WIN32)
	void SetFullPath(char *full_path) { path = strdup(full_path); }
#endif
private:
	FILE * fhandle;
	bool read_only_medium;
	enum { NONE,READ,WRITE } last_action;
#if defined(WIN32)
	bool newtime = false;
	FILETIME filetime;
	char* path = NULL;
#endif
};

enum {
	UTF8ERR_INVALID=-1,
	UTF8ERR_NO_ROOM=-2
};

int utf8_encode(char **ptr, const char *fence, uint32_t code) {
    int uchar_size=1;
    char *p = *ptr;

    if (!p) return UTF8ERR_NO_ROOM;
    if (code >= (uint32_t)0x80000000UL) return UTF8ERR_INVALID;
    if (p >= fence) return UTF8ERR_NO_ROOM;

    if (code >= 0x4000000) uchar_size = 6;
    else if (code >= 0x200000) uchar_size = 5;
    else if (code >= 0x10000) uchar_size = 4;
    else if (code >= 0x800) uchar_size = 3;
    else if (code >= 0x80) uchar_size = 2;

    if ((p+uchar_size) > fence) return UTF8ERR_NO_ROOM;

    switch (uchar_size) {
        case 1: *p++ = (char)code;
            break;
        case 2: *p++ = (char)(0xC0 | (code >> 6));
            *p++ = (char)(0x80 | (code & 0x3F));
            break;
        case 3: *p++ = (char)(0xE0 | (code >> 12));
            *p++ = (char)(0x80 | ((code >> 6) & 0x3F));
            *p++ = (char)(0x80 | (code & 0x3F));
            break;
        case 4: *p++ = (char)(0xF0 | (code >> 18));
            *p++ = (char)(0x80 | ((code >> 12) & 0x3F));
            *p++ = (char)(0x80 | ((code >> 6) & 0x3F));
            *p++ = (char)(0x80 | (code & 0x3F));
            break;
        case 5: *p++ = (char)(0xF8 | (code >> 24));
            *p++ = (char)(0x80 | ((code >> 18) & 0x3F));
            *p++ = (char)(0x80 | ((code >> 12) & 0x3F));
            *p++ = (char)(0x80 | ((code >> 6) & 0x3F));
            *p++ = (char)(0x80 | (code & 0x3F));
            break;
        case 6: *p++ = (char)(0xFC | (code >> 30));
            *p++ = (char)(0x80 | ((code >> 24) & 0x3F));
            *p++ = (char)(0x80 | ((code >> 18) & 0x3F));
            *p++ = (char)(0x80 | ((code >> 12) & 0x3F));
            *p++ = (char)(0x80 | ((code >> 6) & 0x3F));
            *p++ = (char)(0x80 | (code & 0x3F));
            break;
    }

    *ptr = p;
    return 0;
}

int utf8_decode(const char **ptr,const char *fence) {
    const char *p = *ptr;
    int uchar_size=1;
    int ret = 0,c;

    if (!p) return UTF8ERR_NO_ROOM;
    if (p >= fence) return UTF8ERR_NO_ROOM;

    ret = (unsigned char)(*p);
    if (ret >= 0xFE) { p++; return UTF8ERR_INVALID; }
    else if (ret >= 0xFC) uchar_size=6;
    else if (ret >= 0xF8) uchar_size=5;
    else if (ret >= 0xF0) uchar_size=4;
    else if (ret >= 0xE0) uchar_size=3;
    else if (ret >= 0xC0) uchar_size=2;
    else if (ret >= 0x80) { p++; return UTF8ERR_INVALID; }

    if ((p+uchar_size) > fence)
        return UTF8ERR_NO_ROOM;

    switch (uchar_size) {
        case 1: p++;
            break;
        case 2: ret = (ret&0x1F)<<6; p++;
            c = (unsigned char)(*p++); if ((c&0xC0) != 0x80) return UTF8ERR_INVALID;
            ret |= c&0x3F;
            break;
        case 3: ret = (ret&0xF)<<12; p++;
            c = (unsigned char)(*p++); if ((c&0xC0) != 0x80) return UTF8ERR_INVALID;
            ret |= (c&0x3F)<<6;
            c = (unsigned char)(*p++); if ((c&0xC0) != 0x80) return UTF8ERR_INVALID;
            ret |= c&0x3F;
            break;
        case 4: ret = (ret&0x7)<<18; p++;
            c = (unsigned char)(*p++); if ((c&0xC0) != 0x80) return UTF8ERR_INVALID;
            ret |= (c&0x3F)<<12;
            c = (unsigned char)(*p++); if ((c&0xC0) != 0x80) return UTF8ERR_INVALID;
            ret |= (c&0x3F)<<6;
            c = (unsigned char)(*p++); if ((c&0xC0) != 0x80) return UTF8ERR_INVALID;
            ret |= c&0x3F;
            break;
        case 5: ret = (ret&0x3)<<24; p++;
            c = (unsigned char)(*p++); if ((c&0xC0) != 0x80) return UTF8ERR_INVALID;
            ret |= (c&0x3F)<<18;
            c = (unsigned char)(*p++); if ((c&0xC0) != 0x80) return UTF8ERR_INVALID;
            ret |= (c&0x3F)<<12;
            c = (unsigned char)(*p++); if ((c&0xC0) != 0x80) return UTF8ERR_INVALID;
            ret |= (c&0x3F)<<6;
            c = (unsigned char)(*p++); if ((c&0xC0) != 0x80) return UTF8ERR_INVALID;
            ret |= c&0x3F;
            break;
        case 6: ret = (ret&0x1)<<30; p++;
            c = (unsigned char)(*p++); if ((c&0xC0) != 0x80) return UTF8ERR_INVALID;
            ret |= (c&0x3F)<<24;
            c = (unsigned char)(*p++); if ((c&0xC0) != 0x80) return UTF8ERR_INVALID;
            ret |= (c&0x3F)<<18;
            c = (unsigned char)(*p++); if ((c&0xC0) != 0x80) return UTF8ERR_INVALID;
            ret |= (c&0x3F)<<12;
            c = (unsigned char)(*p++); if ((c&0xC0) != 0x80) return UTF8ERR_INVALID;
            ret |= (c&0x3F)<<6;
            c = (unsigned char)(*p++); if ((c&0xC0) != 0x80) return UTF8ERR_INVALID;
            ret |= c&0x3F;
            break;
    }

    *ptr = p;
    return ret;
}

template <class MT> bool String_SBCS_TO_HOST_UTF8(char *d/*CROSS_LEN*/,const char *s/*CROSS_LEN*/,const MT *map,const size_t map_max) {
    const char* df = d + CROSS_LEN - 1;
	const char *sf = s + CROSS_LEN - 1;

    while (*s != 0 && s < sf) {
        unsigned char ic = (unsigned char)(*s++);
        if (ic >= map_max) return false; // non-representable
        MT wc;
            wc = map[ic]; // output: unicode character

        if (utf8_encode(&d,df,(uint32_t)wc) < 0) // will advance d by however many UTF-8 bytes are needed
            return false; // non-representable, or probably just out of room
    }

    if (d > df) return false;
    *d = 0;

    return true;
}

template <class MT> bool String_SBCS_TO_HOST_UTF16(uint16_t *d/*CROSS_LEN*/,const char *s/*CROSS_LEN*/,const MT *map,const size_t map_max) {
    const uint16_t* df = d + CROSS_LEN - 1;
	const char *sf = s + CROSS_LEN - 1;

    while (*s != 0 && s < sf) {
        unsigned char ic = (unsigned char)(*s++);
        if (ic >= map_max) return false; // non-representable
        MT wc;
            wc = map[ic]; // output: unicode character

        *d++ = (uint16_t)wc;
    }

    if (d > df) return false;
    *d = 0;

    return true;
}

template <class MT> int SBCS_From_Host_Find(int c,const MT *map,const size_t map_max) {
    for (size_t i=0;i < map_max;i++) {
        if ((MT)c == map[i])
            return (int)i;
    }

    return -1;
}

template <class MT> bool String_HOST_TO_SBCS_UTF8(char *d/*CROSS_LEN*/,const char *s/*CROSS_LEN*/,const MT *map,const size_t map_max) {
    const char *sf = s + CROSS_LEN - 1;
    const char* df = d + CROSS_LEN - 1;

    while (*s != 0 && s < sf) {
        int ic;
        if ((ic=utf8_decode(&s,sf)) < 0)
            return false; // non-representable

        int oc = SBCS_From_Host_Find<MT>(ic,map,map_max);
        if (oc < 0)
            return false; // non-representable

        if (d >= df) return false;
        *d++ = (char)oc;
    }

    if (d > df) return false;
    *d = 0;

    return true;
}

template <class MT> bool String_HOST_TO_SBCS_UTF16(char *d/*CROSS_LEN*/,const uint16_t *s/*CROSS_LEN*/,const MT *map,const size_t map_max) {
    const uint16_t *sf = s + CROSS_LEN - 1;
    const char* df = d + CROSS_LEN - 1;

    while (*s != 0 && s < sf) {
        int ic;
        ic = (int)(*s++);

        int oc = SBCS_From_Host_Find<MT>(ic,map,map_max);
        if (oc < 0)
            return false; // non-representable

        if (d >= df) return false;
        *d++ = (char)oc;
    }

    if (d > df) return false;
    *d = 0;

    return true;
}

template <class MT> int DBCS_From_Host_Find(int c,const MT *hitbl,const MT *rawtbl,const size_t rawtbl_max) {
    for (size_t h=0;h < 1024;h++) {
        MT ofs = hitbl[h];

        if (ofs == 0xFFFF) continue;
        if ((size_t)(ofs+ (Bitu)0x40) > rawtbl_max) return -1;

        for (size_t l=0;l < 0x40;l++) {
            if ((MT)c == rawtbl[ofs+l])
                return (int)((h << 6) + l);
        }
    }

    return -1;
}

template <class MT> bool String_HOST_TO_DBCS_UTF8(char *d/*CROSS_LEN*/,const char *s/*CROSS_LEN*/,const MT *hitbl,const MT *rawtbl,const size_t rawtbl_max) {
    const char *sf = s + CROSS_LEN - 1;
    const char* df = d + CROSS_LEN - 1;

    while (*s != 0 && s < sf) {
        int ic;
        if ((ic=utf8_decode(&s,sf)) < 0)
            return false; // non-representable

        int oc = DBCS_From_Host_Find<MT>(ic,hitbl,rawtbl,rawtbl_max);
        if (oc < 0)
            return false; // non-representable

        if (oc >= 0x100) {
            if ((d+1) >= df) return false;
            *d++ = (char)(oc >> 8U);
            *d++ = (char)oc;
        }
        else {
            if (d >= df) return false;
            *d++ = (char)oc;
        }
    }

    if (d > df) return false;
    *d = 0;

    return true;
}

template <class MT> bool String_HOST_TO_DBCS_UTF16(char *d/*CROSS_LEN*/,const uint16_t *s/*CROSS_LEN*/,const MT *hitbl,const MT *rawtbl,const size_t rawtbl_max) {
    const uint16_t *sf = s + CROSS_LEN - 1;
    const char* df = d + CROSS_LEN - 1;

    while (*s != 0 && s < sf) {
        int ic;
        ic = (int)(*s++);

        int oc = DBCS_From_Host_Find<MT>(ic,hitbl,rawtbl,rawtbl_max);
        if (oc < 0)
            return false; // non-representable

        if (oc >= 0x100) {
            if ((d+1) >= df) return false;
            *d++ = (char)(oc >> 8U);
            *d++ = (char)oc;
        }
        else {
            if (d >= df) return false;
            *d++ = (char)oc;
        }
    }

    if (d > df) return false;
    *d = 0;

    return true;
}

template <class MT> bool String_DBCS_TO_HOST_UTF8(char *d/*CROSS_LEN*/,const char *s/*CROSS_LEN*/,const MT *hitbl,const MT *rawtbl,const size_t rawtbl_max) {
    const char* df = d + CROSS_LEN - 1;
	const char *sf = s + CROSS_LEN - 1;

    while (*s != 0 && s < sf) {
        uint16_t ic = (unsigned char)(*s++);
        if ((dos.loaded_codepage==932 &&((ic & 0xE0) == 0x80 || (ic & 0xE0) == 0xE0)) && (ic & 0x80) == 0x80) {
            if (*s == 0) return false;
            ic <<= 8U;
            ic += (unsigned char)(*s++);
        }

        MT rawofs = hitbl[ic >> 6];
        if (rawofs == 0xFFFF)
            return false;

        if ((size_t)(rawofs+ (Bitu)0x40) > rawtbl_max)
            return false;
        MT wc = rawtbl[rawofs + (ic & 0x3F)];
        if (wc == 0x0000)
            return false;

        if (utf8_encode(&d,df,(uint32_t)wc) < 0) // will advance d by however many UTF-8 bytes are needed
            return false; // non-representable, or probably just out of room
    }

    if (d > df) return false;
    *d = 0;

    return true;
}

template <class MT> bool String_DBCS_TO_HOST_UTF16(uint16_t *d/*CROSS_LEN*/,const char *s/*CROSS_LEN*/,const MT *hitbl,const MT *rawtbl,const size_t rawtbl_max) {
    const uint16_t* df = d + CROSS_LEN - 1;
	const char *sf = s + CROSS_LEN - 1;

    while (*s != 0 && s < sf) {
        uint16_t ic = (unsigned char)(*s++);
        if ((dos.loaded_codepage==932 &&((ic & 0xE0) == 0x80 || (ic & 0xE0) == 0xE0)) && (ic & 0x80) == 0x80) {
            if (*s == 0) return false;
            ic <<= 8U;
            ic += (unsigned char)(*s++);
        }

        MT rawofs = hitbl[ic >> 6];
        if (rawofs == 0xFFFF)
            return false;

        if ((size_t)(rawofs+ (Bitu)0x40) > rawtbl_max)
            return false;
        MT wc = rawtbl[rawofs + (ic & 0x3F)];
        if (wc == 0x0000)
            return false;

        *d++ = (uint16_t)wc;
    }

    if (d > df) return false;
    *d = 0;

    return true;
}

#if defined(host_cnv_use_wchar)
char *CodePageHostToGuestUTF16(char *d, const uint16_t *s) {
    if (dos.loaded_codepage==932&&String_HOST_TO_DBCS_UTF16<uint16_t>(d,s,cp932_to_unicode_hitbl,cp932_to_unicode_raw,sizeof(cp932_to_unicode_raw)/sizeof(cp932_to_unicode_raw[0])))
        return (char*)cpcnv_temp;
    else if (String_HOST_TO_SBCS_UTF16<uint16_t>(d,s,cp437_to_unicode,sizeof(cp437_to_unicode)/sizeof(cp437_to_unicode[0])))
        return (char*)cpcnv_temp;
    return NULL;
}
#else
char *CodePageHostToGuestUTF8(char *d, const char *s) {
    if (dos.loaded_codepage==932&&String_HOST_TO_DBCS_UTF8<uint16_t>(d,s,cp932_to_unicode_hitbl,cp932_to_unicode_raw,sizeof(cp932_to_unicode_raw)/sizeof(cp932_to_unicode_raw[0])))
        return (char*)cpcnv_temp;
    else if (String_HOST_TO_SBCS_UTF8<uint16_t>(d,s,cp437_to_unicode,sizeof(cp437_to_unicode)/sizeof(cp437_to_unicode[0])))
        return (char*)cpcnv_temp;
    return NULL;
}
#endif

char *CodePageHostToGuest(const host_cnv_char_t *s) {
#if defined(host_cnv_use_wchar)
    if (!CodePageHostToGuestUTF16((char *)cpcnv_temp,(const uint16_t *)s))
#else
    if (!CodePageHostToGuestUTF8((char *)cpcnv_temp,(char *)s))
#endif
        return NULL;

    return (char*)cpcnv_temp;
}

char *CodePageHostToGuestL(const host_cnv_char_t *s) {
#if defined(host_cnv_use_wchar)
    if (!CodePageHostToGuestUTF16((char *)cpcnv_ltemp,(const uint16_t *)s))
#else
    if (!CodePageHostToGuestUTF8((char *)cpcnv_ltemp,(char *)s))
#endif
        return NULL;

    return (char*)cpcnv_ltemp;
}

#if defined(host_cnv_use_wchar)
wchar_t *CodePageGuestToHostUTF16(uint16_t *d, const char *s) {
    if (dos.loaded_codepage==932&&String_DBCS_TO_HOST_UTF16<uint16_t>(d,s,cp932_to_unicode_hitbl,cp932_to_unicode_raw,sizeof(cp932_to_unicode_raw)/sizeof(cp932_to_unicode_raw[0])))
        return cpcnv_temp;
    else if (String_SBCS_TO_HOST_UTF16<uint16_t>(d,s,cp437_to_unicode,sizeof(cp437_to_unicode)/sizeof(cp437_to_unicode[0])))
        return cpcnv_temp;
    return NULL;
}
#else
char *CodePageGuestToHostUTF8(char *d, const char *s) {
    if (dos.loaded_codepage==932&&String_DBCS_TO_HOST_UTF8<uint16_t>(d,s,cp932_to_unicode_hitbl,cp932_to_unicode_raw,sizeof(cp932_to_unicode_raw)/sizeof(cp932_to_unicode_raw[0])))
        return cpcnv_temp;
    else if (String_SBCS_TO_HOST_UTF8<uint16_t>(d,s,cp437_to_unicode,sizeof(cp437_to_unicode)/sizeof(cp437_to_unicode[0])))
        return cpcnv_temp;
    return NULL;
}
#endif

host_cnv_char_t *CodePageGuestToHost(const char *s) {
#if defined(host_cnv_use_wchar)
    if (!CodePageGuestToHostUTF16((uint16_t *)cpcnv_temp,s))
#else
    if (!CodePageGuestToHostUTF8((char *)cpcnv_temp,s))
#endif
        return NULL;

    return cpcnv_temp;
}

host_cnv_char_t *CodePageGuestToHostL(const char *s) {
#if defined(host_cnv_use_wchar)
    if (!CodePageGuestToHostUTF16((uint16_t *)cpcnv_ltemp,s))
#else
    if (!CodePageGuestToHostUTF8((char *)cpcnv_ltemp,s))
#endif
        return NULL;

    return cpcnv_ltemp;
}

bool localDrive::FileCreate(DOS_File * * file,char * name,Bit16u /*attributes*/) {
//TODO Maybe care for attributes but not likely
	char newname[CROSS_LEN];
	strcpy(newname,basedir);
	strcat(newname,name);
	CROSS_FILENAME(newname);
#if defined(LINUX) || defined(MACOSX)
	ChangeUtf8FileName(newname);
#endif
	char* temp_name = dirCache.GetExpandName(newname); //Can only be used in till a new drive_cache action is preformed */
	/* Test if file exists (so we need to truncate it). don't add to dirCache then */
	bool existing_file=false;
	
    const host_cnv_char_t* host_name = CodePageGuestToHost(temp_name);
    FILE * test;
#if defined(_MSC_VER) && (_MSC_VER >= 1900)
	if (host_name) test = _wfopen(host_name,L"rb+");
	else
#endif
	test=fopen(temp_name,"rb+");
	if(test) {
		fclose(test);
		existing_file=true;

	}
	
    FILE * hand;
#if defined(_MSC_VER) && (_MSC_VER >= 1900)
	if (host_name) hand = _wfopen(host_name,L"wb+");
	else
#endif
	hand=fopen(temp_name,"wb+");
	if (!hand){
		LOG_MSG("Warning: file creation failed: %s",newname);
		return false;
	}
   
	if(!existing_file) dirCache.AddEntry(newname, true);
	/* Make the 16 bit device information */
	*file=new localFile(name,hand);
	(*file)->flags=OPEN_READWRITE;

	return true;
}

bool localDrive::FileOpen(DOS_File * * file,char * name,Bit32u flags) {
	const char* type;
	const wchar_t* wtype;
	switch (flags&0xf) {
	case OPEN_READ:        type = "rb" ; wtype = L"rb" ;break;
	case OPEN_WRITE:       type = "rb+"; wtype = L"rb+"; break;
	case OPEN_READWRITE:   type = "rb+"; wtype = L"rb+"; break;
	case OPEN_READ_NO_MOD: type = "rb" ; wtype = L"rb"; break; //No modification of dates. LORD4.07 uses this
	default:
		DOS_SetError(DOSERR_ACCESS_CODE_INVALID);
		return false;
	}
	char newname[CROSS_LEN];
	strcpy(newname,basedir);
	strcat(newname,name);
	CROSS_FILENAME(newname);
#if defined(LINUX) || defined(MACOSX)
	ChangeUtf8FileName(newname);
#endif
	dirCache.ExpandName(newname);

    const host_cnv_char_t* host_name = CodePageGuestToHost(newname);

	//Flush the buffer of handles for the same file. (Betrayal in Antara)
	Bit8u i,drive=DOS_DRIVES;
	localFile *lfp;
	for (i=0;i<DOS_DRIVES;i++) {
		if (Drives[i]==this) {
			drive=i;
			break;
		}
	}
	for (i=0;i<DOS_FILES;i++) {
		if (Files[i] && Files[i]->IsOpen() && Files[i]->GetDrive()==drive && Files[i]->IsName(name)) {
			lfp=dynamic_cast<localFile*>(Files[i]);
			if (lfp) lfp->Flush();
		}
	}

	FILE * hand = NULL;
#if defined(_MSC_VER) && (_MSC_VER >= 1900)
	if (host_name) hand = _wfopen(host_name,wtype);
	else
#endif
	hand=fopen(newname,type);
//	Bit32u err=errno;
	if (!hand) { 
		if((flags&0xf) != OPEN_READ) {
			FILE * hmm=fopen(newname,"rb");
			if (hmm) {
				fclose(hmm);
				LOG_MSG("Warning: file %s exists and failed to open in write mode.\nPlease Remove write-protection",newname);
			}
		}
		return false;
	}

	*file=new localFile(name,hand);
	(*file)->flags=flags;  //for the inheritance flag and maybe check for others.
#if defined(WIN32)
	(*file)->SetFullPath(newname);
#endif
	return true;
}

FILE * localDrive::GetSystemFilePtr(char const * const name, char const * const type) {

	char newname[CROSS_LEN];
	strcpy(newname,basedir);
	strcat(newname,name);
	CROSS_FILENAME(newname);
#if defined(LINUX) || defined(MACOSX)
	ChangeUtf8FileName(newname);
#endif
	dirCache.ExpandName(newname);

	return fopen(newname,type);
}

bool localDrive::GetSystemFilename(char *sysName, char const * const dosName) {

	strcpy(sysName, basedir);
	strcat(sysName, dosName);
	CROSS_FILENAME(sysName);
#if defined(LINUX) || defined(MACOSX)
	ChangeUtf8FileName(sysName);
#endif
	dirCache.ExpandName(sysName);
	return true;
}

#if defined (WIN32)
#include <Shellapi.h>
#endif
bool localDrive::FileUnlink(char * name) {
	char newname[CROSS_LEN];
	strcpy(newname,basedir);
	strcat(newname,name);
	CROSS_FILENAME(newname);
#if defined(LINUX) || defined(MACOSX)
	ChangeUtf8FileName(newname);
#endif
	char *fullname = dirCache.GetExpandName(newname);
#if defined(_MSC_VER) && (_MSC_VER >= 1900)
	const wchar_t* host_name = CodePageGuestToHost(fullname);
	if (host_name == NULL && unlink(fullname) || host_name != NULL && _wunlink(host_name)) {
#else
	if (unlink(fullname)) {
#endif
		//Unlink failed for some reason try finding it.
#if defined (WIN32)
		if (uselfn&&strlen(fullname)>1&&!strcmp(fullname+strlen(fullname)-2,"\\*")||strlen(fullname)>3&&!strcmp(fullname+strlen(fullname)-4,"\\*.*"))
			{
			SHFILEOPSTRUCT op={0};
			op.wFunc = FO_DELETE;
			fullname[strlen(fullname)+1]=0;
			op.pFrom = fullname;
			op.pTo = NULL;
			op.fFlags = FOF_FILESONLY | FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI | 0x1000;
			int err=SHFileOperation(&op);
			if (err) DOS_SetError(err);
			return !err;
			}
#endif
		struct stat buffer;
		if(stat(fullname,&buffer)) {
			//file not found
			DOS_SetError(DOSERR_FILE_NOT_FOUND);
			return false;
		}

		//Do we have access?
		FILE* file_writable = fopen_wrap(fullname,"rb+");
		if(!file_writable) {
			DOS_SetError(DOSERR_ACCESS_DENIED);
			return false;
		}
		fclose(file_writable);

		//File exists and can technically be deleted, nevertheless it failed.
		//This means that the file is probably open by some process.
		//See if We have it open.
		bool found_file = false;
		for(Bitu i = 0;i < DOS_FILES;i++){
			if(Files[i] && Files[i]->IsName(name)) {
				Bitu max = DOS_FILES;
				while(Files[i]->IsOpen() && max--) {
					Files[i]->Close();
					if (Files[i]->RemoveRef()<=0) break;
				}
				found_file=true;
			}
		}
		if(!found_file) {
			DOS_SetError(DOSERR_ACCESS_DENIED);
			return false;
		}
		if (!unlink(fullname)) {
			dirCache.DeleteEntry(newname);
			return true;
		}
		DOS_SetError(DOSERR_ACCESS_DENIED);
		return false;
	} else {
		dirCache.DeleteEntry(newname);
		return true;
	}
}

bool localDrive::FindFirst(char * _dir,DOS_DTA & dta,bool fcb_findfirst) {
	char tempDir[CROSS_LEN];
	strcpy(tempDir,basedir);
	strcat(tempDir,_dir);
	CROSS_FILENAME(tempDir);

#if defined(LINUX) || defined(MACOSX)
	ChangeUtf8FileName(tempDir);
#endif
	size_t len = strlen(tempDir);
#if defined (WIN32)
	bool lead = false;
#endif
	for (unsigned int i=0;i<len;i++) {
#if defined (WIN32)
		if(lead) lead = false;
		else if(isKanji1(tempDir[i])) lead = true;
		else 
#endif
		tempDir[i]=toupper(tempDir[i]);
	}
	if (allocation.mediaid==0xF0 ) {
		EmptyCache(); //rescan floppie-content on each findfirst
	}
    
	if (!check_last_split_char(tempDir, len, CROSS_FILESPLIT)) {
		char end[2]={CROSS_FILESPLIT,0};
		strcat(tempDir,end);
	}

	Bit16u id;
	if (!dirCache.FindFirst(tempDir,id)) {
		DOS_SetError(DOSERR_PATH_NOT_FOUND);
		return false;
	}

	if (lfn_filefind_handle>=LFN_FILEFIND_MAX) {
		dta.SetDirID(id);
		strcpy(srchInfo[id].srch_dir,tempDir);
	} else {
		ldid[lfn_filefind_handle]=id;
		ldir[lfn_filefind_handle]=tempDir;
	}
	
	Bit8u sAttr;
	dta.GetSearchParams(sAttr,tempDir,false);

	if (this->isRemote() && this->isRemovable()) {
		// cdroms behave a bit different than regular drives
		if (sAttr == DOS_ATTR_VOLUME) {
			dta.SetResult(dirCache.GetLabel(),dirCache.GetLabel(),0,0,0,DOS_ATTR_VOLUME);
			return true;
		}
	} else {
		if (sAttr == DOS_ATTR_VOLUME) {
			if ( strcmp(dirCache.GetLabel(), "") == 0 ) {
//				LOG(LOG_DOSMISC,LOG_ERROR)("DRIVELABEL REQUESTED: none present, returned  NOLABEL");
//				dta.SetResult("NO_LABEL",0,0,0,DOS_ATTR_VOLUME);
//				return true;
				DOS_SetError(DOSERR_NO_MORE_FILES);
				return false;
			}
			dta.SetResult(dirCache.GetLabel(),dirCache.GetLabel(),0,0,0,DOS_ATTR_VOLUME);
			return true;
		} else if ((sAttr & DOS_ATTR_VOLUME)  && (*_dir == 0) && !fcb_findfirst) { 
		//should check for a valid leading directory instead of 0
		//exists==true if the volume label matches the searchmask and the path is valid
			if (WildFileCmp(dirCache.GetLabel(),tempDir)) {
				dta.SetResult(dirCache.GetLabel(),dirCache.GetLabel(),0,0,0,DOS_ATTR_VOLUME);
				return true;
			}
		}
	}
	return FindNext(dta);
}

bool localDrive::FindNext(DOS_DTA & dta) {

	char *dir_ent, *ldir_ent;
#if defined(_MSC_VER) && (_MSC_VER >= 1900)
	struct _stat64 stat_block64;
#else
	struct stat stat_block;
#endif
	char full_name[CROSS_LEN];
	char dir_entcopy[CROSS_LEN], ldir_entcopy[CROSS_LEN];

	Bit8u srch_attr;char srch_pattern[LFN_NAMELENGTH+1];
	Bit8u find_attr;

	dta.GetSearchParams(srch_attr,srch_pattern,false);
	uint16_t id = lfn_filefind_handle>=LFN_FILEFIND_MAX?dta.GetDirID():ldid[lfn_filefind_handle];

#if defined(LINUX) || defined(MACOSX)
	ChangeUtf8FileName(srch_pattern);
#endif

again:
	if (!dirCache.FindNext(id,dir_ent,ldir_ent)) {
		if (lfn_filefind_handle<LFN_FILEFIND_MAX) {
			ldid[lfn_filefind_handle]=0;
			ldir[lfn_filefind_handle]="";
		}
		DOS_SetError(DOSERR_NO_MORE_FILES);
		return false;
	}
	if(!WildFileCmp(dir_ent,srch_pattern)&&!LWildFileCmp(ldir_ent,srch_pattern)) goto again;

	strcpy(full_name,lfn_filefind_handle>=LFN_FILEFIND_MAX?srchInfo[id].srch_dir:(ldir[lfn_filefind_handle]!=""?ldir[lfn_filefind_handle].c_str():"\\"));
	strcat(full_name,dir_ent);
	
	//GetExpandName might indirectly destroy dir_ent (by caching in a new directory 
	//and due to its design dir_ent might be lost.)
	//Copying dir_ent first
#if defined(LINUX) || defined(MACOSX)
	utf8_to_sjis_copy(dir_entcopy,dir_ent, CROSS_LEN);
	utf8_to_sjis_copy(ldir_entcopy,ldir_ent, CROSS_LEN);
#else
	strcpy(dir_entcopy,dir_ent);
	strcpy(ldir_entcopy,ldir_ent);
#endif
	char *temp_name = dirCache.GetExpandName(full_name);
#if defined(_MSC_VER) && (_MSC_VER >= 1900)
	const wchar_t* host_name = CodePageGuestToHost(temp_name);
	if (host_name == NULL && _stat64(temp_name,&stat_block64)!=0 || host_name != NULL && _wstat64(host_name,&stat_block64)!=0) {
#else
	if (stat(temp_name,&stat_block)!=0) {
#endif
		goto again;//No symlinks and such
	}	

#if defined(_MSC_VER) && (_MSC_VER >= 1900)
	if(stat_block64.st_mode & S_IFDIR) find_attr=DOS_ATTR_DIRECTORY;
	else find_attr=0;
#else
	if(stat_block.st_mode & S_IFDIR) find_attr=DOS_ATTR_DIRECTORY;
	else find_attr=DOS_ATTR_ARCHIVE;
#endif
#if defined (WIN32)
	Bitu attribs = GetFileAttributesW(host_name);
	if (attribs != INVALID_FILE_ATTRIBUTES)
		find_attr|=attribs&0x3f;
#endif
 	if (~srch_attr & find_attr & (DOS_ATTR_DIRECTORY | DOS_ATTR_HIDDEN | DOS_ATTR_SYSTEM)) goto again;
	
	/*file is okay, setup everything to be copied in DTA Block */
	char find_name[DOS_NAMELENGTH_ASCII], lfind_name[LFN_NAMELENGTH+1];
	Bit16u find_date,find_time;Bit32u find_size;

	if(strlen(dir_entcopy)<DOS_NAMELENGTH_ASCII){
		strcpy(find_name,dir_entcopy);
		upcase(find_name);
	} 
	strcpy(lfind_name,ldir_entcopy);
	lfind_name[LFN_NAMELENGTH]=0;

#if defined(_MSC_VER) && (_MSC_VER >= 1900)
    find_size = (Bit32u)stat_block64.st_size;
#else
	find_size=(Bit32u) stat_block.st_size;
#endif
	struct tm *time;
#if defined(_MSC_VER) && (_MSC_VER >= 1900)
    time=localtime(&stat_block64.st_mtime);
#else
    time=localtime(&stat_block.st_mtime);
#endif
	if(time!=0){
		find_date=DOS_PackDate((Bit16u)(time->tm_year+1900),(Bit16u)(time->tm_mon+1),(Bit16u)time->tm_mday);
		find_time=DOS_PackTime((Bit16u)time->tm_hour,(Bit16u)time->tm_min,(Bit16u)time->tm_sec);
	} else {
		find_time=6; 
		find_date=4;
	}
	dta.SetResult(find_name,lfind_name,find_size,find_date,find_time,find_attr);
	return true;
}

bool localDrive::SetFileAttr(const char * name,uint16_t attr) {
	char newname[CROSS_LEN];
	strcpy(newname,basedir);
	strcat(newname,name);
	CROSS_FILENAME(newname);
#if defined(LINUX) || defined(MACOSX)
	ChangeUtf8FileName(newname);
#endif
	dirCache.ExpandName(newname);

#if defined (WIN32)
	const host_cnv_char_t* host_name = CodePageGuestToHost(newname);
	if (host_name == NULL) {
		LOG_MSG("%s: Filename '%s' from guest is non-representable on the host filesystem through code page conversion",__FUNCTION__,newname);
		DOS_SetError(DOSERR_FILE_NOT_FOUND);
		return false;
	}

	if (!SetFileAttributesW(host_name, attr)) {
		DOS_SetError((uint16_t)GetLastError());
		return false;
	}
	dirCache.EmptyCache();
	return true;
#else
	struct stat status;
	if (stat(newname,&status)==0) {
		if (attr & DOS_ATTR_READ_ONLY)
			status.st_mode &= ~(S_IWUSR|S_IWGRP|S_IWOTH);
		else
			status.st_mode |=  S_IWUSR;
		if (chmod(newname,status.st_mode) < 0) {
			DOS_SetError(DOSERR_ACCESS_DENIED);
			return false;
		}
		return true;
	}

	DOS_SetError(DOSERR_FILE_NOT_FOUND);
	return false;
#endif
}

bool localDrive::GetFileAttr(char * name,Bit16u * attr) {
	char newname[CROSS_LEN];
	strcpy(newname,basedir);
	strcat(newname,name);
	CROSS_FILENAME(newname);
#if defined(LINUX) || defined(MACOSX)
	ChangeUtf8FileName(newname);
#endif
	dirCache.ExpandName(newname);

#if defined (WIN32)
    const host_cnv_char_t* host_name = CodePageGuestToHost(newname);
    if (host_name == NULL) {
        LOG_MSG("%s: Filename '%s' from guest is non-representable on the host filesystem through code page conversion",__FUNCTION__,newname);
		DOS_SetError(DOSERR_FILE_NOT_FOUND);
        return false;
    }
	Bitu attribs = GetFileAttributesW(host_name);
	if (attribs == INVALID_FILE_ATTRIBUTES) {
		DOS_SetError((uint16_t)GetLastError());
		return false;
	}
	*attr = attribs&0x3f;
	return true;
#else
	struct stat status;
	if (stat(newname,&status)==0) {
		if(status.st_mode & S_IFDIR) *attr=DOS_ATTR_DIRECTORY;
		else *attr=DOS_ATTR_ARCHIVE;
		return true;
	}
	*attr=0;
	return false; 
#endif
}

bool localDrive::GetFileAttrEx(char* name, struct stat *status) {
	char newname[CROSS_LEN];
	strcpy(newname,basedir);
	strcat(newname,name);
	CROSS_FILENAME(newname);
#if defined(LINUX) || defined(MACOSX)
	ChangeUtf8FileName(newname);
#endif
	dirCache.ExpandName(newname);
#if defined(_MSC_VER) && (_MSC_VER >= 1900)
	struct _stat64 status64;
	const wchar_t* host_name = CodePageGuestToHost(newname);
	if (host_name == NULL) return !stat(newname,status);
	int flag = _wstat64(host_name,&status64);
	status->st_dev = status64.st_dev;
	status->st_ino = status64.st_ino;
	status->st_mode = status64.st_mode;
	status->st_nlink = status64.st_nlink;
	status->st_uid = status64.st_uid;
	status->st_gid = status64.st_gid;
	status->st_rdev = status64.st_rdev;
	status->st_size = (_off_t)status64.st_size;
	status->st_atime = (time_t)status64.st_atime;
	status->st_mtime = (time_t)status64.st_mtime;
	status->st_ctime = (time_t)status64.st_ctime;
    return !flag;
#else
	return !stat(newname,status);
#endif
}

DWORD localDrive::GetCompressedSize(char* name)
	{
#if !defined (WIN32)
	return 0;
#else
	char newname[CROSS_LEN];
	strcpy(newname,basedir);
	strcat(newname,name);
	CROSS_FILENAME(newname);
#if defined(LINUX) || defined(MACOSX)
	ChangeUtf8FileName(newname);
#endif
	dirCache.ExpandName(newname);
	DWORD size = GetCompressedFileSize(newname, NULL);
	if (size != INVALID_FILE_SIZE) {
		if (size != 0 && size == GetFileSize(newname, NULL)) {
			DWORD sectors_per_cluster, bytes_per_sector, free_clusters, total_clusters;
			if (GetDiskFreeSpace(newname, &sectors_per_cluster, &bytes_per_sector, &free_clusters, &total_clusters)) {
				size = ((size - 1) | (sectors_per_cluster * bytes_per_sector - 1)) + 1;
			}
		}
		return size;
	} else {
		DOS_SetError((Bit16u)GetLastError());
		return -1;
	}
#endif
}

HANDLE localDrive::CreateOpenFile(const char* name)
	{
	char newname[CROSS_LEN];
	strcpy(newname,basedir);
	strcat(newname,name);
	CROSS_FILENAME(newname);
#if defined(LINUX) || defined(MACOSX)
	ChangeUtf8FileName(newname);
#endif
	dirCache.ExpandName(newname);
#if defined (WIN32)
	HANDLE handle=CreateFile(newname, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (handle==INVALID_HANDLE_VALUE)
		DOS_SetError((Bit16u)GetLastError());
	return handle;
#else
	return INVALID_HANDLE_VALUE;
#endif
}

bool localDrive::MakeDir(char * dir) {
	char newdir[CROSS_LEN];
	strcpy(newdir,basedir);
	strcat(newdir,dir);
	CROSS_FILENAME(newdir);
#if defined(LINUX) || defined(MACOSX)
	ChangeUtf8FileName(newdir);
#endif
    std::string newstr = newdir;
    const char* temp_name = dirCache.GetExpandName(newdir);
    const host_cnv_char_t* host_name = CodePageGuestToHost(temp_name);
    int temp=0;
#if defined (WIN32)						/* MS Visual C++ */
#if defined(_MSC_VER) && (_MSC_VER >= 1900)
    if (host_name != NULL)
        temp=_wmkdir(host_name);
    else
#endif
	temp=mkdir(newdir);
#else
	temp=mkdir(newdir,0700);
#endif
	if (temp==0) dirCache.CacheOut(newstr.c_str(),true);

	return (temp==0);// || ((temp!=0) && (errno==EEXIST));
}

bool localDrive::RemoveDir(char * dir) {
	char newdir[CROSS_LEN];
	strcpy(newdir,basedir);
	strcat(newdir,dir);
	CROSS_FILENAME(newdir);
#if defined(LINUX) || defined(MACOSX)
	ChangeUtf8FileName(newdir);
#endif
    std::string newstr = newdir;
    const char* temp_name = dirCache.GetExpandName(newdir);
    const host_cnv_char_t* host_name = CodePageGuestToHost(temp_name);
    int temp=0;
#if defined(_MSC_VER) && (_MSC_VER >= 1900)
    if (host_name != NULL)
        temp=_wrmdir(host_name);
    else
#endif
        temp=rmdir(newdir);
	if (temp==0) dirCache.DeleteEntry(newstr.c_str(),true);
	return (temp==0);
}

bool localDrive::TestDir(char * dir) {
	char newdir[CROSS_LEN];
	strcpy(newdir,basedir);
	strcat(newdir,dir);
	CROSS_FILENAME(newdir);
#if defined(LINUX) || defined(MACOSX)
	ChangeUtf8FileName(newdir);
#endif
	dirCache.ExpandName(newdir);
	// Skip directory test, if "\"
	size_t len = strlen(newdir);
	const host_cnv_char_t* host_name = CodePageGuestToHost(newdir);
	if (len && !check_last_split_char(newdir, len, '\\')) {
		// It has to be a directory !
		struct stat test;
#if defined(_MSC_VER) && (_MSC_VER >= 1900)
        struct _stat64 test64;
        if (host_name != NULL) {
            if (_wstat64(host_name,&test64)) return false;
            if ((test64.st_mode & S_IFDIR)==0) return false;
        } else
#endif
        {
            if (stat(newdir,&test)) return false;
            if ((test.st_mode & S_IFDIR)==0) return false;
        }
	};
    int temp=0;
#if defined(_MSC_VER) && (_MSC_VER >= 1900)
    if (host_name != NULL) temp=_waccess(host_name,F_OK);
    else
#endif
	temp=access(newdir,F_OK);
	return (temp==0);
}

bool localDrive::Rename(char * oldname,char * newname) {
	char newold[CROSS_LEN];
	strcpy(newold,basedir);
	strcat(newold,oldname);
	CROSS_FILENAME(newold);
#if defined(LINUX) || defined(MACOSX)
	ChangeUtf8FileName(newold);
#endif
	dirCache.ExpandName(newold);
	char newnew[CROSS_LEN];
	strcpy(newnew,basedir);
	strcat(newnew,newname);
	CROSS_FILENAME(newnew);
#if defined(LINUX) || defined(MACOSX)
	ChangeUtf8FileName(newnew);
#endif
    std::string newstr = newnew;
    dirCache.GetExpandName(newnew);
	const host_cnv_char_t* host_oname = CodePageGuestToHost(newold);
	const host_cnv_char_t* host_nname = CodePageGuestToHostL(newnew);
	int temp=0;
#if defined(_MSC_VER) && (_MSC_VER >= 1900)
    if (host_oname != NULL && host_nname != NULL)
        temp=_wrename(host_oname,host_nname);
    else
#endif
    temp=rename(newold,newnew);
	if (temp==0) dirCache.CacheOut(newstr.c_str());
	return (temp==0);

}

bool localDrive::AllocationInfo(Bit16u * _bytes_sector,Bit8u * _sectors_cluster,Bit16u * _total_clusters,Bit16u * _free_clusters) {
	*_bytes_sector=allocation.bytes_sector;
	*_sectors_cluster=allocation.sectors_cluster;
	*_total_clusters=allocation.total_clusters;
	*_free_clusters=allocation.free_clusters;
	return true;
}

bool localDrive::FileExists(const char* name) {
	char newname[CROSS_LEN];
	strcpy(newname,basedir);
	strcat(newname,name);
	CROSS_FILENAME(newname);
	dirCache.ExpandName(newname);
#if defined(LINUX) || defined(MACOSX)
	ChangeUtf8FileName(newname);
#endif
	const host_cnv_char_t* host_name = CodePageGuestToHost(newname);
	struct stat temp_stat;
#if defined(_MSC_VER) && (_MSC_VER >= 1900)
    struct _stat64 temp_stat64;
    if (host_name != NULL) {
        if (_wstat64(host_name,&temp_stat64)!=0) return false;
        if (temp_stat64.st_mode & S_IFDIR) return false;
    } else
#endif
    {
        if(stat(newname,&temp_stat)!=0) return false;
        if(temp_stat.st_mode & S_IFDIR) return false;
    }
	return true;
}

bool localDrive::FileStat(const char* name, FileStat_Block * const stat_block) {
	char newname[CROSS_LEN];
	strcpy(newname,basedir);
	strcat(newname,name);
	CROSS_FILENAME(newname);
	dirCache.ExpandName(newname);
#if defined(LINUX) || defined(MACOSX)
	ChangeUtf8FileName(newname);
#endif
	struct stat temp_stat;
	if(stat(newname,&temp_stat)!=0) return false;
	/* Convert the stat to a FileStat */
	struct tm *time;
	if((time=localtime(&temp_stat.st_mtime))!=0) {
		stat_block->time=DOS_PackTime((Bit16u)time->tm_hour,(Bit16u)time->tm_min,(Bit16u)time->tm_sec);
		stat_block->date=DOS_PackDate((Bit16u)(time->tm_year+1900),(Bit16u)(time->tm_mon+1),(Bit16u)time->tm_mday);
	} else {

	}
	stat_block->size=(Bit32u)temp_stat.st_size;
	return true;
}


Bit8u localDrive::GetMediaByte(void) {
	return allocation.mediaid;
}

bool localDrive::isRemote(void) {
	return false;
}

bool localDrive::isRemovable(void) {
	return false;
}

bool localDrive::isWriteProtected(void) {
	return false;
}
Bits localDrive::UnMount(void) { 
	delete this;
	return 0; 
}

#if defined(PATH_MAX) && !defined(MAX_PATH)
#define MAX_PATH PATH_MAX
#endif

#ifndef WIN32
#define open_directoryw open_directory
#define read_directory_firstw read_directory_first
#define read_directory_nextw read_directory_next
#endif

dir_information* open_directoryw(const host_cnv_char_t* dirname);
void *localDrive::opendir(const char *name) {
    // guest to host code page translation
    const host_cnv_char_t* host_name = CodePageGuestToHost(name);
    if (host_name == NULL) {
        LOG_MSG("%s: Filename '%s' from guest is non-representable on the host filesystem through code page conversion",__FUNCTION__,name);
        return open_directory(name);
    }

	return open_directoryw(host_name);
}

bool read_directory_first(dir_information* dirp, char* entry_name, char* entry_sname, bool& is_directory);
bool read_directory_next(dir_information* dirp, char* entry_name, char* entry_sname, bool& is_directory);
bool read_directory_firstw(dir_information* dirp, host_cnv_char_t* entry_name, host_cnv_char_t* entry_sname, bool& is_directory);
bool read_directory_nextw(dir_information* dirp, host_cnv_char_t* entry_name, host_cnv_char_t* entry_sname, bool& is_directory);
bool localDrive::read_directory_first(void *handle, char* entry_name, char* entry_sname, bool& is_directory) {
    host_cnv_char_t tmp[MAX_PATH+1], stmp[MAX_PATH+1];

    if (::read_directory_firstw((dir_information*)handle, tmp, stmp, is_directory)) {
        // guest to host code page translation
        const char* n_stemp_name = CodePageHostToGuest(stmp);
        if (n_stemp_name == NULL) {
#ifdef host_cnv_use_wchar
            LOG_MSG("%s: Filename '%ls' from host is non-representable on the guest filesystem through code page conversion",__FUNCTION__,stmp);
#else
            LOG_MSG("%s: Filename '%s' from host is non-representable on the guest filesystem through code page conversion",__FUNCTION__,stmp);
#endif
            char ctmp[MAX_PATH+1], cstmp[MAX_PATH+1];
            return ::read_directory_first((dir_information*)handle, ctmp, cstmp, is_directory);
        }
		{
			const char* n_temp_name = CodePageHostToGuestL(tmp);
			if (n_temp_name == NULL) {
#ifdef host_cnv_use_wchar
				LOG_MSG("%s: Filename '%ls' from host is non-representable on the guest filesystem through code page conversion",__FUNCTION__,tmp);
#else
				LOG_MSG("%s: Filename '%s' from host is non-representable on the guest filesystem through code page conversion",__FUNCTION__,tmp);
#endif
				strcpy(entry_name,n_stemp_name);
			} else {
				strcpy(entry_name,n_temp_name);
			}
		}
		strcpy(entry_sname,n_stemp_name);
		return true;
    }

    return false;
}

bool localDrive::read_directory_next(void *handle, char* entry_name, char* entry_sname, bool& is_directory) {
    host_cnv_char_t tmp[MAX_PATH+1], stmp[MAX_PATH+1];

    if (::read_directory_nextw((dir_information*)handle, tmp, stmp, is_directory)) {
        // guest to host code page translation
        const char* n_stemp_name = CodePageHostToGuest(stmp);
        if (n_stemp_name == NULL) {
#ifdef host_cnv_use_wchar
            LOG_MSG("%s: Filename '%ls' from host is non-representable on the guest filesystem through code page conversion",__FUNCTION__,stmp);
#else
            LOG_MSG("%s: Filename '%s' from host is non-representable on the guest filesystem through code page conversion",__FUNCTION__,stmp);
#endif
            char ctmp[MAX_PATH+1], cstmp[MAX_PATH+1];
            return ::read_directory_next((dir_information*)handle, ctmp, cstmp, is_directory);

        }
		{
			const char* n_temp_name = CodePageHostToGuestL(tmp);
			if (n_temp_name == NULL) {
#ifdef host_cnv_use_wchar
				LOG_MSG("%s: Filename '%ls' from host is non-representable on the guest filesystem through code page conversion",__FUNCTION__,tmp);
#else
				LOG_MSG("%s: Filename '%s' from host is non-representable on the guest filesystem through code page conversion",__FUNCTION__,tmp);
#endif
				strcpy(entry_name,n_stemp_name);
			} else {
				strcpy(entry_name,n_temp_name);
			}
		}
        strcpy(entry_sname,n_stemp_name);
        return true;
    }

    return false;
}

localDrive::localDrive(const char * startdir,Bit16u _bytes_sector,Bit8u _sectors_cluster,Bit16u _total_clusters,Bit16u _free_clusters,Bit8u _mediaid) {
	strcpy(basedir,startdir);
	sprintf(info,"local directory %s",startdir);
	allocation.bytes_sector=_bytes_sector;
	allocation.sectors_cluster=_sectors_cluster;
	allocation.total_clusters=_total_clusters;
	allocation.free_clusters=_free_clusters;
	allocation.mediaid=_mediaid;

	dirCache.SetBaseDir(basedir,this);
}


//TODO Maybe use fflush, but that seemed to fuck up in visual c
bool localFile::Read(Bit8u * data,Bit16u * size) {
	if ((this->flags & 0xf) == OPEN_WRITE) {	// check if file opened in write-only mode
		DOS_SetError(DOSERR_ACCESS_DENIED);
		return false;
	}
	if (last_action==WRITE) fseek(fhandle,ftell(fhandle),SEEK_SET);
	last_action=READ;
	*size=(Bit16u)fread(data,1,*size,fhandle);
	/* Fake harddrive motion. Inspector Gadget with soundblaster compatible */
	/* Same for Igor */
	/* hardrive motion => unmask irq 2. Only do it when it's masked as unmasking is realitively heavy to emulate */
	Bit8u mask = IO_Read(0x21);
	if(mask & 0x4 ) IO_Write(0x21,mask&0xfb);
	return true;
}

bool localFile::Write(Bit8u * data,Bit16u * size) {
	Bit32u lastflags = this->flags & 0xf;
	if (lastflags == OPEN_READ || lastflags == OPEN_READ_NO_MOD) {	// check if file opened in read-only mode
		DOS_SetError(DOSERR_ACCESS_DENIED);
		return false;
	}
	if (last_action==READ) fseek(fhandle,ftell(fhandle),SEEK_SET);
	last_action=WRITE;
	if(*size==0){  
        return (!ftruncate(fileno(fhandle),(Bit32u)ftell(fhandle)));
    }
    else 
    {
		*size=(Bit16u)fwrite(data,1,*size,fhandle);
		return true;
    }
}

bool localFile::Seek(Bit32u * pos,Bit32u type) {
	int seektype;
	switch (type) {
	case DOS_SEEK_SET:seektype=SEEK_SET;break;
	case DOS_SEEK_CUR:seektype=SEEK_CUR;break;
	case DOS_SEEK_END:seektype=SEEK_END;break;
	default:
	//TODO Give some doserrorcode;
		return false;//ERROR
	}
	int ret=fseek(fhandle,*reinterpret_cast<Bit32s*>(pos),seektype);
	if (ret!=0) {
		// Out of file range, pretend everythings ok 
		// and move file pointer top end of file... ?! (Black Thorne)
		fseek(fhandle,0,SEEK_END);
	};
#if 0
	fpos_t temppos;
	fgetpos(fhandle,&temppos);
	Bit32u * fake_pos=(Bit32u*)&temppos;
	*pos=*fake_pos;
#endif
	*pos=(Bit32u)ftell(fhandle);
	last_action=NONE;
	return true;
}

bool localFile::SetDateTime(Bit16u ndate, Bit16u ntime)
{
#if defined(WIN32)
	{
		HANDLE h = (HANDLE)_get_osfhandle(fileno(fhandle));
		if(h != INVALID_HANDLE_VALUE) {
			FILETIME ft;
			if(DosDateTimeToFileTime(ndate, ntime, &ft)) {
				LocalFileTimeToFileTime(&ft, &filetime);
				if(!SetFileTime(h, NULL, NULL, &filetime)) {
					if(GetLastError() != ERROR_ACCESS_DENIED) {
						dos.errorcode = (Bit16u)GetLastError();
						return false;
					} else {
						newtime = true;
						return true;
					}
				}
			}
		}
	}
#elif defined(LINUX) || defined(MACOSX)
	struct tm t;
	t.tm_isdst = -1;
	t.tm_sec  = (((int)ntime) << 1) & 0x3e;
	t.tm_min  = (((int)ntime) >> 5) & 0x3f;
	t.tm_hour = (((int)ntime) >> 11) & 0x1f;
	t.tm_mday = (int)(ndate) & 0x1f;
	t.tm_mon  = ((int)(ndate >> 5) & 0x0f) - 1;
	t.tm_year = ((int)(ndate >> 9) & 0x7f) + 80;
	time_t ttime = mktime(&t);
	struct timespec tv[2];
	tv[0].tv_sec = ttime;
	tv[0].tv_nsec = 0;
	tv[1].tv_sec = ttime;
	tv[1].tv_nsec = 0;
	fflush(fhandle);
	if(futimens(fileno(fhandle), tv) < 0) {
		return false;
	}
#endif
	return true;
}

bool localFile::Close() {
	// only close if one reference left
	if (refCtr==1) {
		if(fhandle) fclose(fhandle);
		fhandle = 0;
		open = false;
	}
#if defined(WIN32)
	if(newtime && path) {
		const host_cnv_char_t *host_name = CodePageGuestToHost(path);
		if(host_name) {
			HANDLE h = CreateFileW(host_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if(h) {
				SetFileTime(h, NULL, NULL, &filetime);
				CloseHandle(h);
			}
		}
	}
	if(path) {
		free(path);
		path = NULL;
	}
#endif
	return true;
}

Bit16u localFile::GetInformation(void) {
	return read_only_medium?0x40:0;
}
	

localFile::localFile(const char* _name, FILE * handle) {
	fhandle=handle;
	open=true;
	UpdateDateTimeFromHost();

	attr=DOS_ATTR_ARCHIVE;
	last_action=NONE;
	read_only_medium=false;

	name=0;
	SetName(_name);
}

void localFile::FlagReadOnlyMedium(void) {
	read_only_medium = true;
}

bool localFile::UpdateDateTimeFromHost(void) {
	if(!open) return false;
	struct stat temp_stat;
	fstat(fileno(fhandle),&temp_stat);
	struct tm * ltime;
	if((ltime=localtime(&temp_stat.st_mtime))!=0) {
		time=DOS_PackTime((Bit16u)ltime->tm_hour,(Bit16u)ltime->tm_min,(Bit16u)ltime->tm_sec);
		date=DOS_PackDate((Bit16u)(ltime->tm_year+1900),(Bit16u)(ltime->tm_mon+1),(Bit16u)ltime->tm_mday);
	} else {
		time=1;date=1;
	}
	return true;
}

void localFile::Flush(void) {
	if (last_action==WRITE) {
		fseek(fhandle,ftell(fhandle),SEEK_SET);
		last_action=NONE;
	}
}


// ********************************************
// CDROM DRIVE
// ********************************************

int  MSCDEX_RemoveDrive(char driveLetter);
int  MSCDEX_AddDrive(char driveLetter, const char* physicalPath, Bit8u& subUnit);
bool MSCDEX_HasMediaChanged(Bit8u subUnit);
bool MSCDEX_GetVolumeName(Bit8u subUnit, char* name);


cdromDrive::cdromDrive(const char driveLetter, const char * startdir,Bit16u _bytes_sector,Bit8u _sectors_cluster,Bit16u _total_clusters,Bit16u _free_clusters,Bit8u _mediaid, int& error)
		   :localDrive(startdir,_bytes_sector,_sectors_cluster,_total_clusters,_free_clusters,_mediaid),
		    subUnit(0),
		    driveLetter('\0')
{
	// Init mscdex
	error = MSCDEX_AddDrive(driveLetter,startdir,subUnit);
	strcpy(info, "CDRom ");
	strcat(info, startdir);
	this->driveLetter = driveLetter;
	// Get Volume Label
	char name[32];
	if (MSCDEX_GetVolumeName(subUnit,name)) dirCache.SetLabel(name,true,true);
}

bool cdromDrive::FileOpen(DOS_File * * file,char * name,Bit32u flags) {
	if ((flags&0xf)==OPEN_READWRITE) {
		flags &= ~OPEN_READWRITE;
	} else if ((flags&0xf)==OPEN_WRITE) {
		DOS_SetError(DOSERR_ACCESS_DENIED);
		return false;
	}
	bool retcode = localDrive::FileOpen(file,name,flags);
	if(retcode) (dynamic_cast<localFile*>(*file))->FlagReadOnlyMedium();
	return retcode;
}

bool cdromDrive::FileCreate(DOS_File * * /*file*/,char * /*name*/,Bit16u /*attributes*/) {
	DOS_SetError(DOSERR_ACCESS_DENIED);
	return false;
}

bool cdromDrive::FileUnlink(char * /*name*/) {
	DOS_SetError(DOSERR_ACCESS_DENIED);
	return false;
}

bool cdromDrive::RemoveDir(char * /*dir*/) {
	DOS_SetError(DOSERR_ACCESS_DENIED);
	return false;
}

bool cdromDrive::MakeDir(char * /*dir*/) {
	DOS_SetError(DOSERR_ACCESS_DENIED);
	return false;
}

bool cdromDrive::Rename(char * /*oldname*/,char * /*newname*/) {
	DOS_SetError(DOSERR_ACCESS_DENIED);
	return false;
}

bool cdromDrive::GetFileAttr(char * name,Bit16u * attr) {
	bool result = localDrive::GetFileAttr(name,attr);
	if (result) *attr |= DOS_ATTR_READ_ONLY;
	return result;
}

bool cdromDrive::GetFileAttrEx(char* name, struct stat *status) {
	return localDrive::GetFileAttrEx(name,status);
}

DWORD cdromDrive::GetCompressedSize(char* name) {
	return localDrive::GetCompressedSize(name);
}

HANDLE cdromDrive::CreateOpenFile(const char* name) {
		return localDrive::CreateOpenFile(name);
}

bool cdromDrive::FindFirst(char * _dir,DOS_DTA & dta,bool /*fcb_findfirst*/) {
	// If media has changed, reInit drivecache.
	if (MSCDEX_HasMediaChanged(subUnit)) {
		dirCache.EmptyCache();
		// Get Volume Label
		char name[32];
		if (MSCDEX_GetVolumeName(subUnit,name)) dirCache.SetLabel(name,true,true);
	}
	return localDrive::FindFirst(_dir,dta);
}

void cdromDrive::SetDir(const char* path) {
	// If media has changed, reInit drivecache.
	if (MSCDEX_HasMediaChanged(subUnit)) {
		dirCache.EmptyCache();
		// Get Volume Label
		char name[32];
		if (MSCDEX_GetVolumeName(subUnit,name)) dirCache.SetLabel(name,true,true);
	}
	localDrive::SetDir(path);
}

bool cdromDrive::isRemote(void) {
	return true;
}

bool cdromDrive::isRemovable(void) {
	return true;
}

bool cdromDrive::isWriteProtected(void) {
	return false;
}

Bits cdromDrive::UnMount(void) {
	if(MSCDEX_RemoveDrive(driveLetter)) {
		delete this;
		return 0;
	}
	return 2;
}
