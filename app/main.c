#include <stdio.h>
#include <stdlib.h> 
#include <taihen.h>
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include "debugScreen.h"
#include "../plugin/fwtool.h"

#define MOD_PATH "ux0:app/SKGFWT00L/fwtool.skprx"
	
#define printf(...) psvDebugScreenPrintf(__VA_ARGS__)
#define COLORPRINTF(color, ...)		\
do {                                \
	psvDebugScreenSetFgColor(color);\
	psvDebugScreenPrintf(__VA_ARGS__);\
	psvDebugScreenSetFgColor(COLOR_WHITE);\
} while (0)

extern int fwtool_cmd_handler(int cmd, void *cmdbuf);

volatile pkg_toc rtoc;
volatile il_mode ilm;
volatile pkg_fs_etr fs_args;

void wait_key_press(int mode)
{
	SceCtrlData pad;
	COLORPRINTF(COLOR_YELLOW, "Press %s.\n", (mode) ? "CROSS to flash or CIRCLE to exit" : "CIRCLE to reboot");
	while (1) {
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons & SCE_CTRL_CIRCLE) {
			if (mode)
				sceKernelExitProcess(0);
			else
				scePowerRequestColdReset();
		}
		if ((pad.buttons & SCE_CTRL_CROSS) && mode == 1)
			break;
		if ((pad.buttons & SCE_CTRL_TRIANGLE) && (pad.buttons & SCE_CTRL_SELECT)) {
			fwtool_cmd_handler(69, NULL);
			COLORPRINTF(COLOR_GREEN, "debugging mode set\n");
		}
		sceKernelDelayThread(200 * 1000);
	}
}

void erroff() {
	COLORPRINTF(COLOR_RED, "ERR_REQ_OFF\n");
	wait_key_press(0);
	scePowerRequestColdReset();
}

int flashloop() {
	SceUID fd;
	uint32_t off = sizeof(pkg_toc), ret = 1;
	uint8_t ecount = 0;
	while (ecount < rtoc.fs_count) {
		fd = sceIoOpen(ilm.inp, SCE_O_RDONLY, 0);
		sceIoPread(fd, (void *)&fs_args, sizeof(pkg_fs_etr), off);
		sceIoClose(fd);
		if (fs_args.magic == 0x69 && fs_args.pkg_off > 0) {
			ret = 2;
			printf("%s -> sdstor0:%s-lp-%s-%s...\n", ilm.inp, stor_st[fs_args.dst_etr[0]], stor_rd[fs_args.dst_etr[1]], stor_th[fs_args.dst_etr[2]]);
			if (fs_args.dst_etr[0] == 0 && fs_args.dst_etr[2] == 3) 
				ret = fwtool_cmd_handler(1, (void *)&fs_args);
			else if (fs_args.dst_etr[2] == 16 && fs_args.dst_off == 0x400 && rtoc.has_e2x == 1)
				ret = fwtool_cmd_handler(4, (void *)&fs_args);
			else
				ret = fwtool_cmd_handler(2, (void *)&fs_args);
		} else
			ret = 14;
		if (ret != 0) {
			COLORPRINTF(COLOR_RED, "error: 0x1\n");
			erroff();
			return 1;
		}
		ecount = ecount + 1;
		off = fs_args.pkg_off + fs_args.pkg_sz;
	}
	return 0;
}

int set_infobuf(uint8_t write_mode) {
	printf("Setting pkg info... ");
	ilm.inp = "ux0:data/fwtool/fwimage.bin";
	ilm.oup = "ux0:data/fwtool/testimg.bin";
	SceUID fd = sceIoOpen(ilm.inp, SCE_O_RDONLY, 0);
	if (fd >= 0) {
		sceIoRead(fd, (void *)&rtoc, sizeof(pkg_toc));
		sceIoClose(fd);
	} else
		return 1;
	if (rtoc.magic != 0xDEAFBABE)
		return 3;
	ilm.version = rtoc.version;
	ilm.target = rtoc.target;
	ilm.wmode = write_mode;
	ilm.fmode = rtoc.fmode;
	ilm.fw_minor = rtoc.fw_minor;
	int ret = fwtool_cmd_handler(0, (void *)&ilm);
	if (ret != 0) {
		COLORPRINTF(COLOR_RED, "error: 0x%X\n", ret);
		erroff();
		return 2;
	}
	COLORPRINTF(COLOR_BLUE, "ok!\n");
	return ret;
}

