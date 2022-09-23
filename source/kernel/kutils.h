/* THIS FILE IS A PART OF PSP2FWTOOL
 *
 * Copyright (C) 2019-2021 skgleba
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

// Requires taihen or tai_compat

// Kernel logging
static int enable_f_logging = 0; // enable file loging
static char buf_logging[256];

#define LOG(...)                                                       					\
	do                                                                 					\
	{                                                                 					\
		sceDebugPrintf(__VA_ARGS__);                                  					\
		if (enable_f_logging)                                          					\
		{                                                              					\
			memset(buf_logging, 0, sizeof(buf_logging));               					\
			snprintf(buf_logging, sizeof(buf_logging), ##__VA_ARGS__); 					\
			logg(buf_logging, strnlen(buf_logging, sizeof(buf_logging)), LOG_LOC, 2);  	\
		};                                                             					\
	} while (0)

#define LOG_START(...)                                                 					\
	do                                                                 					\
	{                                                                  					\
		sceDebugPrintf("starting new log\n");                         					\
		sceDebugPrintf(__VA_ARGS__);                                 					\
		if (enable_f_logging)                                          					\
		{                                                              					\
			memset(buf_logging, 0, sizeof(buf_logging));               					\
			snprintf(buf_logging, sizeof(buf_logging), ##__VA_ARGS__); 					\
			logg(buf_logging, strnlen(buf_logging, sizeof(buf_logging)), LOG_LOC, 1);	\
		};                                                             					\
	} while (0)

static int logg(void* buffer, int length, const char* logloc, int create) {
	int fd;
	if (create == 0) {
		fd = sceIoOpen(logloc, SCE_O_WRONLY | SCE_O_APPEND, 6);
	} else if (create == 1) {
		fd = sceIoOpen(logloc, SCE_O_WRONLY | SCE_O_TRUNC | SCE_O_CREAT, 6);
	} else if (create == 2) {
		fd = sceIoOpen(logloc, SCE_O_WRONLY | SCE_O_APPEND | SCE_O_CREAT, 6);
	}
	if (fd < 0)
		return 0;
	sceIoWrite(fd, buffer, length);
	sceIoClose(fd);
	return 1;
}

//misc--------------------
#define ALIGN_SECTOR(s) ((s + (BLOCK_SIZE - 1)) & -BLOCK_SIZE) // align (arg) to BLOCK_SIZE
#define ARRAYSIZE(x) ((sizeof(x) / sizeof(0 [x])) / ((size_t)(!(sizeof(x) % sizeof(0 [x])))))

// fix for sdstor0 RW while system is running
static int siofix(void* func) {
	int ret = 0, res = 0, uid = 0;
	ret = uid = sceKernelCreateThread("siofix", func, 64, 0x10000, 0, 0, 0);
	if ((ret < 0) || ((ret = sceKernelStartThread(uid, 0, NULL)) < 0) || ((ret = sceKernelWaitThreadEnd(uid, &res, NULL)) < 0)) {
		ret = -1;
		goto cleanup;
	}
	ret = res;
cleanup:
	if (uid > 0)
		sceKernelDeleteThread(uid);
	return ret;
}

// get vaddr for paddr
unsigned int pa2va(unsigned int pa) {
	unsigned int va = 0, vaddr = 0, paddr = 0;
	for (int i = 0; i < 0x100000; i++) {
		vaddr = i << 12;
		__asm__("mcr p15,0,%1,c7,c8,0\n\t"
			"mrc p15,0,%0,c7,c4,0\n\t" : "=r" (paddr) : "r" (vaddr));
		if ((pa & 0xFFFFF000) == (paddr & 0xFFFFF000)) {
			va = vaddr + (pa & 0xFFF);
			break;
		}
	}
	return va;
}