/*
 * Simple kplugin loader by xerpi
 */

#include <stdio.h>
#include <stdlib.h> 
#include <taihen.h>
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include "debugScreen.h"
#include "../plugin/fwtool.h"

#define MOD_PATH "ux0:app/SKGFWT00L/fwtool.skprx"
	
#define printf(...) psvDebugScreenPrintf(__VA_ARGS__)

extern int fwtool_cmd_handler(int cmd, void *cmdbuf);

volatile pkg_toc rtoc;
volatile il_mode ilm;
volatile pkg_fs_etr fs_args;

void wait_key_press(int mode)
{
	SceCtrlData pad;

	printf("Press CROSS to %s.\n", (mode) ? "flash" : "reboot");

	while (1) {
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons & SCE_CTRL_CROSS)
			break;
		if ((pad.buttons & SCE_CTRL_TRIANGLE) && (pad.buttons & SCE_CTRL_SELECT))
			fwtool_cmd_handler(69, NULL);
		sceKernelDelayThread(200 * 1000);
	}
}

void erroff() {
	printf("ERR_REQ_OFF\n");
	wait_key_press(0);
	scePowerRequestColdReset();
}

void blank_xd() {
	void *buf = malloc(0x100);
	vshIoUmount(0x200, 0, 0, 0);
	vshIoUmount(0x200, 1, 0, 0);
	_vshIoMount(0x200, 0, 2, buf);
	char blankxd[0x200];
	memset(&blankxd, 0, sizeof(blankxd));
	*(uint32_t *)blankxd = 0xCAFEBABE;
	int fd = sceIoOpen("os0:patches.e2xd", SCE_O_WRONLY | SCE_O_TRUNC | SCE_O_CREAT, 6);
	sceIoWrite(fd, &blankxd, 0x200);
	sceIoClose(fd);
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
			printf("error: 0x1\n");
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
		printf("error: 0x%X\n", ret);
		erroff();
		return 2;
	}
	printf("ok!\n");
	return ret;
}

int main(int argc, char *argv[])
{
	int ret;
	SceUID mod_id;
	psvDebugScreenInit();
	printf("FWTOOL v0.4\n");
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
		wait_key_press(1);
		printf("\n---------STAGE 1: SET_NFO---------\n\n");
		ret = set_infobuf(1);
		if (ret != 0) {
			printf("error: 0x%X\n", ret);
			erroff();
			return 0;
		}
		printf("\nUPDATE_MODE: %s\n", (ilm.target == 6) ? "OFF" : "ON");
		printf("WRITE_EMMC: %s\n", (ilm.fmode) ? "YES" : "NO");
		printf("TARGET_TYPE: %s\n", target_dev[ilm.target]);
		if (ilm.target != 6 && rtoc.changefw == 1) {
			printf("\n---------STAGE 2: SC_INIT---------\n\n");
			printf("Changing SNVS firmware...");
			ret = fwtool_cmd_handler(34, NULL);
			if (ret != 0) {
				printf("error: 0x%X\n", ret);
				erroff();
				return 0;
			}
			printf("ok!\n");
			printf("\n---------STAGE 3: CLEANFS---------\n\n");
			ret = fwtool_cmd_handler(3, NULL);
			if (ret != 0) {
				printf("error: 0x%X\n", ret);
				erroff();
				return 0;
			}
			printf("MBR is now clean\n");
		}
		printf("\n---------STAGE 420: FLASH---------\n\n");
		ret = flashloop();
		if (ret != 0) {
			printf("error: 0x%X\n", ret);
			erroff();
			return 0;
		}
		if (ilm.target != 6 && rtoc.has_e2x == 1) {
			printf("\n---------STAGE 666: ++E2X---------\n\n");
			ret = fwtool_cmd_handler(3, NULL);
			if (ret != 0) {
				printf("error: 0x%X\n", ret);
				erroff();
				return 0;
			}
			printf("MBR now points to fake os0\n");
			blank_xd();
			printf("e2x installed\n");
			
		}
		printf("\nALL DONE, rebooting in 5s\n");
		sceKernelDelayThread(5 * 1000 * 1000);
		if (ilm.target != 6)
			sceIoRemove("ux0:id.dat");
		scePowerRequestColdReset();
	}
	return 0;
}
