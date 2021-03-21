/* THIS FILE IS A PART OF PSP2FWTOOL
 *
 * Copyright (C) 2019-2021 skgleba
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

void dualos_get_pkey(int mode) {
	SceCtrlData pad;
	COLORPRINTF(COLOR_YELLOW, "Press %s.\n", (mode) ? "\n [START] to flash the EMMC\n [CIRCLE] to exit the installer\n" : "CIRCLE to reboot");
	while (1) {
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons & SCE_CTRL_CIRCLE) {
			if (mode)
				sceKernelExitProcess(0);
			else
				scePowerRequestColdReset();
		}
		if ((pad.buttons & SCE_CTRL_START) && mode == 1)
			break;
		sceKernelDelayThread(200 * 1000);
	}
}

int swap_full(void) {
	int opret = 1, osmode = *(uint32_t*)(check_dos_br + 12);
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------STAGE 1: SET_TARGET---------\n\n");
	printf("Target: %s\n", osmode ? "slaveOS" : "masterOS");
	dualos_get_pkey(1);

	opret = 2;
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------STAGE 2: RI_SC_INIT---------\n\n");
	main_check_stop(opret);
	printf("Bypassing firmware checks on stage 2 loader...");
	if (fwtool_unlink() < 0)
		return opret;

	opret = 3;
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------STAGE 3: WRITE_EMMC---------\n\n");
	main_check_stop(opret);
	printf("writing %s boot record to emmc...\n", osmode ? "slaveOS" : "masterOS");
	if (fwtool_dualos_swap() < 0)
		return opret;

	opret = 0;
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------cOS SWAP FINISHED!---------\n\n");
	main_check_stop(6);
	
	printf("Removing ux0:id.dat...\n");
	sceIoRemove("ux0:id.dat");

	return 0;
}

int install_proxy(void) {
	psvDebugScreenClear(COLOR_BLACK);
	printf("FWTOOL::DOSITOOL started\n");
	
	int ret = fwtool_talku(19, 0);
	DBG("set high perf mode: 0x%X\n", ret);
	
	dualos_get_pkey(1);
	
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------STAGE X: WRITE_EMMC---------\n\n");
	printf("installing dualOS, this may take a while...\n");
	
	ret = fwtool_dualos_create();

	if (ret == 0) {
		COLORPRINTF(COLOR_CYAN, "\nALL DONE. ");
		dualos_get_pkey(0);
		sceKernelDelayThread(1 * 1000 * 1000);
		sceKernelExitProcess(0);
	}

	COLORPRINTF(COLOR_RED, "\nERROR INSTALLING DUALOS: 0x%X!\n", ret);
	dualos_get_pkey(0);
	sceKernelDelayThread(1 * 1000 * 1000);
	sceKernelExitProcess(0);
	return -1;
}

int swap_proxy(void) {
	psvDebugScreenClear(COLOR_BLACK);
	printf("FWTOOL::DOSCTOOL started\n");
	
	int ret = swap_full();

	if (ret == 0) {
		COLORPRINTF(COLOR_CYAN, "\nALL DONE. ");
		dualos_get_pkey(0);
		sceKernelDelayThread(1 * 1000 * 1000);
		sceKernelExitProcess(0);
	}

	COLORPRINTF(COLOR_RED, "\nERROR AT STAGE %d !\n", ret);
	dualos_get_pkey(0);
	sceKernelDelayThread(1 * 1000 * 1000);
	sceKernelExitProcess(0);
	return -1;
}