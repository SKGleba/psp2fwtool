/*
 * Simple kplugin loader by xerpi
 */

#include <stdio.h>
#include <taihen.h>
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include "debugScreen.h"

#define MOD_PATH "ux0:app/SKG8R1CC0/bricc.skprx"
	
#define printf(...) psvDebugScreenPrintf(__VA_ARGS__)

static int only_chk = 0;

typedef struct {
	uint32_t	size[16];
	uint32_t	coff0[16];
	uint8_t		eoff0[16];
	uint32_t	coff1[16];
	uint8_t		eoff1[16];
	uint32_t	coff2[16];
	uint8_t		eoff2[16];
} __attribute__((packed)) chkbin_data;

typedef struct {
	uint32_t	size;
	char		off0;
	char		off1;
	char		off2;
} __attribute__((packed)) chkt_data;

void wait_key_press()
{
	SceCtrlData pad;

	printf("Press CROSS to flash.\n");

	while (1) {
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons & SCE_CTRL_CROSS)
			break;
		sceKernelDelayThread(200 * 1000);
	}
}

void start_key_press()
{
	SceCtrlData pad;
	printf("Press START to continue.\n");
	while (1) {
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons & SCE_CTRL_START)
			break;
		if (pad.buttons & SCE_CTRL_SELECT) {
			only_chk = 1;
			break;
		}
		if (pad.buttons & SCE_CTRL_TRIANGLE) {
			only_chk = 2;
			break;
		}
		sceKernelDelayThread(200 * 1000);
	}
}

static int check(char *file, int entry) {
	printf("Checking %s [%d]\n", file, entry);
	chkbin_data chkbin;
	chkt_data chkt;
	printf("getting entries for %d... ", entry);
	int mfd = sceIoOpen("ux0:data/bricc/chk.bin", SCE_O_RDONLY, 0);
	if(mfd < 0){
		return 1;
	}
	sceIoPread(mfd, &chkbin, sizeof(chkbin_data), 0);
	int tfd = sceIoOpen(file, SCE_O_RDONLY, 0);
	if(tfd < 0){
		return 2;
	}
	printf("ok!\nComparing:\n");
	chkt.size = sceIoLseek(tfd, 0, SCE_SEEK_END);
	printf("Size... is size 0x%lX=0x%lX?\n", chkt.size, chkbin.size[entry]);
	if (chkt.size != chkbin.size[entry]) 
		return 3;
	sceIoPread(tfd, &chkt.off0, 1, chkbin.coff0[entry]);
	printf("Data 1... is u8 @ 0x%lX = 0x%X?\n", chkbin.coff0[entry], chkbin.eoff0[entry]);
	if ((uint8_t)chkbin.eoff0[entry] != (uint8_t)chkt.off0)
		return 5;
	sceIoPread(tfd, &chkt.off1, 1, chkbin.coff1[entry]);
	printf("Data 2... is u8 @ 0x%lX = 0x%X?\n", chkbin.coff1[entry], chkbin.eoff1[entry]);
	if ((uint8_t)chkbin.eoff1[entry] != (uint8_t)chkt.off1)
		return 6;
	sceIoPread(tfd, &chkt.off2, 1, chkbin.coff2[entry]);
	printf("Data 3... is u8 @ 0x%lX = 0x%X?\n", chkbin.coff2[entry], chkbin.eoff2[entry]);
	if ((uint8_t)chkbin.eoff2[entry] != (uint8_t)chkt.off2)
		return 7;
	sceIoClose(mfd);
	sceIoClose(tfd);
	printf("Done checking %s [%d]\n", file, entry);
	return 0;
}

static int set(char *file, int entry) {
	printf("Setting %s [%d]\n", file, entry);
	chkbin_data chkbin;
	chkt_data chkt;
	printf("prepping entries for %d... ", entry);
	int mfd = sceIoOpen("ux0:data/bricc/chk.bin", SCE_O_WRONLY | SCE_O_TRUNC | SCE_O_CREAT, 6);
	if(mfd < 0){
		return 1;
	}
	int tfd = sceIoOpen(file, SCE_O_RDONLY, 0);
	if(tfd < 0){
		return 2;
	}
	printf("ok!\nSetting:\n");
	chkbin.size[entry] = sceIoLseek(tfd, 0, SCE_SEEK_END);
	chkt.size = chkbin.size[entry]; 
	printf("Size... size 0x%lX=0x%lX\n", chkt.size, chkbin.size[entry]);
	chkbin.coff0[entry] = 0x1;
	sceIoPread(tfd, &chkt.off0, 1, chkbin.coff0[entry]);
	chkbin.eoff0[entry] = (uint8_t)chkt.off0;
	printf("Data 1... u8 @ 0x%lX = 0x%X\n", chkbin.coff0[entry], chkbin.eoff0[entry]);
	chkbin.coff1[entry] = 0x2000;
	sceIoPread(tfd, &chkt.off1, 1, chkbin.coff1[entry]);
	chkbin.eoff1[entry] = (uint8_t)chkt.off1;
	printf("Data 2... u8 @ 0x%lX = 0x%X\n", chkbin.coff1[entry], chkbin.eoff1[entry]);
	chkbin.coff2[entry] = 0x10000;
	sceIoPread(tfd, &chkt.off2, 1, chkbin.coff2[entry]);
	chkbin.eoff2[entry] = (uint8_t)chkt.off2;
	printf("Data 3... u8 @ 0x%lX = 0x%X\n", chkbin.coff2[entry], chkbin.eoff2[entry]);
	sceIoPwrite(mfd, &chkbin, sizeof(chkbin_data), 0);
	sceIoClose(mfd);
	sceIoClose(tfd);
	printf("Done setting %s [%d]\n", file, entry);
	return 0;
}

