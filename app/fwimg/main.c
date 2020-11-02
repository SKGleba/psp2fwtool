#include <stdio.h>
#include <stdlib.h> 
#include <taihen.h>
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include "debugScreen.h"
#include "../../plugin/fwtool.h"

#define FWTOOL_VERSION_STR "FWTOOL v0.8 (beta) by SKGleba"

#define CHUNK_SIZE 64 * 1024
	
#define DBG(...) sceClibPrintf(__VA_ARGS__);

#define printf(...)		\
do {                                \
	sceClibPrintf(__VA_ARGS__);\
	psvDebugScreenPrintf(__VA_ARGS__);\
} while (0)
	
#define COLORPRINTF(color, ...)		\
do {                                \
	psvDebugScreenSetFgColor(color);\
	sceClibPrintf(__VA_ARGS__);\
	psvDebugScreenPrintf(__VA_ARGS__);\
	psvDebugScreenSetFgColor(COLOR_WHITE);\
} while (0)

extern int fwtool_read_fwimage(uint32_t offset, uint32_t size, uint32_t crc32, uint32_t unzip);
extern int fwtool_write_partition(uint32_t offset, uint32_t size, uint8_t partition);
extern int fwtool_personalize_bl(int fup);
extern int fwtool_update_mbr(int use_e2x, int swap_bl, int swap_os);
extern int fwtool_flash_e2x(uint32_t size);
extern int fwtool_unlink(void);
extern int fwtool_talku(int cmd, int cmdbuf);

static char src_u[64];
static int redir_writes = 0;

int fcp(const char *src, const char *dst) {
	printf("Copying %s -> %s (file)... ", src, dst);
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
	printf("%s\n", (res < 0) ? "FAILED" : "OK");
	return res;
}

void copyDir(char *src, char *dst, int csub) {
	if (csub)
		sceIoMkdir(dst, 6);
	SceUID dfd = sceIoDopen(src);
	if (dfd >= 0) {
		printf("Copying %s -> %s (dir)\n", src, dst);
		int res = 0;
		do {
			SceIoDirent dir;
			memset(&dir, 0, sizeof(SceIoDirent));
			res = sceIoDread(dfd, &dir);
			if (res > 0) {
				char *src_path = malloc(strlen(src) + strlen(dir.d_name) + 2);
				snprintf(src_path, 265, "%s/%s", src, dir.d_name);
				char *dst_path = malloc(strlen(dst) + strlen(dir.d_name) + 2);
				snprintf(dst_path, 265, "%s/%s", dst, dir.d_name);
				if (SCE_S_ISDIR(dir.d_stat.st_mode))
					copyDir(src_path, dst_path, 1);
				else
					fcp(src_path, dst_path);
			}
		} while (res > 0);
		sceIoDclose(dfd);
	}
	return;
}

void wait_key_press(int mode)
{
	SceCtrlData pad;
	int ret = 0;
	COLORPRINTF(COLOR_YELLOW, "Press %s.\n", (mode) ? "\n[CROSS] to flash the fwimage\n[SQUARE] to enable kernel file log\n[TRIANGLE] to redirect writes to GC-SD\n[CIRCLE] to exit the installer" : "CIRCLE to reboot");
	sceKernelDelayThread(1000 * 1000);
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
		if ((pad.buttons & SCE_CTRL_TRIANGLE) && mode == 1) {
			redir_writes = fwtool_talku(9, 0);
			printf("REDIRECT TO GC-SD: %s\n", (redir_writes) ? "ENABLED" : "DISABLED");
			sceKernelDelayThread(1000 * 1000);
		}
		if ((pad.buttons & SCE_CTRL_SQUARE) && mode == 1) {
			ret = fwtool_talku(5, 0);
			printf("LOG KDBG TO FILE: %s\n", (ret) ? "ENABLED" : "DISABLED");
			sceKernelDelayThread(1000 * 1000);
		}
		sceKernelDelayThread(200 * 1000);
	}
}

