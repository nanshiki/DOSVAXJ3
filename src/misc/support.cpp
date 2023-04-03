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
 *  Wengier: LFN and LPT support
 */


#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <cctype>
#include <string>
#include "dosbox.h"
#include "debug.h"
#include "support.h"
#include "video.h"
#include "cross.h"
#include "dos_inc.h"
#include "jega.h"


void upcase(std::string &str) {
	int (*tf)(int) = std::toupper;
	std::transform(str.begin(), str.end(), str.begin(), tf);
}

void lowcase(std::string &str) {
	int (*tf)(int) = std::tolower;
	std::transform(str.begin(), str.end(), str.begin(), tf);
}

void trim(std::string &str) {
	std::string::size_type loc = str.find_first_not_of(" \r\t\f\n");
	if (loc != std::string::npos) str.erase(0,loc);
	loc = str.find_last_not_of(" \r\t\f\n");
	if (loc != std::string::npos) str.erase(loc+1);
}

char *strchr_dbcs(char *str, char ch) {
    bool lead = false;
    int lastpos = -1;
    if ((IS_DOSV || IS_J3_ARCH) && (ch == '\\' || ch == '|')) {
        for (size_t i=0; i<strlen(str); i++) {
            if (lead) lead = false;
            else if (isKanji1(str[i])) lead = true;
            else if (str[i] == ch) {lastpos = (int)i;break;}
        }
        return lastpos>-1 ? str + lastpos : NULL;
    } else
        return strchr(str, ch);
}

char *strrchr_dbcs(char *str, char ch) {
    bool lead = false;
    int lastpos = -1;
    if ((IS_DOSV || IS_J3_ARCH) && (ch == '\\' || ch == '|')) {
        for (size_t i=0; i<strlen(str); i++) {
            if (lead) lead = false;
            else if (isKanji1(str[i])) lead = true;
            else if (str[i] == ch) lastpos = (int)i;
        }
        return lastpos>-1 ? str + lastpos : NULL;
    } else
        return strrchr(str, ch);
}

char *strtok_dbcs(char *s, const char *d) {
    if (!IS_DOSV && !IS_J3_ARCH) return strtok(s, d);
    static char* input = NULL;
    if (s != NULL) input = s;
    if (input == NULL) return NULL;
    char* result = new char[strlen(input) + 1];
    int i = 0;
    bool lead = false;
    for (; input[i] != '\0'; i++) {
        if (!lead && isKanji1(input[i])) {
            result[i] = input[i];
            lead = true;
        } else if (input[i] != d[0] || lead) {
            result[i] = input[i];
            lead = false;
        } else {
            result[i] = '\0';
            input = input + i + 1;
            return result;
        }
    }
    result[i] = '\0';
    input = NULL;
    return result;
}

bool check_last_split_char(const char *name, size_t len, char split)
{
	bool tail = false;
	if(split == '\\') {
		bool lead = false;
		for(size_t pos = 0 ; pos < len ; pos++) {
			if(lead) lead = false;
			else if(isKanji1(name[pos])) lead = true;
			else if(pos == len - 1 && name[pos] == split) tail = true;
		}
	} else if(len > 0) {
		if(name[len - 1] == split) tail = true;
	}
	return tail;
}

/* 
	Ripped some source from freedos for this one.

*/


/*
 * replaces all instances of character o with character c
 */


void strreplace_dbcs(char * str,char o,char n) {
	bool lead = false;
	while (*str) {
		if(lead) lead = false;
		else if(isKanji1(*str)) lead = true;
		else if (*str==o) *str=n;
		str++;
	}
}

void strreplace(char * str,char o,char n) {
	while (*str) {
		if (*str==o) *str=n;
		str++;
	}
}

char *ltrim(char *str) { 
	while (*str && isspace(*reinterpret_cast<unsigned char*>(str))) str++;
	return str;
}

char *rtrim(char *str) {
	char *p;
	p = strchr(str, '\0');
	while (--p >= str && *reinterpret_cast<unsigned char*>(p) != '\f' && isspace(*reinterpret_cast<unsigned char*>(p))) {};
	p[1] = '\0';
	return str;
}

char *trim(char *str) {
	return ltrim(rtrim(str));
}

char * upcase(char * str) {
    //for (char* idx = str; *idx ; idx++) *idx = toupper(*reinterpret_cast<unsigned char*>(idx));
	bool flag = false;
	Bit8u *pt = (Bit8u *)str;
	while(*pt != '\0') {
		if(flag) {
			flag = false;
		} else {
			if(isKanji1(*pt)) {
				flag = true;
			} else {
				*pt = toupper(*pt);
			}
		}
		pt++;
	}
    return str;
}

char * lowcase(char * str) {
	//for(char* idx = str; *idx ; idx++)  *idx = tolower(*reinterpret_cast<unsigned char*>(idx));
	bool flag = false;
	Bit8u *pt = (Bit8u *)str;
	while(*pt != '\0') {
		if(flag) {
			flag = false;
		} else {
			if(isKanji1(*pt)) {
				flag = true;
			} else {
				*pt = tolower(*pt);
			}
		}
		pt++;
	}
	return str;
}