void erroff() {
	printf("ERR_REQ_OFF\n");
	start_key_press();
	scePowerRequestColdReset();
}

int main(int argc, char *argv[])
{
	int ret;
	SceUID mod_id;
	psvDebugScreenInit();
	printf("FWTOOL v0.2\n");
	start_key_press();
	printf("\nLoading KModule...\n");
	tai_module_args_t argg;
	argg.size = sizeof(argg);
	argg.pid = KERNEL_PID;
	argg.args = 0;
	argg.argp = NULL;
	argg.flags = 0;
	mod_id = taiLoadStartKernelModuleForUser(MOD_PATH, &argg);

	if (mod_id > 0) {
		printf("\n---------STAGE 1: TFW_CHK---------\n\n");
		if (only_chk == 0) {
			ret = check("ux0:data/bricc/slb2.bin", 2);
			if (ret == 0) {
				ret = check("ux0:data/bricc/os0.bin", 3);
					if (ret == 0) {
						ret = check("ux0:data/bricc/vs0.bin", 4);
							if (ret != 0)
								printf("Error @ 14: 0x%X\n", ret);
					} else
						printf("Error @ 13: 0x%X\n", ret);
			} else
				printf("Error @ 12: 0x%X\n", ret);
		} else if (only_chk == 1) {
			ret = set("ux0:data/bricc/slb2.bin", 2);
			if (ret == 0) {
				ret = set("ux0:data/bricc/os0.bin", 3);
					if (ret == 0) {
						ret = set("ux0:data/bricc/vs0.bin", 4);
							if (ret != 0)
								printf("Error @ 14: 0x%X\n", ret);
					} else
						printf("Error @ 13: 0x%X\n", ret);
			} else
				printf("Error @ 12: 0x%X\n", ret);
		} else {
			ret = 0;
			printf("SKIP_CHK\n");
		}
		if (ret != 0) {
			printf("STAGE 1 FAILED\n");
			erroff();
			return 0;
		}
		sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
	} else if (mod_id < 0 && only_chk == 0 && skbricWork(69) == 34) {
		printf("\nUPDATE_MODE: ON\n");
		wait_key_press();
		printf("\n---------STAGE 2: SC_INIT---------\n\n");
		printf("Changing SNVS firmware...");
		ret = skbricWork(53);
		if (ret != 0) {
			printf("error: 0x%X\n", ret);
			erroff();
			return 0;
		}
		printf("ok!\n");
		printf("\n---------STAGE 420: FLASH---------\n\n");
		printf("Flashing slb2... ");
		ret = skbricWork(3);
		if (ret != 0) {
			printf("error: 0x%X\n", ret);
			erroff();
			return 0;
		}
		printf("ok!\n");
		printf("Flashing os0...  ");
		ret = skbricWork(4);
		if (ret != 0) {
			printf("error: 0x%X\n", ret);
			erroff();
			return 0;
		}
		printf("ok!\n");
		printf("Flashing vs0...  ");
		ret = skbricWork(5);
		if (ret != 0) {
			printf("error: 0x%X\n", ret);
			erroff();
			return 0;
		}
		printf("ok!\n");
		printf("\nALL DONE, rebooting in 5s\n");
		sceKernelDelayThread(5 * 1000 * 1000);
		sceIoRemove("ux0:id.dat");
		scePowerRequestColdReset();
	} else if (mod_id < 0 && only_chk == 1 && skbricWork(69) == 34) {
		printf("\nUPDATE_MODE: LOCAL\n");
		wait_key_press();
		printf("ok!\n");
		printf("\n---------STAGE 3: PREP_FS---------\n\n");
		printf("Preparing slb2... ");
		ret = skbricWork(0);
		if (ret != 0) {
			printf("error: 0x%X\n", ret);
			erroff();
			return 0;
		}
		printf("ok!\n");
		printf("Preparing os0...  ");
		ret = skbricWork(1);
		if (ret != 0) {
			printf("error: 0x%X\n", ret);
			erroff();
			return 0;
		}
		printf("ok!\n");
		printf("Preparing vs0...  ");
		ret = skbricWork(2);
		if (ret != 0) {
			printf("error: 0x%X\n", ret);
			erroff();
			return 0;
		}
		printf("ok!\n");
		printf("\nALL DONE, rebooting in 5s\n");
		sceKernelDelayThread(5 * 1000 * 1000);
		scePowerRequestColdReset();
	} else if (mod_id < 0 && only_chk == 2 && skbricWork(69) == 34) {
		printf("\nUPDATE_MODE: SC\n");
		wait_key_press();
		printf("\n---------STAGE 2: SC_INIT---------\n\n");
		printf("Changing SNVS firmware...");
		ret = skbricWork(53);
		if (ret != 0) {
			printf("error: 0x%X\n", ret);
			erroff();
			return 0;
		}
		printf("ok!\n");
		printf("\nALL DONE, rebooting in 5s\n");
		sceKernelDelayThread(5 * 1000 * 1000);
		scePowerRequestColdReset();
	}
	return 0;
}
