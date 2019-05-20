
#ifndef DOSBOX_JFONT_H
#define DOSBOX_JFONT_H

#include "setup.h"

#define	VIRTUAL_TEXT_SIZE		0x500

void InitFontHandle();
void QuitFont();
bool GetWindowsFont(Bitu code, Bit8u *buff, int width, int height);
Bit16u GetTextSeg();
void SetTextSeg();
bool MakeSbcs19Font();
bool MakeSbcs16Font();
bool MakeSbcs24Font();
Bit8u GetKanjiAttr(Bitu x, Bitu y);
Bit8u GetKanjiAttr();
Bit8u *GetSbcsFont(Bitu code);
Bit8u *GetSbcs19Font(Bitu code);
Bit8u *GetSbcs24Font(Bitu code);
void SetFontName(const char *name);
void GetDbcsFrameFont(Bitu code, Bit8u *buff);
Bit8u *GetDbcsFont(Bitu code);
Bit8u *GetDbcs24Font(Bitu code);
bool CheckStayVz();
bool CheckAnotherDisplayDriver();
Bit16u GetGaijiSeg();
void SetGaijiConfig(Section_prop *section);
bool SetGaijiData(Bit16u code, PhysPt data);
bool SetGaijiData24(Bit16u code, PhysPt data);

#ifdef NDEBUG
#define JTrace
#else
void JTrace(const char *form , ...);
#endif

#endif
