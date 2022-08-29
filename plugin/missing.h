/* THIS FILE IS A PART OF PSP2FWTOOL
 *
 * Copyright (C) 2019-2021 skgleba
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

extern int ksceSdifReadSectorMmc(int devictx, uint32_t sector, void* buffer, uint32_t count);
extern int ksceSdifWriteSectorMmc(int devictx, uint32_t sector, void* buffer, uint32_t count);
extern int ksceSdifReadSectorSd(int devictx, uint32_t sector, void* buffer, uint32_t count);
extern int ksceSdifWriteSectorSd(int devictx, uint32_t sector, void* buffer, uint32_t count);
extern int ksceSdifGetSdContextPartValidateMmc(int device);
extern int ksceSdifGetSdContextPartValidateSd(int device);
extern void* ksceSysrootGetSysbase(void);
extern int ksceSysconAbbySync(int* status);
extern int ksceSysconErnieShutdown(int mode);
extern int ksceSysconBatterySWReset(void);
extern int kscePowerRequestErnieShutdown(int mode);

typedef struct {
    const char* dev;
    const char* dev2;
    const char* blkdev;
    const char* blkdev2;
    int id;
} SceIoDevice;

typedef struct {
    int id;
    const char* dev_unix;
    int unk;
    int dev_major;
    int dev_minor;
    const char* dev_filesystem;
    int unk2;
    SceIoDevice* dev;
    int unk3;
    SceIoDevice* dev2;
    int unk4;
    int unk5;
    int unk6;
    int unk7;
} SceIoMountPoint;

enum MEMBLOCK_CONSTRUCT {
    MB_A_RO = 0x4,
    MB_A_RX = 0x5,
    MB_A_RW = 0x6,
    MB_A_F = 0x7,
    MB_A_URO = 0x40,
    MB_A_URX = 0x50,
    MB_A_URW = 0x60,
    MB_A_UF = 0x70,
    MB_S_G = 0x200,
    MB_S_H = 0x800,
    MB_S_S = 0xD00,
    MB_C_LD = 0x2000,
    MB_C_HE = 0x4000,
    MB_C_N = 0x8000,
    MB_C_Y = 0xD000,
    MB_I_IO = 0x100000,
    MB_I_DEF = 0x200000,
    MB_I_CDR = 0x400000,
    MB_I_BU = 0x500000,
    MB_I_PC = 0x800000,
    MB_I_SHR = 0x900000,
    MB_I_CDLG = 0xA00000,
    MB_I_BK = 0xC00000,
    MB_I_PMM = 0xF00000,
    MB_U_CDRN = 0x5000000,
    MB_U_UNF = 0x6000000,
    MB_U_CDRD = 0x9000000,
    MB_U_SHR = 0xA000000,
    MB_U_IO = 0xB000000,
    MB_U_DEF = 0xC000000,
    MB_U_PC = 0xD000000,
    MB_U_CDLGP = 0xE000000,
    MB_U_CDLGV = 0xF000000,
    MB_K_DEF = 0x10000000,
    MB_K_IO = 0x20000000,
    MB_K_PC = 0x30000000,
    MB_K_CDRD = 0x40000000,
    MB_K_CDRN = 0x50000000,
    MB_K_UNF = 0x60000000,
    MB_K_GPU = 0xA0000000,
};