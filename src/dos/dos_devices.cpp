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
 *  Wengier: LPT support
 */


#include <string.h>
#include "dosbox.h"
#include "callback.h"
#include "regs.h"
#include "mem.h"
#include "bios.h"
#include "dos_inc.h"
#include "setup.h"
#include "support.h"
#include "control.h"
#include "drives.h" //Wildcmp
/* Include all the devices */

#include "dev_con.h"

DOS_Device * Devices[DOS_DEVICES];

bool DOS_ExtDevice::CheckSameDevice(Bit16u seg, Bit16u s_off, Bit16u i_off) {
	if(seg == ext.segment && s_off == ext.strategy && i_off == ext.interrupt) {
		return true;
	}
	return false;
}

Bit16u DOS_ExtDevice::CallDeviceFunction(Bit8u command, Bit8u length, Bit16u seg, Bit16u offset, Bit16u size) {
	Bit16u oldbx = reg_bx;
	Bit16u oldes = SegValue(es);

	real_writeb(dos.tables.dcp, 0, length);
	real_writeb(dos.tables.dcp, 1, 0);
	real_writeb(dos.tables.dcp, 2, command);
	real_writew(dos.tables.dcp, 3, 0);
	real_writed(dos.tables.dcp, 5, 0);
	real_writed(dos.tables.dcp, 9, 0);
	real_writeb(dos.tables.dcp, 13, 0);
	real_writew(dos.tables.dcp, 14, offset);
	real_writew(dos.tables.dcp, 16, seg);
	real_writew(dos.tables.dcp, 18, size);

	reg_bx = 0;
	SegSet16(es, dos.tables.dcp);
	CALLBACK_RunRealFar(ext.segment, ext.strategy);
	CALLBACK_RunRealFar(ext.segment, ext.interrupt);
	reg_bx = oldbx;
	SegSet16(es, oldes);

	return real_readw(dos.tables.dcp, 3);
}

bool DOS_ExtDevice::ReadFromControlChannel(PhysPt bufptr,Bit16u size,Bit16u * retcode) {
	if(ext.attribute & 0x4000) {
		// IOCTL INPUT
		if((CallDeviceFunction(3, 26, (Bit16u)(bufptr >> 4), (Bit16u)(bufptr & 0x000f), size) & 0x8000) == 0) {
			*retcode = real_readw(dos.tables.dcp, 18);
			return true;
		}
	}
	return false;
}

bool DOS_ExtDevice::WriteToControlChannel(PhysPt bufptr,Bit16u size,Bit16u * retcode) { 
	if(ext.attribute & 0x4000) {
		// IOCTL OUTPUT
		if((CallDeviceFunction(12, 26, (Bit16u)(bufptr >> 4), (Bit16u)(bufptr & 0x000f), size) & 0x8000) == 0) {
			*retcode = real_readw(dos.tables.dcp, 18);
			return true;
		}
	}
	return false;
}

bool DOS_ExtDevice::Read(Bit8u * data,Bit16u * size) {
	PhysPt bufptr = (dos.tables.dcp + 2) << 4;
	for(Bit16u no = 0 ; no < *size ; no++) {
		// INPUT
		if((CallDeviceFunction(4, 26, dos.tables.dcp + 2, 0 , 1) & 0x8000)) {
			return false;
		} else {
			if(real_readw(dos.tables.dcp, 18) != 1) {
				return false;
			}
			*data++ = mem_readb(bufptr);
		}
	}
	return true;
}

bool DOS_ExtDevice::Write(Bit8u * data,Bit16u * size) {
	PhysPt bufptr = (dos.tables.dcp + 2) << 4;
	for(Bit16u no = 0 ; no < *size ; no++) {
		mem_writeb(bufptr, *data);
		// OUTPUT
		if((CallDeviceFunction(8, 26, dos.tables.dcp + 2, 0, 1) & 0x8000)) {
			return false;
		} else {
			if(real_readw(dos.tables.dcp, 18) != 1) {
				return false;
			}
		}
		data++;
	}
	return true;
}

bool DOS_ExtDevice::Close() {
	return true;
}

bool DOS_ExtDevice::Seek(Bit32u * pos,Bit32u type) {
	return true;
}

Bit16u DOS_ExtDevice::GetInformation(void) { 
	// bit9=1 .. ExtDevice
	return (ext.attribute & 0xc07f) | 0x0080 | EXT_DEVICE_BIT;
}

Bit8u DOS_ExtDevice::GetStatus(bool input_flag) {
	Bit16u status;
	if(input_flag) {
		// NON-DESTRUCTIVE INPUT NO WAIT
		status = CallDeviceFunction(5, 14, 0, 0, 0);
	} else {
		// OUTPUT STATUS
		status = CallDeviceFunction(10, 13, 0, 0, 0);
	}
	// check NO ERROR & BUSY
	if((status & 0x8200) == 0) {
		return 0xff;
	}
	return 0x00;
}

