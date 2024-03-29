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


#ifndef DOSBOX_SUPPORT_H
#define DOSBOX_SUPPORT_H

#include <string.h>
#include <string>
#include <ctype.h>
#ifndef DOSBOX_DOSBOX_H
#include "dosbox.h"
#endif

#if defined (_MSC_VER)						/* MS Visual C++ */
#define	strcasecmp(a,b) stricmp(a,b)
#define strncasecmp(a,b,n) _strnicmp(a,b,n)
#endif

#define safe_strncpy(a,b,n) do { strncpy((a),(b),(n)-1); (a)[(n)-1] = 0; } while (0)

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

char *strchr_dbcs(char *str, char ch);
char *strrchr_dbcs(char *str, char ch);
char *strtok_dbcs(char *s, const char *d);
void strreplace(char * str,char o,char n);
void strreplace_dbcs(char * str,char o,char n);
char *ltrim(char *str);
char *rtrim(char *str);
char *trim(char * str);
char * upcase(char * str);
char * lowcase(char * str);
bool check_last_split_char(const char *name, size_t len, char split);

bool ScanCMDBool(char * cmd,char const * const check);
char * ScanCMDRemain(char * cmd);
char * StripWord(char *&cmd);
char * StripArg(char *&cmd);
bool IsDecWord(char * word);
bool IsHexWord(char * word);
Bits ConvDecWord(char * word);
Bits ConvHexWord(char * word);

void trim(std::string& str);
void upcase(std::string &str);
void lowcase(std::string &str);

#if defined(LINUX) || defined(MACOSX)
void utf8_to_sjis_copy(char *dst, const char *src, int len);
void sjis_to_utf8_copy(char *dst, const char *src, int len);
void sjis_to_utf16_copy(char *dst, const char *src, int len);
void ChangeUtf8FileName(char *fullname);
#endif

#endif