int update_default(const char *fwimage) {
	int opret = 1;

	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------STAGE 1: FS_INIT---------\n\n");
	if (fwimage == NULL)
		fwimage = "ux0:data/fwtool/fwimage.bin";
	else {
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
	printf("Target: %s\n", target_dev[target]);
	if (!fwtool_talku(6, target) && target != 6)
		goto err;
	printf("FS_PART count: %d\n", fwimg_toc.fs_count);
	if (fwimg_toc.fs_count == 0)
		goto err;
	
	sceKernelDelayThread(1000 * 1000);
	wait_key_press(1);
	
	if (target < 6) {
		opret = 2;
		psvDebugScreenClear(COLOR_BLACK);
		COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
		COLORPRINTF(COLOR_CYAN, "\n---------STAGE 2: SC_INIT---------\n\n");
		printf("Bypassing firmware checks on stage 2 loader...");
		if (fwtool_unlink() < 0)
			goto err;
	}
	
	opret = 3;
	pkg_fs_etr fs_entry;
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------STAGE 3: PREP_BL---------\n\n");
	fd = sceIoOpen(fwimage, SCE_O_RDONLY, 0);
	ret = sceIoPread(fd, &fs_entry, sizeof(pkg_fs_etr), sizeof(pkg_toc) + (fwimg_toc.bl_fs_no * sizeof(pkg_fs_etr)));
	sceIoClose(fd);
	printf("Checking bootloader FS_PART nfo (%d)...\n", fwimg_toc.bl_fs_no);
	DBG("\nFS_PART[%d] - magic 0x%04X | type %d\n"
		" READ: size 0x%X | offset 0x%X | ungzip %d\n"
		" WRITE: size 0x%X | offset 0x%X @ id %d\n"
		" PART_CRC32: 0x%08X\n",
		fwimg_toc.bl_fs_no, fs_entry.magic, fs_entry.type,
		fs_entry.pkg_sz, fs_entry.pkg_off, (fs_entry.pkg_sz < fs_entry.dst_sz),
		fs_entry.dst_sz, fs_entry.dst_off, fs_entry.part_id,
		fs_entry.crc32);
	if (ret < 0 || fs_entry.magic != 0xAA12)
		goto err;
	int verif_bl = 0;
	if (fs_entry.type == 1) {
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
	} else {
		printf("Could not find any bootloader entry!\nContinue anyways?\n");
		wait_key_press(1);
	}
	
	opret = 4;
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------STAGE 4: FLASHFS---------\n\n");
	uint8_t ecount = 0;
	int swap_os = 0, swap_bl = 0, use_e2x = 0;
	uint32_t off = sizeof(pkg_toc);
	while (ecount < fwimg_toc.fs_count) {
		DBG("getting entry %d (0x%X)\n", ecount, off);
		fd = sceIoOpen(fwimage, SCE_O_RDONLY, 0);
		ret = sceIoPread(fd, &fs_entry, sizeof(pkg_fs_etr), off);
		sceIoClose(fd);
		if (ret < 0 || fs_entry.magic != 0xAA12)
			goto err;
		printf("Installing %s (R", (fs_entry.type == 2) ? "e2x" : pcode_str[fs_entry.part_id]);
		DBG("\nFS_PART[%d] - magic 0x%04X | type %d\n"
			" READ: size 0x%X | offset 0x%X | ungzip %d\n"
			" WRITE: size 0x%X | offset 0x%X @ id %d\n"
			" PART_CRC32: 0x%08X\n",
			ecount, fs_entry.magic, fs_entry.type,
			fs_entry.pkg_sz, fs_entry.pkg_off, (fs_entry.pkg_sz < fs_entry.dst_sz),
			fs_entry.dst_sz, fs_entry.dst_off, fs_entry.part_id,
			fs_entry.crc32);
		if (fwtool_read_fwimage(fs_entry.pkg_off, fs_entry.pkg_sz, fs_entry.crc32, (fs_entry.pkg_sz == fs_entry.dst_sz) ? 0 : fs_entry.dst_sz) < 0)
			goto err;
		ret = -1;
		printf("W @ 0x%X)... ", fs_entry.dst_off);
		if (fs_entry.type == 0)
			ret = fwtool_write_partition(fs_entry.dst_off, fs_entry.dst_sz, fs_entry.part_id);
		else if (fs_entry.type == 1 && fs_entry.dst_off == 0 && verif_bl) {
			if (fwtool_talku(7, 0))
				ret = fwtool_write_partition(0, fs_entry.dst_sz, 2);
		} else if (fs_entry.type == 2 && fs_entry.dst_off == 0x400)
			ret = fwtool_flash_e2x(fs_entry.dst_sz);
		if (ret < 0)
			goto err;
		COLORPRINTF(COLOR_YELLOW, "ok!\n");
		if (target < 6) {
			DBG("enabling MBR offset update!\n");
			if (fs_entry.type == 0 && fs_entry.part_id == 3)
				swap_os = 1;
			else if (fs_entry.type == 1)
				swap_bl = 1;
		}
		if (fs_entry.type == 2)
			use_e2x = 1;
		ecount-=-1;
		off-=-sizeof(pkg_fs_etr);
	}
	
	opret = 5;
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------STAGE 5: UPD_MBR---------\n\n");
	printf("Swap os0: %d\nSwap slb2: %d\nEnable enso: %d\nUpdating the MBR...\n", swap_os, swap_bl, use_e2x);
	if (fwtool_update_mbr(use_e2x, swap_bl, swap_os) < 0)
		goto err;
	
	opret = 0;
	psvDebugScreenClear(COLOR_BLACK);
	COLORPRINTF(COLOR_RED, FWTOOL_VERSION_STR);
	COLORPRINTF(COLOR_CYAN, "\n---------UPDATE FINISHED!---------\n\n");
	if (redir_writes)
		goto err;
	printf("Removing ux0:id.dat...\n");
	sceIoRemove("ux0:id.dat");
	printf("Applying custom partition patches...\n");
	sceClibStrncpy(src_u, "sdstor0:int-lp-ina-os", 63);
	if (fwtool_talku(10, (int)src_u) >= 0)
		copyDir("ux0:data/fwtool/os0-patch", "grw0:", 0);
	if (fwtool_talku(8, 0x300) >= 0)
		copyDir("ux0:data/fwtool/vs0-patch", "vs0:", 0);
	copyDir("ux0:data/fwtool/ur0-patch", "ur0:", 0);
	
err:
	return opret;
}

int main(int argc, char *argv[])
{
	psvDebugScreenInit();
	psvDebugScreenClear(COLOR_BLACK);
	printf("FWTOOL::FLASHTOOL started\n");
	
	int ret = update_default(NULL);
	if (ret == 0) {
		COLORPRINTF(COLOR_CYAN, "\nALL DONE. ");
		wait_key_press(0);
		sceKernelDelayThread(1 * 1000 * 1000);
		sceKernelExitProcess(0);
	}
	
	COLORPRINTF(COLOR_RED, "\nERROR AT STAGE %d !\n", ret);
	wait_key_press(0);
	sceKernelDelayThread(1 * 1000 * 1000);
	sceKernelExitProcess(0);
	return 0;
}