bool ScanCMDBool(char * cmd,char const * const check) {
	char * scan=cmd;size_t c_len=strlen(check);
	while ((scan=strchr(scan,'/'))) {
		/* found a / now see behind it */
		scan++;
		if (strncasecmp(scan,check,c_len)==0 && (scan[c_len]==' ' || scan[c_len]=='\t' || scan[c_len]=='/' || scan[c_len]==0)) {
		/* Found a math now remove it from the string */
			memmove(scan-1,scan+c_len,strlen(scan+c_len)+1);
			trim(scan-1);
			return true;
		}
	}
	return false;
}

/* This scans the command line for a remaining switch and reports it else returns 0*/
char * ScanCMDRemain(char * cmd) {
	char * scan,*found;;
	if ((scan=found=strchr(cmd,'/'))) {
		while ( *scan && !isspace(*reinterpret_cast<unsigned char*>(scan)) ) scan++;
		*scan=0;
		return found;
	} else return 0; 
}

char * StripWord(char *&line) {
	char * scan=line;
	scan=ltrim(scan);
	if (*scan=='"') {
		char * end_quote=strchr(scan+1,'"');
		if (end_quote) {
			*end_quote=0;
			line=ltrim(++end_quote);
			return (scan+1);
		}
	}
	char * begin=scan;
	for (char c = *scan ;(c = *scan);scan++) {
		if (isspace(*reinterpret_cast<unsigned char*>(&c))) {
			*scan++=0;
			break;
		}
	}
	line=scan;
	return begin;
}

char * StripArg(char *&line) {
	char * scan=line;
	int q=0;
	scan=ltrim(scan);
	char * begin=scan;
	for (char c = *scan ;(c = *scan);scan++) {
		if (*scan=='"') {
			q++;
		} else if (q/2*2==q && isspace(*reinterpret_cast<unsigned char*>(&c))) {
			*scan++=0;
			break;
		}
	}
	line=scan;
	return begin;
}

Bits ConvDecWord(char * word) {
	bool negative=false;Bitu ret=0;
	if (*word=='-') {
		negative=true;
		word++;
	}
	while (char c=*word) {
		ret*=10;
		ret+=c-'0';
		word++;
	}
	if (negative) return 0-ret;
	else return ret;
}

Bits ConvHexWord(char * word) {
	Bitu ret=0;
	while (char c=toupper(*reinterpret_cast<unsigned char*>(word))) {
		ret*=16;
		if (c>='0' && c<='9') ret+=c-'0';
		else if (c>='A' && c<='F') ret+=10+(c-'A');
		word++;
	}
	return ret;
}

double ConvDblWord(char * word) {
	return 0.0f;
}


static char buf[1024];           //greater scope as else it doesn't always gets thrown right (linux/gcc2.95)
void E_Exit(const char * format,...) {
#if C_DEBUG && C_HEAVY_DEBUG
 	DEBUG_HeavyWriteLogInstruction();
#endif
	va_list msg;
	va_start(msg,format);
	vsnprintf(buf,sizeof(buf),format,msg);
	va_end(msg);

	buf[sizeof(buf) - 1] = '\0';
	//strcat(buf,"\n"); catcher should handle the end of line.. 

	throw(buf);
}

#if defined(LINUX) || defined(MACOSX)
#include <iconv.h>

void utf8_to_sjis_copy(char *dst, const char *src, int len)
{
	iconv_t ic;
	char *psrc, *pdst;
	size_t srcsize, dstsize;
	ic = iconv_open("CP932", "UTF-8");
	psrc = (char *)src;
	pdst = dst;
	srcsize = strlen(src);
	dstsize = len;
	iconv(ic, &psrc, &srcsize, &pdst, &dstsize);
	iconv_close(ic);
	*pdst = 0;
}

void sjis_to_utf8_copy(char *dst, const char *src, int len)
{
	iconv_t ic;
	char *psrc, *pdst;
	size_t srcsize, dstsize;
	ic = iconv_open("UTF-8", "CP932");
	psrc = (char *)src;
	pdst = dst;
	srcsize = strlen(src);
	dstsize = len;
	iconv(ic, &psrc, &srcsize, &pdst, &dstsize);
	iconv_close(ic);
	*pdst = 0;
}

void sjis_to_utf16_copy(char *dst, const char *src, int len)
{
	iconv_t ic;
	char *psrc, *pdst;
	size_t srcsize, dstsize;
	ic = iconv_open("UTF-16LE", "CP932");
	psrc = (char *)src;
	pdst = dst;
	srcsize = strlen(src);
	dstsize = len;
	iconv(ic, &psrc, &srcsize, &pdst, &dstsize);
	iconv_close(ic);
	*pdst = 0;
}

void ChangeUtf8FileName(char *fullname)
{
	char *dst, *src;
	char temp[CROSS_LEN];
	sjis_to_utf8_copy(temp, fullname, CROSS_LEN);
	src = temp;
	dst = fullname;
	while(*src != '\0') {
		if(*src == (char)0xe2 && *(src + 1) == (char)0x80 && *(src + 2) == (char)0xbe) {
			*dst = '~';
			src += 2;
		} else if(*src == (char)0xc2 && *(src + 1) == (char)0xa5) {
			*dst = '/';
			src++;
		} else {
			*dst = *src;
		}
		src++;
		dst++;
	}
	*dst = 0;
}

#endif


