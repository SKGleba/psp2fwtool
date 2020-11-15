#define CHUNK_SIZE 64 * 1024
#define hasEndSlash(path) (path[strlen(path) - 1] == '/')

void fwimg_get_pkey(int mode)
{
	SceCtrlData pad;
	COLORPRINTF(COLOR_YELLOW, "Press %s.\n", (mode) ? "\n [START] to flash the fwimage\n [CIRCLE] to exit the installer\n" : "CIRCLE to reboot");
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

int fcp(const char *src, const char *dst)
{
	DBG("Copying %s -> %s (file)... ", src, dst);
	int res;
	SceUID fdsrc = -1, fddst = -1;
	void *buf = NULL;

	res = fdsrc = sceIoOpen(src, SCE_O_RDONLY, 0);
	if (res < 0)
		goto err;

	res = fddst = sceIoOpen(dst, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (res < 0)
		goto err;

	buf = memalign(4096, CHUNK_SIZE);
	if (!buf)
	{
		res = -1;
		goto err;
	}

	do
	{
		res = sceIoRead(fdsrc, buf, CHUNK_SIZE);
		if (res > 0)
			res = sceIoWrite(fddst, buf, res);
	} while (res > 0);

err:
	if (buf)
		free(buf);
	if (fddst >= 0)
		sceIoClose(fddst);
	if (fdsrc >= 0)
		sceIoClose(fdsrc);
	DBG("%s\n", (res < 0) ? "FAILED" : "OK");
	return res;
}

int copyDir(const char *src_path, const char *dst_path)
{
	SceUID dfd = sceIoDopen(src_path);
	if (dfd >= 0)
	{
		DBG("Copying %s -> %s (dir)\n", src_path, dst_path);
		SceIoStat stat;
		sceClibMemset(&stat, 0, sizeof(SceIoStat));
		sceIoGetstatByFd(dfd, &stat);

		stat.st_mode |= SCE_S_IWUSR;

		sceIoMkdir(dst_path, 6);

		int res = 0;

		do
		{
			SceIoDirent dir;
			sceClibMemset(&dir, 0, sizeof(SceIoDirent));

			res = sceIoDread(dfd, &dir);
			if (res > 0)
			{
				char *new_src_path = malloc(strlen(src_path) + strlen(dir.d_name) + 2);
				snprintf(new_src_path, 1024, "%s%s%s", src_path, hasEndSlash(src_path) ? "" : "/", dir.d_name);

				char *new_dst_path = malloc(strlen(dst_path) + strlen(dir.d_name) + 2);
				snprintf(new_dst_path, 1024, "%s%s%s", dst_path, hasEndSlash(dst_path) ? "" : "/", dir.d_name);

				int ret = 0;

				if (SCE_S_ISDIR(dir.d_stat.st_mode))
				{
					ret = copyDir(new_src_path, new_dst_path);
				}
				else
				{
					ret = fcp(new_src_path, new_dst_path);
				}

				free(new_dst_path);
				free(new_src_path);

				if (ret < 0)
				{
					sceIoDclose(dfd);
					return ret;
				}
			}
		} while (res > 0);

		sceIoDclose(dfd);
	}
	else
		return fcp(src_path, dst_path);

	return 1;
}

int update_default(const char *fwimage)
{
	int opret = 1;

	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------STAGE 1: FS_INIT---------\n\n");
	main_check_stop(opret);
	if (fwimage == NULL)
		fwimage = "ux0:data/fwtool/fwimage.bin";
	else
	{
		sceClibStrncpy(src_u, fwimage, 63);
		if (fwtool_talku(0, (int)src_u) < 0)
			goto err;
	}
	printf("Firmware image: %s\n", fwimage);
	pkg_toc fwimg_toc;
	SceUID fd = sceIoOpen(fwimage, SCE_O_RDONLY, 0);
	if (fd < 0)
		goto err;
	int ret = sceIoRead(fd, (void *)&fwimg_toc, sizeof(pkg_toc));
	sceIoClose(fd);
	printf("Image magic: 0x%X exp 0xcafebabe\nImage version: %d\n", fwimg_toc.magic, fwimg_toc.version);
	if (ret < 0 || fwimg_toc.magic != 0xcafebabe || fwimg_toc.version != 2)
		goto err;
	uint8_t target = fwimg_toc.target;
	if (target > 6)
		goto err;
	printf("Target: %s\n", target_dev[target]);
	if (!skip_int_chk && !fwtool_talku(6, target) && target < 5)
		goto err;
	printf("FS_PART count: %d\n", fwimg_toc.fs_count);
	if (fwimg_toc.fs_count == 0)
		goto err;
	fwimg_get_pkey(1);

	int verif_bl = 0;
	pkg_fs_etr fs_entry;
	if (target < 6)
	{
		opret = 2;
		psvDebugScreenClear(COLOR_BLACK);
		COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
		COLORPRINTF(COLOR_CYAN, "\n---------STAGE 2: SC_INIT---------\n\n");
		main_check_stop(opret);
		printf("Bypassing firmware checks on stage 2 loader...");
		if (fwtool_unlink() < 0)
			goto err;

		opret = 3;
		psvDebugScreenClear(COLOR_BLACK);
		COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
		COLORPRINTF(COLOR_CYAN, "\n---------STAGE 3: PREP_BL---------\n\n");
		main_check_stop(opret);
		fd = sceIoOpen(fwimage, SCE_O_RDONLY, 0);
		ret = sceIoPread(fd, &fs_entry, sizeof(pkg_fs_etr), sizeof(pkg_toc) + (fwimg_toc.bl_fs_no * sizeof(pkg_fs_etr)));
		sceIoClose(fd);
		printf("Checking bootloader FS_PART nfo (%d)...\n", fwimg_toc.bl_fs_no);
		DBG("\nFS_PART[%d] - magic 0x%04X | type %d\n"
			" READ: size 0x%X | offset 0x%X | ungzip %d\n"
			" WRITE: size 0x%X | offset 0x%X @ id %d\n"
			" PART_CRC32: 0x%08X | skip crc32 checks %d\n",
			fwimg_toc.bl_fs_no, fs_entry.magic, fs_entry.type,
			fs_entry.pkg_sz, fs_entry.pkg_off, (fs_entry.pkg_sz < fs_entry.dst_sz),
			fs_entry.dst_sz, fs_entry.dst_off, fs_entry.part_id,
			fs_entry.crc32, skip_int_chk);
		if (ret < 0 || fs_entry.magic != 0xAA12)
			goto err;
		if (fs_entry.type == 1)
		{
			printf("Reading the new bootloaders...\n");
			if (fwtool_read_fwimage(fs_entry.pkg_off, fs_entry.pkg_sz, fs_entry.crc32, (fs_entry.pkg_sz == fs_entry.dst_sz) ? 0 : fs_entry.dst_sz) < 0)
				goto err;
			printf("Personalizing the new bootloaders...\n");
			if (fwtool_personalize_bl(1) < 0)
				goto err;
			verif_bl = 1;
			printf("Updating the SHA256 in SNVS...\n");
			if (fwtool_talku(11, 0) < 0)
				goto err;
		}
		else
		{
			printf("Could not find any bootloader entry!\nContinue anyways?\n");
			sceKernelDelayThread(1000 * 1000);
			fwimg_get_pkey(1);
		}
	}

	opret = 4;
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------STAGE 4: FLASHFS---------\n\n");
	uint8_t ecount = 0;
	int swap_os = 0, swap_bl = 0, use_e2x = 0;
	uint32_t off = sizeof(pkg_toc);
	while (ecount < fwimg_toc.fs_count)
	{
		DBG("getting entry %d (0x%X)\n", ecount, off);
		main_check_stop(opret);
		fd = sceIoOpen(fwimage, SCE_O_RDONLY, 0);
		ret = sceIoPread(fd, &fs_entry, sizeof(pkg_fs_etr), off);
		sceIoClose(fd);
		if (ret < 0 || fs_entry.magic != 0xAA12)
			goto err;
		printf("Installing %s (R", (fs_entry.type == 2) ? "e2x" : pcode_str[fs_entry.part_id]);
		DBG("\nFS_PART[%d] - magic 0x%04X | type %d\n"
			" READ: size 0x%X | offset 0x%X | ungzip %d\n"
			" WRITE: size 0x%X | offset 0x%X @ id %d\n"
			" PART_CRC32: 0x%08X | skip crc32 checks %d\n",
			ecount, fs_entry.magic, fs_entry.type,
			fs_entry.pkg_sz, fs_entry.pkg_off, (fs_entry.pkg_sz < fs_entry.dst_sz),
			fs_entry.dst_sz, fs_entry.dst_off, fs_entry.part_id,
			fs_entry.crc32, skip_int_chk);
		if (fwtool_read_fwimage(fs_entry.pkg_off, fs_entry.pkg_sz, fs_entry.crc32, (fs_entry.pkg_sz == fs_entry.dst_sz) ? 0 : fs_entry.dst_sz) < 0)
			goto err;
		ret = -1;
		printf("W @ 0x%X)... ", fs_entry.dst_off);
		if (fs_entry.type == 0)
			ret = fwtool_write_partition(fs_entry.dst_off, fs_entry.dst_sz, fs_entry.part_id);
		else if (fs_entry.type > 0 && target == 6)
			ret = 0;
		else if (fs_entry.type == 1 && fs_entry.dst_off == 0 && verif_bl)
		{
			if (fwtool_talku(7, 0))
				ret = fwtool_write_partition(0, fs_entry.dst_sz, 2);
		}
		else if (fs_entry.type == 2 && fs_entry.dst_off == 0x400)
			ret = fwtool_flash_e2x(fs_entry.dst_sz);
		if (ret < 0)
			goto err;
		COLORPRINTF(COLOR_YELLOW, "ok!\n");
		if (fs_entry.type == 0 && fs_entry.part_id == 3)
			swap_os = 1;
		else if (fs_entry.type == 1)
			swap_bl = 1;
		if (fs_entry.type == 2)
			use_e2x = 1;
		ecount -= -1;
		off -= -sizeof(pkg_fs_etr);
	}

	if (target < 6)
	{
		opret = 5;
		psvDebugScreenClear(COLOR_BLACK);
		COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
		COLORPRINTF(COLOR_CYAN, "\n---------STAGE 5: UPD_MBR---------\n\n");
		main_check_stop(opret);
		printf("Swap os0: %d\nSwap slb2: %d\nEnable enso: %d\nUpdating the MBR...\n", swap_os, swap_bl, use_e2x);
		if (fwtool_update_mbr(use_e2x, swap_bl, swap_os) < 0)
			goto err;
	}

	opret = 0;
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------UPDATE FINISHED!---------\n\n");
	main_check_stop(6);
	if (redir_writes)
		goto err;
	printf("Removing ux0:id.dat... ");
	ret = sceIoRemove("ux0:id.dat");
	printf("0x%X\nRemoving ux0:tai/config.txt... ", ret);
	ret = sceIoRemove("ux0:tai/config.txt");
	printf("0x%X\nApplying custom partition patches:\n - os0... ", ret);
	ret = -1;
	sceClibMemcpy(src_u, "sdstor0:int-lp-ina-os", sizeof("sdstor0:int-lp-ina-os"));
	if (fwtool_talku(10, (int)src_u) >= 0)
		ret = copyDir("ux0:data/fwtool/os0-patch", "grw0:");
	printf("0x%X\n - vs0... ", ret);
	ret = -1;
	if (fwtool_talku(8, 0x300) >= 0)
		ret = copyDir("ux0:data/fwtool/vs0-patch", "vs0:");
	printf("0x%X\n - ur0... ", ret);
	ret = copyDir("ux0:data/fwtool/ur0-patch", "ur0:");
	printf("0x%X\n", ret);
err:
	return opret;
}

int update_proxy(void)
{
	psvDebugScreenClear(COLOR_BLACK);
	printf("FWTOOL::FLASHTOOL started\n");

	int ret = update_default(NULL);
	if (ret == 0)
	{
		COLORPRINTF(COLOR_CYAN, "\nALL DONE. ");
		fwimg_get_pkey(0);
		sceKernelDelayThread(1 * 1000 * 1000);
		sceKernelExitProcess(0);
	}

	COLORPRINTF(COLOR_RED, "\nERROR AT STAGE %d !\n", ret);
	fwimg_get_pkey(0);
	sceKernelDelayThread(1 * 1000 * 1000);
	sceKernelExitProcess(0);
	return -1;
}