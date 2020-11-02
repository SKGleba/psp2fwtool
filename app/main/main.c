#include <stdio.h>
#include <stdlib.h> 
#include <taihen.h>
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include "debugScreen.h"
	
#define printf(...) psvDebugScreenPrintf(__VA_ARGS__)
#define COLORPRINTF(color, ...)		\
do {                                \
	psvDebugScreenSetFgColor(color);\
	psvDebugScreenPrintf(__VA_ARGS__);\
	psvDebugScreenSetFgColor(COLOR_WHITE);\
} while (0)
	
#define FWTOOL_VERSION_STR "FWTOOL v0.8 (beta) by SKGleba"
	
const char mmit[5][32] = {" -> Flash a firmware image"," -> Create a restore image"," -> Restore a firmware"," -> Exit"};
int optct = 4;

void erroff() {
	COLORPRINTF(COLOR_RED, "ERR_REQ_OFF\n");
	SceCtrlData pad;
	COLORPRINTF(COLOR_YELLOW, "Press %s.\n", "CROSS to reset or CIRCLE to exit");
	while (1) {
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons & SCE_CTRL_CIRCLE)
			break;
		if (pad.buttons & SCE_CTRL_CROSS) {
			scePowerRequestColdReset();
			break;
		}
		sceKernelDelayThread(200 * 1000);
	}
	sceKernelExitProcess(0);
}

void smenu(int sel){
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_CYAN, FWTOOL_VERSION_STR "\n");
	for(int i = 0; i < optct; i++){
		if(sel==i){
			psvDebugScreenSetFgColor(COLOR_BLUE);
		}
		printf("%s\n", mmit[i]);
		psvDebugScreenSetFgColor(COLOR_WHITE);
	}
	psvDebugScreenSetFgColor(COLOR_WHITE);
}

static int launch_submodule(int sid) {
	
	if (sid == 3) {
		sceKernelExitProcess(0);
		return 0;
	}
	
	printf("\nLoading the supply module...\n");
	int mod_id = 0;
	tai_module_args_t argg;
	argg.size = sizeof(argg);
	argg.pid = KERNEL_PID;
	argg.args = 0;
	argg.argp = NULL;
	argg.flags = 0;
	
	// load fwtool kernel
	if (taiLoadStartKernelModuleForUser("ux0:app/SKGFWT00L/fwtool.skprx", &argg) < 0)
		return -1;
	
	printf("Launching the installer...\n");
	
	// launch the correct main module
	switch(sid) {
		case 0: // fwtool original
			sceAppMgrLoadExec("app0:fwimg.bin", NULL, NULL);
			break;
		case 1: // fwcreate
			sceAppMgrLoadExec("app0:fwcrt.bin", NULL, NULL);
			break;
		case 2: // fwrestore
			sceAppMgrLoadExec("app0:fwres.bin", NULL, NULL);
			break;
		default:
			break;
	}
	
	return 0;
}

int main(int argc, char *argv[])
{
	psvDebugScreenInit();
	
	smenu(0);
	
	int sel = 0;
	SceCtrlData pad;
	while (1) {
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons == SCE_CTRL_CROSS)
			break;
		else if (pad.buttons == SCE_CTRL_UP) {
			if(sel!=0)
				sel--;
			smenu(sel);
			sceKernelDelayThread(0.3 * 1000 * 1000);
		} else if (pad.buttons == SCE_CTRL_DOWN) {
			if(sel+1<optct)
				sel++;
			smenu(sel);
			sceKernelDelayThread(0.3 * 1000 * 1000);
		}
	}
	
	if (launch_submodule(sel) < 0)
		erroff();
	return 0;
}
