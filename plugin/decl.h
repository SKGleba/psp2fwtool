/* THIS FILE IS A PART OF PSP2FWTOOL
 *
 * Copyright (C) 2019-2022 skgleba
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __DECL_H__
#define __DECL_H__

// SceThreadmgrForDriver
int sceKernelCreateThread(const char* name, SceKernelThreadEntry entry, int initPriority, uint32_t stackSize, unsigned int attr, int cpuAffinityMask, const SceKernelThreadOptParam* option);
int sceKernelStartThread(int thid, uint32_t arglen, void* argp);
int sceKernelWaitThreadEnd(int thid, int* stat, unsigned int* timeout);
int sceKernelDeleteThread(int thid);

// SceSysconForDriver
uint32_t sceSysconGetHardwareInfo(void);
int sceSysconErnieShutdown(int mode);
int sceSysconBatterySWReset(void);

// SceIofilemgrForDriver
int sceIoClose(int fd);
int sceIoOpen(const char* file, int flags, int mode);
int sceIoPread(int fd, void* data, uint32_t size, uint64_t offset);
int sceIoPwrite(int fd, void* data, uint32_t size, uint64_t offset);
int sceIoMount(int id, const char* path, int permission, int a4, int a5, int a6);
int sceIoUmount(int id, int a2, int a3, int a4);
int sceIoGetstat(const char* file, SceIoStat* stat);
int sceIoWrite(int fd, void* data, uint32_t size);

// SceSdifForDriver
int sceSdifReadSectorMmc(int devictx, uint32_t sector, void* buffer, uint32_t count);
int sceSdifWriteSectorMmc(int devictx, uint32_t sector, void* buffer, uint32_t count);
int sceSdifReadSectorSd(int devictx, uint32_t sector, void* buffer, uint32_t count);
int sceSdifWriteSectorSd(int devictx, uint32_t sector, void* buffer, uint32_t count);
int sceSdifGetSdContextPartValidateMmc(int device);
int sceSdifGetSdContextPartValidateSd(int device);

// SceModulemgrForDriver
int sceKernelSearchModuleByName(const char* name);

// ScePowerForDriver
int scePowerRequestErnieShutdown(int mode);
int scePowerRequestColdReset(void);
int scePowerSetArmClockFrequency(int freq);
int scePowerSetBusClockFrequency(int freq);

// SceDebugForDriver
int sceDebugPrintf(const char* fmt, ...);

// SceKernelUtilsForDriver
int sceSha256Digest(const void* plain, uint32_t len, void* digest);
int sceGzipDecompress(void* dst, uint32_t dst_size, const void* src, uint32_t* crc32);

// SceSblAIMgrForDriver
int sceSblAimgrIsCEX(void);
int sceSblAimgrIsDEX(void);
int sceSblAimgrIsTest(void);
int sceSblAimgrIsTool(void);

// SceSysmemForDriver
int sceKernelAllocMemBlock(const char* name, unsigned int type, uint32_t size, SceKernelAllocMemBlockKernelOpt* opt);
int sceKernelGetMemBlockBase(int uid, void** base);
int sceKernelMemcpyUserToKernel(void* dst, const void* src, uint32_t size);
int sceKernelMemcpyKernelToUser(void* dst, const void* src, uint32_t size);

// SceSysrootForKernel
void* sceSysrootGetSysrootBase(void);

#endif