int show_info() {
	pkg_toc totoc;
	SceUID fd = sceIoOpen("ux0:data/fwtool/fwimage.bin", SCE_O_RDONLY, 0);
	if (fd >= 0) {
		sceIoRead(fd, (void *)&totoc, sizeof(pkg_toc));
		sceIoClose(fd);
	} else {
		COLORPRINTF(COLOR_RED, "could NOT open ux0:data/fwtool/fwimage.bin\n");
		return 1;
	}
	psvDebugScreenSetFgColor(COLOR_CYAN);
	COLORPRINTF(COLOR_CYAN, "\npackage location: ");
	printf("ux0:data/fwtool/fwimage.bin\n\n");
	COLORPRINTF(COLOR_CYAN, "package info:\n");
	printf(" version: %d\n target: %s\n has e2x: %s\n bootloaders magic: %d [0x%04X]\n\n", totoc.version, target_dev[totoc.target], (totoc.has_e2x) ? "YES" : "NO", totoc.fw_minor, totoc.fw_minor);
	COLORPRINTF(COLOR_CYAN, "package contents:\n");
	uint32_t off = sizeof(pkg_toc);
	uint8_t ecount = 0;
	pkg_fs_etr fsa;
	fd = sceIoOpen("ux0:data/fwtool/fwimage.bin", SCE_O_RDONLY, 0);
	while (ecount < totoc.fs_count) {
		sceIoPread(fd, (void *)&fsa, sizeof(pkg_fs_etr), off);
		printf(" %d: sdstor0:%s-lp-%s-%s @ %d [%d]\n", ecount, stor_st[fsa.dst_etr[0]], stor_rd[fsa.dst_etr[1]], stor_th[fsa.dst_etr[2]], fsa.dst_off, fsa.dst_sz);
		ecount = ecount + 1;
		off = fsa.pkg_off + fsa.pkg_sz;
	}
	sceIoClose(fd);
	printf("\n");
	return 0;
}

int main(int argc, char *argv[])
{
	int ret;
	SceUID mod_id;
	psvDebugScreenInit();
	COLORPRINTF(COLOR_CYAN, "FWTOOL v0.5 by SKGleba\n");
	printf("\nLoading KModule...\n");
	tai_module_args_t argg;
	argg.size = sizeof(argg);
	argg.pid = KERNEL_PID;
	argg.args = 0;
	argg.argp = NULL;
	argg.flags = 0;
	mod_id = taiLoadStartKernelModuleForUser(MOD_PATH, &argg);
	if (mod_id > 0)
		sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
	else {
		if (show_info() == 1)
			erroff();
		wait_key_press(1);
		psvDebugScreenClear(COLOR_BLACK);
		COLORPRINTF(COLOR_CYAN, "\n---------STAGE 1: SET_NFO---------\n\n");
		ret = set_infobuf(1);
		if (ret != 0) {
			COLORPRINTF(COLOR_RED, "error: 0x%X\n", ret);
			erroff();
			return 0;
		}
		printf("\nUPDATE_MODE: %s\n", (ilm.target == 6) ? "OFF" : "ON");
		printf("WRITE_EMMC: %s\n", (ilm.fmode) ? "YES" : "NO");
		printf("TARGET_TYPE: %s\n", target_dev[ilm.target]);
		if (ilm.target != 6 && rtoc.changefw == 1) {
			COLORPRINTF(COLOR_CYAN, "\n---------STAGE 2: SC_INIT---------\n\n");
			printf("Changing SNVS firmware...");
			ret = fwtool_cmd_handler(34, NULL);
			if (ret != 0) {
				COLORPRINTF(COLOR_RED, "error: 0x%X\n", ret);
				erroff();
				return 0;
			}
			COLORPRINTF(COLOR_BLUE, "ok!\n");
			COLORPRINTF(COLOR_CYAN, "\n---------STAGE 3: CLEANFS---------\n\n");
			ret = fwtool_cmd_handler(3, NULL);
			if (ret != 0) {
				COLORPRINTF(COLOR_RED, "error: 0x%X\n", ret);
				erroff();
				return 0;
			}
			COLORPRINTF(COLOR_BLUE, "MBR is now clean\n");
		}
		COLORPRINTF(COLOR_CYAN, "\n---------STAGE 420: FLASH---------\n\n");
		ret = flashloop();
		if (ret != 0) {
			COLORPRINTF(COLOR_RED, "error: 0x%X\n", ret);
			erroff();
			return 0;
		}
		if (ilm.target != 6 && rtoc.has_e2x == 1) {
			COLORPRINTF(COLOR_CYAN, "\n---------STAGE 666: ++E2X---------\n\n");
			ret = fwtool_cmd_handler(3, NULL);
			if (ret != 0) {
				COLORPRINTF(COLOR_RED, "error: 0x%X\n", ret);
				erroff();
				return 0;
			}
			COLORPRINTF(COLOR_BLUE, "MBR now points to fake os0\ne2x installed\n");
		}
		COLORPRINTF(COLOR_CYAN, "\nALL DONE, rebooting in 5s\n");
		sceKernelDelayThread(5 * 1000 * 1000);
		if (ilm.target != 6)
			sceIoRemove("ux0:id.dat");
		scePowerRequestColdReset();
	}
	return 0;
}