Bit32u DOS_CheckExtDevice(const char *name, bool already_flag) {
	Bit32u addr = dos_infoblock.GetDeviceChain();
	Bit16u seg, off;
	Bit16u next_seg, next_off;
	Bit16u no;
	char devname[8 + 1];

	seg = addr >> 16;
	off = addr & 0xffff;
	while(1) {
		no = real_readw(seg, off + 4);
		next_seg = real_readw(seg, off + 2);
		next_off = real_readw(seg, off);
		if(next_seg == 0xffff && next_off == 0xffff) {
			break;
		}
		if(no & 0x8000) {
			for(no = 0 ; no < 8 ; no++) {
				if((devname[no] = real_readb(seg, off + 10 + no)) <= 0x20) {
					devname[no] = 0;
					break;
				}
			}
			devname[8] = 0;
			if(!strcmp(name, devname)) {
				if(already_flag) {
					for(no = 0 ; no < DOS_DEVICES ; no++) {
						if(Devices[no]) {
							if(Devices[no]->GetInformation() & EXT_DEVICE_BIT) {
								if(((DOS_ExtDevice *)Devices[no])->CheckSameDevice(seg, real_readw(seg, off + 6), real_readw(seg, off + 8))) {
									return 0;
								}
							}
						}
					}
				}
				return (Bit32u)seg << 16 | (Bit32u)off;
			}
		}
		seg = next_seg;
		off = next_off;
	}
	return 0;
}

static void DOS_CheckOpenExtDevice(const char *name) {
	Bit32u addr;

	if((addr = DOS_CheckExtDevice(name, true)) != 0) {
		DOS_ExtDevice *device = new DOS_ExtDevice(name, addr >> 16, addr & 0xffff);
		DOS_AddDevice(device);
	}
}

class device_NUL : public DOS_Device {
public:
	device_NUL() { SetName("NUL"); };
	virtual bool Read(Bit8u * /*data*/,Bit16u * size) {
		*size = 0; //Return success and no data read. 
		LOG(LOG_IOCTL,LOG_NORMAL)("%s:READ",GetName());
		return true;
	}
	virtual bool Write(Bit8u * /*data*/,Bit16u * /*size*/) {
		LOG(LOG_IOCTL,LOG_NORMAL)("%s:WRITE",GetName());
		return true;
	}
	virtual bool Seek(Bit32u * /*pos*/,Bit32u /*type*/) {
		LOG(LOG_IOCTL,LOG_NORMAL)("%s:SEEK",GetName());
		return true;
	}
	virtual bool Close() { return true; }
	virtual Bit16u GetInformation(void) { return 0x8084; }
	virtual bool ReadFromControlChannel(PhysPt /*bufptr*/,Bit16u /*size*/,Bit16u * /*retcode*/){return false;}
	virtual bool WriteToControlChannel(PhysPt /*bufptr*/,Bit16u /*size*/,Bit16u * /*retcode*/){return false;}
};

class device_LPT1 : public device_NUL {
public:
   	device_LPT1() { SetName("LPT1");}
	Bit16u GetInformation(void) { return 0x80A0; }
	bool Read(Bit8u* /*data*/,Bit16u * /*size*/){
		DOS_SetError(DOSERR_ACCESS_DENIED);
		return false;
	}	
};

bool DOS_Device::Read(Bit8u * data,Bit16u * size) {
	return Devices[devnum]->Read(data,size);
}

bool DOS_Device::Write(Bit8u * data,Bit16u * size) {
	return Devices[devnum]->Write(data,size);
}

bool DOS_Device::Seek(Bit32u * pos,Bit32u type) {
	return Devices[devnum]->Seek(pos,type);
}

bool DOS_Device::Close() {
	return Devices[devnum]->Close();
}

Bit16u DOS_Device::GetInformation(void) { 
	return Devices[devnum]->GetInformation();
}

bool DOS_Device::ReadFromControlChannel(PhysPt bufptr,Bit16u size,Bit16u * retcode) { 
	return Devices[devnum]->ReadFromControlChannel(bufptr,size,retcode);
}

bool DOS_Device::WriteToControlChannel(PhysPt bufptr,Bit16u size,Bit16u * retcode) { 
	return Devices[devnum]->WriteToControlChannel(bufptr,size,retcode);
}

Bit8u DOS_Device::GetStatus(bool input_flag) {
	Bit16u info = Devices[devnum]->GetInformation();
	if(info & EXT_DEVICE_BIT) {
		return Devices[devnum]->GetStatus(input_flag);
	}
	return (info & 0x40) ? 0x00 : 0xff;
}

