void rpoint_get_pkey(int mode)
{
	SceCtrlData pad;
	COLORPRINTF(COLOR_YELLOW, "Press %s.\n", (mode) ? "\n [START] to flash the EMMC\n [CIRCLE] to exit the installer\n" : "CIRCLE to reboot");
	while (1)
	{
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons & SCE_CTRL_CIRCLE)
		{
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

int rpoint_get_dsel(void)
{
	SceCtrlData pad;
	COLORPRINTF(COLOR_YELLOW, "Press\n [START] to dump the whole EMMC\n [SQUARE] to dump without user storages\n [CIRCLE] to exit");
	while (1)
	{
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons & SCE_CTRL_CIRCLE)
			sceKernelExitProcess(0);
		else if (pad.buttons & SCE_CTRL_START)
			return 1;
		else if (pad.buttons & SCE_CTRL_SQUARE)
			return 2;
		sceKernelDelayThread(200 * 1000);
	}
}

int create_full(const char *fwrpoint)
{
	int opret = 1;
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------STAGE 1: SET_TARGET---------\n\n");
	main_check_stop(opret);
	if (fwrpoint == NULL)
		fwrpoint = "ux0:data/fwtool/fwrpoint.bin";
	else
	{
		sceClibStrncpy(src_u, fwrpoint, 63);
		if (fwtool_talku(14, (int)src_u) < 0)
			return opret;
	}
	printf("New firmware restore point: %s\n", fwrpoint);
	int crt_mode = rpoint_get_dsel();

	opret = 2;
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------STAGE 2: DDUMP_EMMC---------\n\n");
	main_check_stop(opret);
	printf("dumping emmc to %s, this may take a longer while...\n", fwrpoint);
	if (fwtool_rw_emmcimg(crt_mode) < 0)
		return opret;

	opret = 3;
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------STAGE 3: ADD_CID_16---------\n\n");
	main_check_stop(opret);
	printf("checking header...\n");
	emmcimg_super img_super;
	int fd = sceIoOpen(fwrpoint, SCE_O_RDONLY, 0);
	int ret = sceIoRead(fd, &img_super, sizeof(emmcimg_super));
	sceIoClose(fd);
	if (ret < 0 || img_super.magic != 0xC00F2020)
		return opret;
	printf("getting console id...\n");
	if (_vshSblAimgrGetConsoleId(&img_super.target) < 0)
		return opret;
	printf("updating header...\n");
	fd = sceIoOpen(fwrpoint, SCE_O_WRONLY, 6);
	ret = sceIoWrite(fd, &img_super, sizeof(emmcimg_super));
	sceIoClose(fd);
	if (ret < 0)
		return opret;

	return 0;
}

int restore_full(const char *fwrpoint)
{
	int opret = 1;
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------STAGE 1: SET_SOURCE---------\n\n");
	main_check_stop(opret);
	if (fwrpoint == NULL)
		fwrpoint = "ux0:data/fwtool/fwrpoint.bin";
	else
	{
		sceClibStrncpy(src_u, fwrpoint, 63);
		if (fwtool_talku(14, (int)src_u) < 0)
			return opret;
	}
	printf("Firmware restore point: %s\n", fwrpoint);
	rpoint_get_pkey(1);

	opret = 2;
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------STAGE 2: CHK_HEADER---------\n\n");
	main_check_stop(opret);
	printf("checking header...\n");
	emmcimg_super img_super;
	int fd = sceIoOpen(fwrpoint, SCE_O_RDONLY, 0);
	int ret = sceIoRead(fd, &img_super, sizeof(emmcimg_super));
	sceIoClose(fd);
	if (ret < 0 || img_super.magic != 0xC00F2020)
		return opret;
	printf("comparing target...\n");
	char cid[0x10];
	sceClibMemset(cid, 0, 0x10);
	if (!skip_int_chk && (_vshSblAimgrGetConsoleId(cid) < 0 || sceClibMemcmp(cid, &img_super.target, 0x10) != 0))
		return opret;

	opret = 3;
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------STAGE 3: RI_SC_INIT---------\n\n");
	main_check_stop(opret);
	printf("Bypassing firmware checks on stage 2 loader...");
	if (fwtool_unlink() < 0)
		return opret;

	opret = 4;
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------STAGE 4: WRITE_EMMC---------\n\n");
	main_check_stop(opret);
	printf("writing %s to emmc, this may take a longer while...\n", fwrpoint);
	if (fwtool_rw_emmcimg(0) < 0)
		return opret;

	opret = 0;
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------ERESTORE FINISHED!---------\n\n");
	main_check_stop(6);
	if (redir_writes)
		return 0;
	printf("Removing ux0:id.dat...\n");
	sceIoRemove("ux0:id.dat");

	return 0;
}

int create_proxy(void)
{
	psvDebugScreenClear(COLOR_BLACK);
	printf("FWTOOL::CRTOOL started\n");

	int ret = create_full(NULL);

	if (ret == 0)
	{
		COLORPRINTF(COLOR_CYAN, "\nALL DONE. ");
		rpoint_get_pkey(0);
		sceKernelDelayThread(1 * 1000 * 1000);
		sceKernelExitProcess(0);
	}

	COLORPRINTF(COLOR_RED, "\nERROR AT STAGE %d !\n", ret);
	rpoint_get_pkey(0);
	sceKernelDelayThread(1 * 1000 * 1000);
	sceKernelExitProcess(0);
	return -1;
}

int restore_proxy(void)
{
	psvDebugScreenClear(COLOR_BLACK);
	printf("FWTOOL::RITOOL started\n");

	int ret = restore_full(NULL);

	if (ret == 0)
	{
		COLORPRINTF(COLOR_CYAN, "\nALL DONE. ");
		rpoint_get_pkey(0);
		sceKernelDelayThread(1 * 1000 * 1000);
		sceKernelExitProcess(0);
	}

	COLORPRINTF(COLOR_RED, "\nERROR AT STAGE %d !\n", ret);
	rpoint_get_pkey(0);
	sceKernelDelayThread(1 * 1000 * 1000);
	sceKernelExitProcess(0);
	return -1;
}