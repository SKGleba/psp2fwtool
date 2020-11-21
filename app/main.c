/* THIS FILE IS A PART OF PSP2FWTOOL
 *
 * Copyright (C) 2019-2020 skgleba
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <taihen.h>
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include "debugScreen.h"
#include "../plugin/fwtool.h"

#define DBG(...) sceClibPrintf(__VA_ARGS__);

#define printf(...)                        \
	do                                     \
	{                                      \
		sceClibPrintf(__VA_ARGS__);        \
		psvDebugScreenPrintf(__VA_ARGS__); \
	} while (0)

#define COLORPRINTF(color, ...)                \
	do                                         \
	{                                          \
		psvDebugScreenSetFgColor(color);       \
		sceClibPrintf(__VA_ARGS__);            \
		psvDebugScreenPrintf(__VA_ARGS__);     \
		psvDebugScreenSetFgColor(COLOR_WHITE); \
	} while (0)

extern int fwtool_read_fwimage(uint32_t offset, uint32_t size, uint32_t crc32, uint32_t unzip);
extern int fwtool_write_partition(uint32_t offset, uint32_t size, uint8_t partition);
extern int fwtool_personalize_bl(int fup);
extern int fwtool_update_mbr(int use_e2x, int swap_bl, int swap_os);
extern int fwtool_flash_e2x(uint32_t size);
extern int fwtool_unlink(void);
extern int fwtool_talku(int cmd, int cmdbuf);
extern int fwtool_rw_emmcimg(int dump);

static int redir_writes = 0, file_logging = 0, skip_int_chk = 0;
static char src_u[64];

void main_check_stop(uint32_t code) {
	SceCtrlData pad, pad1;
	sceCtrlPeekBufferPositive(0, &pad1, 1);
	if (!(pad1.buttons & SCE_CTRL_TRIANGLE))
		return;
	COLORPRINTF(COLOR_RED, "REQ_STOP | CODE: 0x%X\n", code);
	COLORPRINTF(COLOR_YELLOW, "Press %s.\n", "\n CROSS to reset\n SQUARE to continue\n CIRCLE to exit");
	sceKernelDelayThread(1000 * 1000);
	while (1) {
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons & SCE_CTRL_CIRCLE)
			break;
		else if (pad.buttons & SCE_CTRL_SQUARE)
			return;
		else if (pad.buttons & SCE_CTRL_CROSS) {
			scePowerRequestColdReset();
			break;
		}
		sceKernelDelayThread(200 * 1000);
	}
	sceKernelExitProcess(0);
	sceKernelDelayThread(200 * 1000);
}

#include "fwimg.c"
#include "rpoint.c"

const char main_opt_str[4][32] = { " -> Flash a firmware image", " -> Create a EMMC image", " -> Restore the EMMC image", " -> Exit" };
const char settings_opt_str[4][32] = { " -> Toggle file logging", " -> Toggle wredirect to GC-SD", " -> Toggle integrity checks", " -> Back" };
int optct = 4;

void erroff() {
	COLORPRINTF(COLOR_RED, "ERR_REQ_OFF\n");
	SceCtrlData pad;
	COLORPRINTF(COLOR_YELLOW, "Press %s.\n", "CROSS to reset or CIRCLE to exit");
	while (1) {
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons & SCE_CTRL_CIRCLE)
			break;
		else if (pad.buttons & SCE_CTRL_CROSS) {
			scePowerRequestColdReset();
			break;
		}
		sceKernelDelayThread(200 * 1000);
	}
	sceKernelExitProcess(0);
}

void agreement() {
	printf("\nThis software will make PERMANENT modifications to your Vita\nIf anything goes wrong, there is NO RECOVERY.\n\n");
	COLORPRINTF(COLOR_PURPLE, "\n\n -> I understood, continue.\n\n");
	SceCtrlData pad;
	while (1) {
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons & SCE_CTRL_CIRCLE)
			break;
		else if (pad.buttons & SCE_CTRL_CROSS)
			return;
		sceKernelDelayThread(200 * 1000);
	}
	sceKernelExitProcess(0);
}

void main_menu(int sel) {
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_CYAN, FWTOOL_VERSION_STR "\n");
	for (int i = 0; i < optct; i++) {
		if (sel == i)
			psvDebugScreenSetFgColor(COLOR_PURPLE);
		printf("%s\n", main_opt_str[i]);
		psvDebugScreenSetFgColor(COLOR_WHITE);
	}
	psvDebugScreenSetFgColor(COLOR_WHITE);
}

void settings_menu(int sel) {
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_CYAN, FWTOOL_VERSION_STR "\n");
	COLORPRINTF(COLOR_YELLOW, "\nDEV SETTINGS\n");
	for (int i = 0; i < optct; i++) {
		if (sel == i)
			psvDebugScreenSetFgColor(COLOR_PURPLE);
		printf("%s\n", settings_opt_str[i]);
		psvDebugScreenSetFgColor(COLOR_WHITE);
	}
	psvDebugScreenSetFgColor(COLOR_WHITE);
}

int settings(void) {
	int sel = 0;
	SceCtrlData pad;
	settings_menu(0);
	while (1) {
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons == SCE_CTRL_CROSS) {
			if (sel == 0) {
				file_logging = fwtool_talku(5, 0);
				COLORPRINTF(COLOR_YELLOW, "FILE LOGGING: %s\n", (file_logging) ? "ENABLED" : "DISABLED");
			} else if (sel == 1) {
				redir_writes = fwtool_talku(9, 0);
				COLORPRINTF(COLOR_YELLOW, "REDIR WRITES: %s\n", (redir_writes) ? "ENABLED" : "DISABLED");
			} else if (sel == 2) {
				skip_int_chk = fwtool_talku(17, 0);
				COLORPRINTF(COLOR_YELLOW, "SKIP INT CHK: %s\n", (skip_int_chk) ? "ENABLED" : "DISABLED");
			} else if (sel > 2)
				return 69;
			sceKernelDelayThread(0.3 * 1000 * 1000);
		} else if (pad.buttons == SCE_CTRL_UP) {
			if (sel != 0)
				sel--;
			settings_menu(sel);
			sceKernelDelayThread(0.3 * 1000 * 1000);
		} else if (pad.buttons == SCE_CTRL_DOWN) {
			if (sel + 1 < optct)
				sel++;
			settings_menu(sel);
			sceKernelDelayThread(0.3 * 1000 * 1000);
		}
	}
	return -1;
}

int main(int argc, char* argv[]) {
	psvDebugScreenInit();
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_CYAN, FWTOOL_VERSION_STR "\n");
	sceIoMkdir("ux0:data/fwtool/", 6);
	printf("\nLoading the supply module...\n");
	tai_module_args_t argg;
	argg.size = sizeof(argg);
	argg.pid = KERNEL_PID;
	argg.args = 0;
	argg.argp = NULL;
	argg.flags = 0;
	// load fwtool kernel
	if (taiLoadStartKernelModuleForUser("ux0:app/SKGFWT00L/fwtool.skprx", &argg) < 0) {
		if (fwtool_talku(16, 0) < 0) {
			printf("get_bind_status failed, the kernel module either failed to load or was locked by another process\n");
			erroff();
		}
	} else
		sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);

	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_CYAN, FWTOOL_VERSION_STR "\n");
	agreement();

	int sel = 0, ret = 0;
	SceCtrlData pad;
rloop:
	main_menu(0);
	sceKernelDelayThread(1 * 1000 * 1000);
	while (1) {
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons == SCE_CTRL_CROSS) {
			ret = -1;
			if (sel == 0)
				ret = update_proxy();
			else if (sel == 1)
				ret = create_proxy();
			else if (sel == 2)
				ret = restore_proxy();
			else if (sel > 2) {
				sceKernelExitProcess(0);
				return 0;
			}
			break;
		} else if (pad.buttons == SCE_CTRL_UP) {
			if (sel != 0)
				sel--;
			main_menu(sel);
			sceKernelDelayThread(0.3 * 1000 * 1000);
		} else if (pad.buttons == SCE_CTRL_DOWN) {
			if (sel + 1 < optct)
				sel++;
			main_menu(sel);
			sceKernelDelayThread(0.3 * 1000 * 1000);
		} else if (pad.buttons == SCE_CTRL_SELECT) {
			ret = settings();
			break;
		}
	}

	if (ret < 0)
		erroff();
	else if (ret == 69)
		goto rloop;

	return 0;
}