DOS_File::DOS_File(const DOS_File& orig) {
	flags=orig.flags;
	time=orig.time;
	date=orig.date;
	attr=orig.attr;
	refCtr=orig.refCtr;
	open=orig.open;
	hdrive=orig.hdrive;
	name=0;
	if(orig.name) {
		name=new char [strlen(orig.name) + 1];strcpy(name,orig.name);
	}
}

DOS_File & DOS_File::operator= (const DOS_File & orig) {
	flags=orig.flags;
	time=orig.time;
	date=orig.date;
	attr=orig.attr;
	refCtr=orig.refCtr;
	open=orig.open;
	hdrive=orig.hdrive;
	if(name) {
		delete [] name; name=0;
	}
	if(orig.name) {
		name=new char [strlen(orig.name) + 1];strcpy(name,orig.name);
	}
	return *this;
}

Bit8u DOS_FindDevice(char const * name) {
	/* should only check for the names before the dot and spacepadded */
	char fullname[DOS_PATHLENGTH];Bit8u drive;
	bool ime_flag = false;
//	if(!name || !(*name)) return DOS_DEVICES; //important, but makename does it
	if(*name == '@' && *(name + 1) == ':') {
		strcpy(fullname, name + 2);
		ime_flag = true;
	} else {
		if (!DOS_MakeName(name,fullname,&drive)) return DOS_DEVICES;
	}
	char* name_part = strrchr_dbcs(fullname,'\\');
	if(name_part) {
		*name_part++ = 0;
		//Check validity of leading directory.
		if(!Drives[drive]->TestDir(fullname)) return DOS_DEVICES;
	} else name_part = fullname;

	char* dot = strrchr(name_part,'.');
	if(dot) *dot = 0; //no ext checking

	DOS_CheckOpenExtDevice(name_part);
	for(Bit8s index = DOS_DEVICES - 1 ; index >= 0 ; index--) {
		if(Devices[index]) {
			if(Devices[index]->GetInformation() & EXT_DEVICE_BIT) {
				if(WildFileCmp(name_part, Devices[index]->name)) {
					if(DOS_CheckExtDevice(name_part, false) != 0) {
						return index;
					} else {
						delete Devices[index];
						Devices[index] = 0;
						break;
					}
				}
			}
		}
	}
	if(ime_flag) {
		return DOS_DEVICES;
	}

	static char com[5] = { 'C','O','M','1',0 };
	static char lpt[5] = { 'L','P','T','1',0 };
	// AUX is alias for COM1 and PRN for LPT1
	// A bit of a hack. (but less then before).
	// no need for casecmp as makename returns uppercase
	if (strcmp(name_part, "AUX") == 0) name_part = com;
	if (strcmp(name_part, "PRN") == 0) name_part = lpt;

	/* loop through devices */
	for(Bit8u index = 0;index < DOS_DEVICES;index++) {
		if (Devices[index]) {
			if (WildFileCmp(name_part,Devices[index]->name)) return index;
		}
	}
	return DOS_DEVICES;
}


void DOS_AddDevice(DOS_Device * adddev) {
//Caller creates the device. We store a pointer to it
//TODO Give the Device a real handler in low memory that responds to calls
	for(Bitu i = 0; i < DOS_DEVICES;i++) {
		if(!Devices[i]){
			Devices[i] = adddev;
			Devices[i]->SetDeviceNumber(i);
			return;
		}
	}
	E_Exit("DOS:Too many devices added");
}

void DOS_DelDevice(DOS_Device * dev) {
// We will destroy the device if we find it in our list.
// TODO:The file table is not checked to see the device is opened somewhere!
	for (Bitu i = 0; i <DOS_DEVICES;i++) {
		if(Devices[i] && !strcasecmp(Devices[i]->name,dev->name)){
			delete Devices[i];
			Devices[i] = 0;
			return;
		}
	}
}

void DOS_SetupDevices(void) {
	DOS_Device * newdev;
	newdev=new device_CON();
	DOS_AddDevice(newdev);
	DOS_Device * newdev2;
	newdev2=new device_NUL();
	DOS_AddDevice(newdev2);
	DOS_Device * newdev3;
	newdev3=new device_LPT1();
	DOS_AddDevice(newdev3);
}

void DOS_ClearKeyMap()
{
	for(Bitu i = 0 ; i < DOS_DEVICES ; i++) {
		if(Devices[i]) {
			if(Devices[i]->IsName("CON")) {
				device_CON *con = (device_CON *)Devices[i];
				con->ClearKeyMap();
				break;
			}
		}
	}
}

void DOS_SetConKey(Bit16u src, Bit16u dst)
{
	for(Bitu i = 0 ; i < DOS_DEVICES ; i++) {
		if(Devices[i]) {
			if(Devices[i]->IsName("CON")) {
				device_CON *con = (device_CON *)Devices[i];
				con->SetKeyMap(src, dst);
				break;
			}
		}
	}
}

