/* THIS FILE IS A PART OF PSP2FWTOOL
 *
 * Copyright (C) 2019-2022 skgleba
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#define CHUNK_SIZE 64 * 1024
#define hasEndSlash(path) (path[strlen(path) - 1] == '/')

void fwimg_get_pkey(int mode) {
	SceCtrlData pad;
	COLORPRINTF(COLOR_YELLOW, "Press %s.\n", (mode) ? "\n [START] to install the fwimage\n [CIRCLE] to exit the installer\n" : "CIRCLE to reboot");
	while (1) {
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons & SCE_CTRL_CIRCLE) {
			if (mode)
				sceKernelExitProcess(0);
			else
				fwtool_talku(CMD_REBOOT, 0);
		}
		if ((pad.buttons & SCE_CTRL_START) && mode == 1)
			break;
		sceKernelDelayThread(100 * 1000);
	}
}

int fcp(const char* src, const char* dst) {
	DBG("Copying %s -> %s (file)... ", src, dst);
	int res;
	SceUID fdsrc = -1, fddst = -1;
	void* buf = NULL;

	res = fdsrc = sceIoOpen(src, SCE_O_RDONLY, 0);
	if (res < 0)
		goto err;

	res = fddst = sceIoOpen(dst, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (res < 0)
		goto err;

	buf = memalign(4096, CHUNK_SIZE);
	if (!buf) {
		res = -1;
		goto err;
	}

	do {
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

int copyDir(const char* src_path, const char* dst_path) {
	SceUID dfd = sceIoDopen(src_path);
	if (dfd >= 0) {
		DBG("Copying %s -> %s (dir)\n", src_path, dst_path);
		SceIoStat stat;
		sceClibMemset(&stat, 0, sizeof(SceIoStat));
		sceIoGetstatByFd(dfd, &stat);

		stat.st_mode |= SCE_S_IWUSR;

		sceIoMkdir(dst_path, 6);

		int res = 0;

		do {
			SceIoDirent dir;
			sceClibMemset(&dir, 0, sizeof(SceIoDirent));

			res = sceIoDread(dfd, &dir);
			if (res > 0) {
				char* new_src_path = malloc(strlen(src_path) + strlen(dir.d_name) + 2);
				sceClibSnprintf(new_src_path, 1024, "%s%s%s", src_path, hasEndSlash(src_path) ? "" : "/", dir.d_name);

				char* new_dst_path = malloc(strlen(dst_path) + strlen(dir.d_name) + 2);
				sceClibSnprintf(new_dst_path, 1024, "%s%s%s", dst_path, hasEndSlash(dst_path) ? "" : "/", dir.d_name);

				int ret = 0;

				if (SCE_S_ISDIR(dir.d_stat.st_mode)) {
					ret = copyDir(new_src_path, new_dst_path);
				} else {
					ret = fcp(new_src_path, new_dst_path);
				}

				free(new_dst_path);
				free(new_src_path);

				if (ret < 0) {
					sceIoDclose(dfd);
					return ret;
				}
			}
		} while (res > 0);

		sceIoDclose(dfd);
	} else
		return fcp(src_path, dst_path);

	return 1;
}

// Install [fwimage] (ux0:data/fwtool/fwimage.bin if NULL)
int update_default(const char* fwimage, int ud0_pathdir, uint32_t fwimg_start_offset) {
	/*
		Stage 1 - basic image checks
		- Errors if no file/no FS_PARTs
		- Errors if bad magic/version
		- Errors if bad target
		- Errors if wrong hardware target
		- Errors if wrong firmware target
		- Errors if failed the TOC checksum verify
		- Asks the user one last time to confirm flash
	*/
	int opret = 1;
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------STAGE 1: FS_INIT---------\n\n");
	main_check_stop(opret);
	if (!fwimage)
		fwimage = "ux0:data/fwtool/" CFWIMG_NAME;
	else {
		sceClibStrncpy(src_u, fwimage, 63);
		if (fwtool_talku(CMD_SET_FWIMG_PATH, (int)src_u) < 0)
			goto err;
	}
	DBG("running pre-install checks\n");
	printf("Firmware image: %s [0x%X]\n", fwimage, fwimg_start_offset);
	pkg_toc fwimg_toc;
	SceUID fd = sceIoOpen(fwimage, SCE_O_RDONLY, 0);
	if (fd < 0)
		goto err;
	int ret = sceIoPread(fd, (void*)&fwimg_toc, sizeof(pkg_toc), fwimg_start_offset);
	sceIoClose(fd);
	DBG("Image magic: 0x%08X exp 0x%08X\nImage version: %d\n", fwimg_toc.magic, CFWIMG_MAGIC, fwimg_toc.version);
	if (ret < 0 || fwimg_toc.magic != CFWIMG_MAGIC || fwimg_toc.version != CFWIMG_VERSION)
		goto err;
	uint8_t target = fwimg_toc.target;
	if (target > FWTARGET_SAFE)
		goto err;
	printf("Target: %s\n", target_dev[target]);
	if (!skip_int_chk && !fwtool_talku(CMD_CMP_TARGET, target) && target < FWTARGET_ALL)
		goto err;
	if (fwimg_toc.fw_version) {
		printf("Firmware: 0x%08X\n", fwimg_toc.fw_version);
		if (!skip_int_chk && fwtool_check_rvk(FSPART_TYPE_FS, SCEMBR_PART_EMPTY, fwimg_toc.fw_version & -0x100, 0))
			goto err;
	}
	if (fwimg_toc.target_hw_rev) {
		printf("Hardware: 0x%08X\n", fwimg_toc.target_hw_rev);
		uint32_t current_hw = fwtool_talku(CMD_GET_HW_REV, 0);
		if (!skip_int_chk && (current_hw & fwimg_toc.target_hw_mask) != (fwimg_toc.target_hw_rev & fwimg_toc.target_hw_mask))
			goto err;
	}
	if (fwimg_toc.target_min_fw) {
		printf(" - minimum allowed firmware: 0x%08X\n", fwimg_toc.target_min_fw);
		if (!skip_int_chk && (fwtool_talku(CMD_GET_CURRENT_FWV, 0) < fwimg_toc.target_min_fw))
			goto err;
	}
	if (fwimg_toc.target_max_fw) {
		printf(" - maximum allowed firmware: 0x%08X\n", fwimg_toc.target_max_fw);
		if (!skip_int_chk && (fwtool_talku(CMD_GET_CURRENT_FWV, 0) > fwimg_toc.target_max_fw))
			goto err;
	}
	if (fwimg_toc.target_require_enso) {
		printf(" - enso_ex is required\n");
		if (!skip_int_chk && !fwtool_talku(CMD_GET_ENSO_STATUS, 0))
			goto err;
	}
	if (fwimg_toc.force_component_update) {
		printf(" - force component update\n");
		if (!fwtool_talku(CMD_FORCE_DEV_UPDATE, 0))
			fwtool_talku(CMD_FORCE_DEV_UPDATE, 0);
	}
	if (fwimg_toc.use_file_logging) {
		printf(" - use file logging\n");
		if (!fwtool_talku(CMD_SET_FILE_LOGGING, 0))
			fwtool_talku(CMD_SET_FILE_LOGGING, 0);
	}
	DBG("FS_PART count: %d\n", fwimg_toc.fs_count);
	if (!fwimg_toc.fs_count)
		goto err;
	if (fwimg_toc.build_info[0])
		printf("Description: %s\n", fwimg_toc.build_info);
	if (fwimg_toc.toc_crc32) {
		DBG("verifying TOC checksum\n");
		uint32_t exp_toc_crc = fwimg_toc.toc_crc32;
		fwimg_toc.toc_crc32 = 0;
		uint32_t act_toc_crc = crc32(0, (void*)&fwimg_toc, sizeof(pkg_toc));
		DBG("TOTOC crc 0x%08X\n", act_toc_crc);
		unsigned char tmp_toc_buf[sizeof(pkg_fs_etr)];
		fd = sceIoOpen(fwimage, SCE_O_RDONLY, 0);
		if (fd < 0)
			goto err;
		for (int i = 0; i < fwimg_toc.fs_count; i -= -1) {
			sceIoPread(fd, tmp_toc_buf, sizeof(pkg_fs_etr), fwimg_start_offset + sizeof(pkg_toc) + (i * sizeof(pkg_fs_etr)));
			act_toc_crc = crc32(act_toc_crc, tmp_toc_buf, sizeof(pkg_fs_etr));
		}
		sceIoClose(fd);
		if (act_toc_crc != exp_toc_crc) {
			DBG("checksum mismatch! t: 0x%X | c: 0x%X\n", exp_toc_crc, act_toc_crc);
			printf("TOC checksum mismatch!\n");
			goto err;
		}
	}
	ret = fwtool_talku(CMD_SET_PERF_MODE, 0);
	DBG("set high perf mode: 0x%X\n", ret);
	fwimg_get_pkey(1);

	int verif_bl = 0, update_dev = 0;
	pkg_fs_etr fs_entry;
	if (target < FWTARGET_SAFE) {
		/*
			Stage 2 - Prepare SLSK
			- Puts SLSK in separate memblock and personalizes them
			- Compares the ARM KBL fw with console minfw to avoid bricks
			- Updates the SLSK sha256 in SNVS for the inactive bank
			> TODO: check bl2 fw instead to allow ARM KBL mods
		*/
		opret = 2;
		psvDebugScreenClear(COLOR_BLACK);
		COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
		COLORPRINTF(COLOR_CYAN, "\n---------STAGE 2: PREP_BL---------\n\n");
		main_check_stop(opret);
		fd = sceIoOpen(fwimage, SCE_O_RDONLY, 0);
		ret = sceIoPread(fd, &fs_entry, sizeof(pkg_fs_etr), fwimg_start_offset + sizeof(pkg_toc) + (fwimg_toc.bl_fs_no * sizeof(pkg_fs_etr)));
		sceIoClose(fd);
		printf("Checking bootloader FS_PART nfo (%d)...\n", fwimg_toc.bl_fs_no);
		DBG("\nFS_PART[%d] - magic 0x%04X | type %d\n"
			" READ: size 0x%X | offset 0x%X | ungzip %d\n"
			" WRITE: size 0x%X | offset 0x%X @ id %d\n"
			" CHECK: hdr2 0x%X | hdr3 0x%X\n"
			" PART_CRC32: 0x%08X | skip crc32 checks %d\n",
			fwimg_toc.bl_fs_no, fs_entry.magic, fs_entry.type,
			fs_entry.pkg_sz, fwimg_start_offset + fs_entry.pkg_off, (fs_entry.pkg_sz < fs_entry.dst_sz),
			fs_entry.dst_sz, fs_entry.dst_off, fs_entry.part_id,
			fs_entry.hdr2, fs_entry.hdr3,
			fs_entry.crc32, skip_int_chk);
		if (ret < 0 || fs_entry.magic != FSPART_MAGIC || fs_entry.part_id > SCEMBR_PART_UNUSED)
			goto err;
		if (fs_entry.type == FSPART_TYPE_BL) {
			printf("Reading the new bootloaders...\n");
			if (fwtool_read_fwimage(fwimg_start_offset + fs_entry.pkg_off, fs_entry.pkg_sz, fs_entry.crc32, (fs_entry.pkg_sz == fs_entry.dst_sz) ? 0 : fs_entry.dst_sz) < 0)
				goto err;
			printf("Personalizing the new bootloaders...\n");
			if (fwtool_personalize_bl(1) < 0)
				goto err;
			printf("Checking the minfwv vs ARM KBL fwv...\n");
			if (!skip_int_chk && fwtool_talku(CMD_VALIDATE_KBLFW, 0) < 0)
				goto err;
			verif_bl = 1;
			printf("Updating the SHA256 in SNVS...\n");
			if (fwtool_talku(CMD_SET_INACTIVE_BL_SHA256, 0) < 0)
				goto err;
		} else {
			printf("Could not find any bootloader entry!\nSkipping stage 3\n");
			sceKernelDelayThread(1000 * 1000);
		}

		if (verif_bl) {
			/*
				Stage 3 - patched syscon_init()
				This sets system firmware version to 0xDEADBEEF which makes stage 2 loader use its hardcoded ver
			*/
			opret = 3;
			psvDebugScreenClear(COLOR_BLACK);
			COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
			COLORPRINTF(COLOR_CYAN, "\n---------STAGE 3: SC_INIT---------\n\n");
			main_check_stop(opret);
			printf("Bypassing firmware checks on stage 2 loader...");
			if (fwtool_unlink() < 0)
				goto err;
		}

		/*
			Stage 4 - flash criticals
			- Flashes all the critical FS_PARTs to EMMC
			- Sets update_mbr flags
		*/
		opret = 4;
		psvDebugScreenClear(COLOR_BLACK);
		COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
		COLORPRINTF(COLOR_CYAN, "\n------STAGE 4: WRITE_CRIT_FS------\n\n");
		uint8_t ecount = 0;
		int swap_os = 0, swap_bl = 0, use_e2x = 0;
		uint32_t off = sizeof(pkg_toc);
		while (ecount < fwimg_toc.fs_count) {
			DBG("getting entry %d (0x%X)\n", ecount, off);
			main_check_stop(opret);
			fd = sceIoOpen(fwimage, SCE_O_RDONLY, 0);
			ret = sceIoPread(fd, &fs_entry, sizeof(pkg_fs_etr), fwimg_start_offset + off);
			sceIoClose(fd);
			if (ret < 0 || fs_entry.magic != FSPART_MAGIC || fs_entry.part_id > SCEMBR_PART_UNUSED)
				goto err;
			if (fs_entry.type < FSPART_TYPE_DEV) { // make sure its a fs_part
				if (fs_entry.part_id < SCEMBR_PART_SYSTEM && (fs_entry.part_id != SCEMBR_PART_EMPTY || fs_entry.type == FSPART_TYPE_E2X)) { // make sure its a critical fs, we write the rest later
					printf("Installing %s (R", (fs_entry.type == FSPART_TYPE_E2X) ? "e2x" : pcode_str[fs_entry.part_id]);
					DBG("\nFS_PART[%d] - magic 0x%04X | type %d\n"
						" READ: size 0x%X | offset 0x%X | ungzip %d\n"
						" WRITE: size 0x%X | offset 0x%X @ id %d\n"
						" CHECK: hdr2 0x%X | hdr3 0x%X\n"
						" PART_CRC32: 0x%08X | skip crc32 checks %d\n",
						ecount, fs_entry.magic, fs_entry.type,
						fs_entry.pkg_sz, fwimg_start_offset + fs_entry.pkg_off, (fs_entry.pkg_sz < fs_entry.dst_sz),
						fs_entry.dst_sz, fs_entry.dst_off, fs_entry.part_id,
						fs_entry.hdr2, fs_entry.hdr3,
						fs_entry.crc32, skip_int_chk);
					if ((fs_entry.hdr2 || fs_entry.hdr3) && fwtool_check_rvk(fs_entry.type, fs_entry.part_id, fs_entry.hdr2, fs_entry.hdr3))
						goto err;
					if (fwtool_read_fwimage(fwimg_start_offset + fs_entry.pkg_off, fs_entry.pkg_sz, fs_entry.crc32, (fs_entry.pkg_sz == fs_entry.dst_sz) ? 0 : fs_entry.dst_sz) < 0)
						goto err;
					ret = -1;
					printf("W @ 0x%X)... ", fs_entry.dst_off);
					if (fs_entry.type == FSPART_TYPE_FS) // default fs
						ret = fwtool_write_partition(fs_entry.dst_off, fs_entry.dst_sz, fs_entry.part_id);
					else if (fs_entry.type == FSPART_TYPE_BL && !fs_entry.dst_off && verif_bl) { // slsk
						if (fwtool_talku(CMD_BL_TO_FSP, 0))
							ret = fwtool_write_partition(0, fs_entry.dst_sz, SCEMBR_PART_SBLS);
					} else if (fs_entry.type == FSPART_TYPE_E2X && fs_entry.dst_off == 0x400) // e2x
						ret = fwtool_flash_e2x(fs_entry.dst_sz);
					if (ret < 0)
						goto err;
					COLORPRINTF(COLOR_YELLOW, "ok!\n");
					if (fs_entry.type == FSPART_TYPE_FS && fs_entry.part_id == SCEMBR_PART_KERNEL)
						swap_os = 1;
					else if (fs_entry.type == FSPART_TYPE_BL)
						swap_bl = 1;
					else if (fs_entry.type == FSPART_TYPE_E2X)
						use_e2x = 1;
				} else
					DBG("non-critical target fs, skipping for now\n");
			} else {
				update_dev = 1;
				DBG("device firmware, skipping\n");
			}
			ecount -= -1;
			off -= -sizeof(pkg_fs_etr);
		}

		if (use_e2x | swap_bl | swap_os) {
			/*
				Stage 5 - update the Master Boot Record
				- Updates the MBR with new offsets
			*/
			opret = 5;
			psvDebugScreenClear(COLOR_BLACK);
			COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
			COLORPRINTF(COLOR_CYAN, "\n---------STAGE 5: UPD_MBR---------\n\n");
			main_check_stop(opret);
			printf("Swap os0: %d\nSwap slb2: %d\nEnable enso: %d\nUpdating the MBR...\n", swap_os, swap_bl, use_e2x);
			if (fwtool_update_mbr(use_e2x, swap_bl, swap_os) < 0)
				goto err;
		}
	}

	/*
		Stage 6 - flash remains
		- Flashes all the non-critical FS_PARTs to EMMC
		- Sets update_mbr flags
	*/
	opret = 6;
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n-------STAGE 6: WRITE_NC_FS-------\n\n");
	ret = fwtool_talku(CMD_SET_PERF_MODE, 1);
	DBG("set boost mode: 0x%X\n", ret);
	uint8_t ecount = 0;
	uint32_t off = sizeof(pkg_toc);
	while (ecount < fwimg_toc.fs_count) {
		DBG("getting entry %d (0x%X)\n", ecount, off);
		main_check_stop(opret);
		fd = sceIoOpen(fwimage, SCE_O_RDONLY, 0);
		ret = sceIoPread(fd, &fs_entry, sizeof(pkg_fs_etr), fwimg_start_offset + off);
		sceIoClose(fd);
		if (ret < 0 || fs_entry.magic != FSPART_MAGIC || fs_entry.part_id > SCEMBR_PART_UNUSED)
			goto err;
		if (fs_entry.type < FSPART_TYPE_DEV) { // make sure its a fs, set update_dev flag if not
			if (fs_entry.part_id > SCEMBR_PART_KERNEL || (fs_entry.part_id == SCEMBR_PART_EMPTY && fs_entry.type != FSPART_TYPE_E2X)) { // we flashed the criticals earlier
				if (fs_entry.part_id == SCEMBR_PART_EMPTY) {
					int e2xmisc_known_data = 0;
					for (int i = 0; i < E2X_MISC_NOTYPE; i++) {
						if (e2x_misc_type_offsets[i] == fs_entry.dst_off && e2x_misc_type_sizes[i] == fs_entry.dst_sz) {
							printf("Flashing enso_ex recovery data (R");
							e2xmisc_known_data = 1;
						}
					}
					if (!e2xmisc_known_data)
						printf("Attempting to flash unknown raw data (R");
				} else
					printf("Installing %s (R", pcode_str[fs_entry.part_id]);
				DBG("\nFS_PART[%d] - magic 0x%04X | type %d\n"
					" READ: size 0x%X | offset 0x%X | ungzip %d\n"
					" WRITE: size 0x%X | offset 0x%X @ id %d\n"
					" CHECK: hdr2 0x%X | hdr3 0x%X\n"
					" PART_CRC32: 0x%08X | skip crc32 checks %d\n",
					ecount, fs_entry.magic, fs_entry.type,
					fs_entry.pkg_sz, fwimg_start_offset + fs_entry.pkg_off, (fs_entry.pkg_sz < fs_entry.dst_sz),
					fs_entry.dst_sz, fs_entry.dst_off, fs_entry.part_id,
					fs_entry.hdr2, fs_entry.hdr3,
					fs_entry.crc32, skip_int_chk);
				if ((fs_entry.hdr2 || fs_entry.hdr3) && fwtool_check_rvk(fs_entry.type, fs_entry.part_id, fs_entry.hdr2, fs_entry.hdr3))
					goto err;
				if (fwtool_read_fwimage(fwimg_start_offset + fs_entry.pkg_off, fs_entry.pkg_sz, fs_entry.crc32, (fs_entry.pkg_sz == fs_entry.dst_sz) ? 0 : fs_entry.dst_sz) < 0)
					goto err;
				ret = -1;
				printf("W @ 0x%X)... ", fs_entry.dst_off);
				ret = fwtool_write_partition(fs_entry.dst_off, fs_entry.dst_sz, fs_entry.part_id);
				if (ret < 0)
					goto err;
				COLORPRINTF(COLOR_YELLOW, "ok!\n");
			} else
				DBG("critical target fs, skipping\n");
		} else
			DBG("device firmware, skipping\n");
		ecount -= -1;
		off -= -sizeof(pkg_fs_etr);
	}

	/*
		Stage 7 - post-update patches
		- Removes id.dat and tai config.txt from ux0
		- Copies contents of *-patch dirs to * partitions
	*/
	opret = 0;
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n----FIRMWARE UPDATE FINISHED!-----\n\n");
	main_check_stop(7);
	if (redir_writes)
		goto err;
	if (fwimg_toc.target == FWTARGET_DEVTOOL && verif_bl) {
		printf("Updating SDK version in TSMP\n");
		fwtool_talku(CMD_SET_TSMP_FWV, fwimg_toc.fw_version | 0x11);
	}
	printf("Removing ux0:id.dat... ");
	ret = sceIoRemove("ux0:id.dat");
	printf("0x%X\nRemoving ux0:tai/config.txt... ", ret);
	ret = sceIoRemove("ux0:tai/config.txt");
	printf("0x%X\nApplying custom partition patches:\n - os0... ", ret);
	ret = -1;
	sceClibMemcpy(src_u, "sdstor0:int-lp-ina-os", sizeof("sdstor0:int-lp-ina-os"));
	if (fwtool_talku(CMD_GRW_MOUNT, (int)src_u) >= 0)
		ret = copyDir((ud0_pathdir) ? "ud0:fwtool/os0-patch" : "ux0:data/fwtool/os0-patch", "grw0:");
	printf("0x%X\n - vs0... ", ret);
	ret = -1;
	if (fwtool_talku(CMD_UMOUNT, 0x300) >= 0)
		ret = copyDir((ud0_pathdir) ? "ud0:fwtool/vs0-patch" : "ux0:data/fwtool/vs0-patch", "vs0:");
	printf("0x%X\n - ur0... ", ret);
	ret = copyDir((ud0_pathdir) ? "ud0:fwtool/ur0-patch" : "ux0:data/fwtool/ur0-patch", "ur0:");
	printf("0x%X\n", ret);
	if (!update_dev || target == FWTARGET_SAFE)
		goto err;

	/*
		Stage 8 - update devices
		- Flashes device FS_PARTs to target components
	*/
	opret = 8;
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n--------STAGE 8: DEVFW_UPD--------\n\n");
	main_check_stop(opret);
	ecount = 0;
	uint32_t hdr_data[3];
	off = sizeof(pkg_toc);
	while (ecount < fwimg_toc.fs_count) {
		main_check_stop(opret);
		fd = sceIoOpen(fwimage, SCE_O_RDONLY, 0);
		ret = sceIoPread(fd, &fs_entry, sizeof(pkg_fs_etr), fwimg_start_offset + off);
		sceIoClose(fd);
		if (ret < 0 || fs_entry.magic != FSPART_MAGIC || fs_entry.part_id > SCEMBR_PART_UNUSED)
			goto err;
		if (fs_entry.type == FSPART_TYPE_DEV && fs_entry.part_id < DEV_NODEV) {
			DBG("rvkchecking entry %d (0x%02X(0x%08X, 0x%08X))\n", ecount, fs_entry.part_id, fs_entry.hdr2, fs_entry.hdr3);
			if (!(fs_entry.hdr2 || fs_entry.hdr3) || !fwtool_check_rvk(fs_entry.type, fs_entry.part_id, fs_entry.hdr2, fs_entry.hdr3)) {
				printf("Flashing %s (R", dcode_str[fs_entry.part_id]);
				DBG("\nFS_PART[%d] - magic 0x%04X | type %d\n"
					" READ: size 0x%X | offset 0x%X | ungzip %d\n"
					" WRITE: size 0x%X | device %d\n"
					" CHECK: hdr2 0x%X | hdr3 0x%X\n"
					" FIRMWARE: 0x%08X\n"
					" PART_CRC32: 0x%08X | skip crc32 checks %d\n",
					ecount, fs_entry.magic, fs_entry.type,
					fs_entry.pkg_sz, fwimg_start_offset + fs_entry.pkg_off, (fs_entry.pkg_sz < fs_entry.dst_sz),
					fs_entry.dst_sz, fs_entry.part_id,
					fs_entry.hdr2, fs_entry.hdr3,
					fs_entry.dst_off,
					fs_entry.crc32, skip_int_chk);
				if (fwtool_read_fwimage(fwimg_start_offset + fs_entry.pkg_off, fs_entry.pkg_sz, fs_entry.crc32, (fs_entry.pkg_sz == fs_entry.dst_sz) ? 0 : fs_entry.dst_sz) < 0)
					goto err;
				ret = -1;
				printf("W @ %s)... ", dcode_str[fs_entry.part_id]);
				printf("MAY TAKE A WHILE... ");
				hdr_data[0] = fs_entry.hdr2;
				hdr_data[1] = fs_entry.hdr3;
				hdr_data[2] = fs_entry.dst_off;
				ret = fwtool_update_dev(fs_entry.part_id, fs_entry.dst_sz, hdr_data);
				if (ret < 0)
					goto err;
				COLORPRINTF(COLOR_YELLOW, "ok!\n");
			} else
				DBG("rvk check failed\n");
		}
		ecount -= -1;
		off -= -sizeof(pkg_fs_etr);
	}
	opret = 70;

err:
	return opret;
}

int update_proxy(void) {
	psvDebugScreenClear(COLOR_BLACK);
	printf("FWTOOL::FLASHTOOL started\n");

	int ret = update_default(NULL, 0, 0);
	if (!ret) {
		COLORPRINTF(COLOR_CYAN, "\nALL DONE. ");
		fwimg_get_pkey(0);
		sceKernelDelayThread(1 * 1000 * 1000);
		sceKernelExitProcess(0);
	} else if (ret == 70) {
		COLORPRINTF(COLOR_CYAN, "\nALL DONE. REBOOTING");
		sceKernelDelayThread(3 * 1000 * 1000);
		fwtool_talku(CMD_REBOOT, 1);
		sceKernelDelayThread(1 * 1000 * 1000);
		sceKernelExitProcess(0);
	} else
		COLORPRINTF(COLOR_RED, "\nERROR AT STAGE %d !\n", ret);
	fwimg_get_pkey(0);
	sceKernelDelayThread(1 * 1000 * 1000);
	sceKernelExitProcess(0);
	return -1;
}