 /*
 Copyright (c) 2016, akm
 All rights reserved.
 This content is under the MIT License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dosbox.h"
#include "control.h"
#include "support.h"
#include "jega.h"//for AX
#include "jfont.h"
using namespace std;

#define SBCS 0
#define DBCS 1
#define ID_LEN 6
#define NAME_LEN 8
#define SBCS16_LEN 256 * 16
#define SBCS19_LEN 256 * 19
#define DBCS16_LEN 65536 * 32
#define DBCS24_LEN 65536 * 72
#define SBCS24_LEN 256 * 48

Bit8u jfont_sbcs_19[SBCS19_LEN];//256 * 19( * 8)
Bit8u jfont_dbcs_16[DBCS16_LEN];//65536 * 16 * 2 (* 8)
Bit8u jfont_sbcs_16[SBCS16_LEN];//256 * 16( * 8)
Bit8u jfont_dbcs_24[DBCS24_LEN];//65536 * 24 * 3
Bit8u jfont_sbcs_24[SBCS24_LEN];//256 * 12 * 2
Bit8u jfont_cache_dbcs_16[65536];
Bit8u jfont_cache_dbcs_24[65536];

typedef struct {
    char id[ID_LEN];
    char name[NAME_LEN];
    unsigned char width;
    unsigned char height;
    unsigned char type;
} fontx_h;

typedef struct {
    Bit16u start;
	Bit16u end;
} fontxTbl;

Bit16u chrtosht(FILE *fp)
{
	Bit16u i, j;
	i = (Bit8u)getc(fp);
	j = (Bit8u)getc(fp) << 8;
	return(i | j);
}

Bitu getfontx2header(FILE *fp, fontx_h *header)
{
    fread(header->id, ID_LEN, 1, fp);
    if (strncmp(header->id, "FONTX2", ID_LEN) != 0) {
	return 1;
    }
    fread(header->name, NAME_LEN, 1, fp);
    header->width = (Bit8u)getc(fp);
    header->height = (Bit8u)getc(fp);
    header->type = (Bit8u)getc(fp);
    return 0;
}

void readfontxtbl(fontxTbl *table, Bitu size, FILE *fp)
{
    while (size > 0) {
	table->start = chrtosht(fp);
	table->end = chrtosht(fp);
	++table;
	--size;
    }
}

#if defined(LINUX)
#include <limits.h>
extern Bit8u int10_font_16[];
#endif

static bool LoadFontxFile(const char *fname, int height = 16) {
    fontx_h head;
    fontxTbl *table;
    Bitu code;
    Bit8u size;
	if (!fname) return false;
	if(*fname=='\0') return false;
	FILE * mfile=fopen(fname,"rb");
	if (!mfile) {
#if defined(LINUX)
		char *start = strrchr((char *)fname, '/');
		if(start != NULL) {
			char cname[PATH_MAX + 1];
			sprintf(cname, ".%s", start);
			mfile=fopen(cname, "rb");
			if (!mfile) {
				LOG_MSG("MSG: Can't open FONTX2 file: %s",fname);
				return false;
			}
		}
#else
		LOG_MSG("MSG: Can't open FONTX2 file: %s",fname);
		return false;
#endif
	}
	if (getfontx2header(mfile, &head) != 0) {
		fclose(mfile);
		LOG_MSG("MSG: FONTX2 header is incorrect\n");
		return false;
    }
	// switch whether the font is DBCS or not
	if (head.type == DBCS) {
		if (head.width == 16 && head.height == 16) {
			size = getc(mfile);
			table = (fontxTbl *)calloc(size, sizeof(fontxTbl));
			readfontxtbl(table, size, mfile);
			for (Bitu i = 0; i < size; i++) {
				for (code = table[i].start; code <= table[i].end; code++) {
					fread(&jfont_dbcs_16[code * 32], sizeof(Bit8u), 32, mfile);
					jfont_cache_dbcs_16[code] = 1;
				}
			}
		}
		else if (head.width == 24 && head.height == 24) {
			size = getc(mfile);
			table = (fontxTbl *)calloc(size, sizeof(fontxTbl));
			readfontxtbl(table, size, mfile);
			for (Bitu i = 0; i < size; i++) {
				for (code = table[i].start ; code <= table[i].end ; code++) {
					fread(&jfont_dbcs_24[code * 72], sizeof(Bit8u), 72, mfile);
					jfont_cache_dbcs_24[code] = 1;
				}
			}
		}
		else {
			fclose(mfile);
			LOG_MSG("MSG: FONTX2 DBCS font size is not correct\n");
			return false;
		}
	}
    else {
		if (head.width == 8 && head.height == 19 && height == 19) {
			fread(jfont_sbcs_19, sizeof(Bit8u), SBCS19_LEN, mfile);
		} else if (head.width == 8 && head.height == 16) {
			if(height == 16) {
				for(int i = 0 ; i < 256 ; i++) {
					fread(&jfont_sbcs_16[i * 16], sizeof(Bit8u), 16, mfile);
				}
			} else if(height == 19) {
				for(int i = 0 ; i < 256 ; i++) {
					fread(&jfont_sbcs_19[i * 19 + 1], sizeof(Bit8u), 16, mfile);
				}
			}
		} else if (head.width == 12 && head.height == 24) {
			fread(jfont_sbcs_24, sizeof(Bit8u), SBCS24_LEN, mfile);
		}
		else {
			fclose(mfile);
			LOG_MSG("MSG: FONTX2 SBCS font size is not correct\n");
			return false;
		}
    }
	fclose(mfile);
	return true;
}

static bool CheckEmptyData(Bit8u *data, Bitu length)
{
	while(length > 0) {
		if(*data++ != 0) {
			return false;
		}
		length--;
	}
	return true;
}


void JFONT_Init(Section_prop * section) {
	std::string file_name;
	std::string font_name;
	bool yen_flag = section->Get_bool("yen");

	font_name = section->Get_string("jfontname");
#if defined(WIN32)
	SetFontUse20(section->Get_bool("jfontuse20"));
#endif
	SetFontName(font_name.c_str());
	Prop_path* pathprop = section->Get_path("jfontsbcs");
	if (pathprop) {
		if(!LoadFontxFile(pathprop->realpath.c_str(), 19)) {
			if(!MakeSbcs19Font()) {
				LOG_MSG("MSG: SBCS 8x19 font file path is not specified.\n");
#if defined(LINUX)
				for(Bitu ct = 0 ; ct < 0x100 ; ct++) {
					memcpy(&jfont_sbcs_19[ct * 19 + 1], &int10_font_16[ct * 16], 16);
				}
#endif
			}
		} else if(yen_flag) {
			if(!CheckEmptyData(&jfont_sbcs_19[0x7f * 19], 19)) {
				memcpy(&jfont_sbcs_19[0x5c * 19], &jfont_sbcs_19[0x7f * 19], 19);
			}
		}
	} else {
		if(!MakeSbcs19Font()) {
			LOG_MSG("MSG: SBCS 8x19 font file path is not specified.\n");
		}
	}
	pathprop = section->Get_path("jfontdbcs");
	if(pathprop) {
		LoadFontxFile(pathprop->realpath.c_str());
	}
	if(IS_J3_ARCH || IS_DOSV) {
		pathprop = section->Get_path("jfontsbcs16");
		if(pathprop) {
			if(!LoadFontxFile(pathprop->realpath.c_str())) {
				if(!MakeSbcs16Font()) {
					LOG_MSG("MSG: SBCS 8x16 font file path is not specified.\n");
#if defined(LINUX)
					memcpy(jfont_sbcs_16, int10_font_16, 256 * 16);
#endif
				}
			} else if(yen_flag) {
				if(!CheckEmptyData(&jfont_sbcs_16[0x7f * 16], 16)) {
					memcpy(&jfont_sbcs_16[0x5c * 16], &jfont_sbcs_16[0x7f * 16], 16);
				}
			}
		} else {
			if(!MakeSbcs16Font()) {
				LOG_MSG("MSG: SBCS 8x16 font file path is not specified.\n");
			}
		}
		pathprop = section->Get_path("jfontdbcs24");
		if(pathprop) {
			LoadFontxFile(pathprop->realpath.c_str());
		}
		pathprop = section->Get_path("jfontsbcs24");
		if(pathprop) {
			if(!LoadFontxFile(pathprop->realpath.c_str())) {
				if(!MakeSbcs24Font()) {
					LOG_MSG("MSG: SBCS 12x24 font file path is not specified.\n");
				}
			} else if(yen_flag) {
				if(!CheckEmptyData(&jfont_sbcs_24[0x7f * 2 * 24], 2 * 24)) {
					memcpy(&jfont_sbcs_24[0x5c * 2 * 24], &jfont_sbcs_24[0x7f * 2 * 24], 2 * 24);
				}
			}
		} else {
			if(!MakeSbcs24Font()) {
				LOG_MSG("MSG: SBCS 12x24 font file path is not specified.\n");
			}
		}
	}
}

