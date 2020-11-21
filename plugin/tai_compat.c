/* THIS FILE IS A PART OF PSP2FWTOOL
 *
 * Copyright (C) 2019-2020 skgleba
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <string.h>
#include <taihen.h>
#include <vitasdkkern.h>

static int (*cleainv_dcache)(void* addr, uint32_t size) = NULL;								  // DcacheCleanInvalidateRange
static int (*get_mod_info)(SceUID pid, SceUID modid, SceKernelModuleInfo* sceinfo_op) = NULL; // GetModuleInfo

#define DACR_OFF(stmt)                     \
	do                                     \
	{                                      \
		unsigned prev_dacr;                \
		__asm__ volatile(                  \
			"mrc p15, 0, %0, c3, c0, 0 \n" \
			: "=r"(prev_dacr));            \
		__asm__ volatile(                  \
			"mcr p15, 0, %0, c3, c0, 0 \n" \
			:                              \
			: "r"(0xFFFF0000));            \
		stmt;                              \
		__asm__ volatile(                  \
			"mcr p15, 0, %0, c3, c0, 0 \n" \
			:                              \
			: "r"(prev_dacr));             \
	} while (0)

// [sz]B @ [data] -> [name] @ [off]
#define INJECT(name, off, data, sz)                              \
	do                                                           \
	{                                                            \
		uintptr_t addr = 0;                                      \
		int modid = ksceKernelSearchModuleByName(name);          \
		if (modid >= 0)                                          \
		{                                                        \
			module_get_offset(KERNEL_PID, modid, 0, off, &addr); \
			DACR_OFF(memcpy((void *)addr, (void *)data, sz););   \
			cleainv_dcache((void *)addr, sz);                    \
		}                                                        \
	} while (0)

// [sz]B @ [data] -> [modid] @ [off]
#define INJECT_NOGET(modid, off, data, sz)                   \
	do                                                       \
	{                                                        \
		uintptr_t addr = 0;                                  \
		module_get_offset(KERNEL_PID, modid, 0, off, &addr); \
		DACR_OFF(memcpy((void *)addr, (void *)data, sz););   \
		cleainv_dcache((void *)addr, sz);                    \
	} while (0)

// taihen's module_get_offset
int module_get_offset(SceUID pid, SceUID modid, int segidx, size_t offset, uintptr_t* addr) {
	SceKernelModuleInfo sceinfo;
	int ret;
	if (segidx > 3)
		return -1;
	sceinfo.size = sizeof(sceinfo);
	if (get_mod_info == NULL)
		return -1;
	ret = get_mod_info(pid, modid, &sceinfo);
	if (ret < 0)
		return ret;
	if (offset > sceinfo.segments[segidx].memsz)
		return -1;
	*addr = (uintptr_t)sceinfo.segments[segidx].vaddr + offset;
	return 1;
}

/*
 sets the getModuleInfo & cleanDcache ptr and returns
 * 0 if fw is probably 3.60
 * 1 if fw is probably 3.65
 * -1 if bad fw or other error
*/
int tai_init(void) {
	char stuz[4];
	int tifwv = 1, sysmemid = ksceKernelSearchModuleByName("SceSysmem");
	stuz[0] = *(uint8_t*)ksceKernelSearchModuleByName;
	stuz[1] = (*(uint8_t*)(ksceKernelSearchModuleByName + 1) - 0xC0) + (*(uint8_t*)(ksceKernelSearchModuleByName + 2) * 0x10);
	stuz[2] = *(uint8_t*)(ksceKernelSearchModuleByName + 4);
	stuz[3] = (*(uint8_t*)(ksceKernelSearchModuleByName + 5) - 0xC0) + (*(uint8_t*)(ksceKernelSearchModuleByName + 6) - 0x40);
	get_mod_info = (void*)(*(uint32_t*)stuz) + 0x30b4; // ksceKernelSearchModuleByName is @ 0x3d00
	if (*(uint16_t*)get_mod_info != 0x83b5) {
		get_mod_info = (void*)(*(uint32_t*)stuz) + 0x476c;
		if (*(uint16_t*)get_mod_info != 0x83b5)
			return -1;
		tifwv = 0;
	}
	if (module_get_offset(KERNEL_PID, sysmemid, 0, 0x21719, (uintptr_t*)&cleainv_dcache) < 1)
		return -1;
	return tifwv;
}