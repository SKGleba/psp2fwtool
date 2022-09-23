/* THIS FILE IS A PART OF PSP2FWTOOL
 *
 * Copyright (C) 2019-2022 skgleba
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <psp2kern/sblaimgr.h>
#include <psp2kern/kernel/utils.h>
#include <vitasdkkern.h>

#include "decl.h"

#include "../fwtool.h"
#include "fwtool_funcs.h"

#include "kutils.h"
#include "missing.h"

#include "crc32.c"
#include "tai_compat.c"

static SceIoDevice custom;
char* fwimage = NULL, * fwrpoint = NULL;
void* fsp_buf = NULL, * gz_buf = NULL, * bl_buf = NULL;
static uint32_t fw = 0, minfw = 0, hw_rev = 0, * perv_clkm = NULL, * perv_clks = NULL, dev_checked[DEV_NODEV];
static int (*read_real_mmc)(int target, uint32_t off, void* dst, uint32_t sz) = NULL;
static char src_k[64], mbr[BLOCK_SIZE], cpart_k[64], real_mbr[BLOCK_SIZE], dualos_smallbr[BLOCK_SIZE];
static int emmc = 0, cur_dev = 0, use_new_bl = 0, redir_writes = 0, upmgr_ln = 0, is_prenso = 0, is_locked = 0, skip_int_chk = 0, force_scu = 0;

/*
	BYPASS1: Skip firmware ver checks on bootloaders 0xdeadbeef
	This sets system firmware version to 0xDEADBEEF which makes stage 2 loader use its hardcoded ver
	All offsets are specific to 3.60-3.74
*/
static int skip_bootloader_chk(void) {
	LOG("skip_bootloader_chk\n");
	if (upmgr_ln < 0)
		return upmgr_ln;

	LOG("getting update_mgr funcs\n");
	int (*read_snvs)(int sector, void* data_out, int do_load_sm, int sm_ctx) = NULL;
	int (*write_snvs)(int sector, void* data_in, int do_load_sm, int sm_ctx) = NULL;
	module_get_offset(KERNEL_PID, upmgr_ln, 0, 0x7660 | 1, (uintptr_t*)&read_snvs);
	module_get_offset(KERNEL_PID, upmgr_ln, 0, 0x7774 | 1, (uintptr_t*)&write_snvs);
	if (!read_snvs || !write_snvs)
		return -1;

	LOG("preparing nvs sector 1\n");
	uint32_t patched_fwv[8];
	for (int i = 0; i < 8; i++)
		patched_fwv[i] = 0xDEADBEEF;

	LOG("writing nvs sector 1\n");
	if (write_snvs(1, patched_fwv, 1, -1))
		return -1;

	LOG("reading nvs sector 2\n");
	if (read_snvs(2, patched_fwv, 1, -1))
		return -2;

	LOG("preparing nvs sector 2\n");
	patched_fwv[0] = 0xDEADBEEF;

	LOG("writing nvs sector 2\n");
	if (write_snvs(2, patched_fwv, 1, -1))
		return -3;

	return 0;
}

// Personalize slb2 data @ bufaddr
static int personalize_buf(void* bufaddr) {
	int ret = -1, (*personalize_slsk)(const char* enc_in, const char* enc_out, const char* dbg_enp_in, const char* dbg_enp_out, void* workbuf) = NULL;
	LOG("personalize_buf\n");
	if (upmgr_ln < 0)
		return ret;

	// get req func, same for 3.60-3.73
	module_get_offset(KERNEL_PID, upmgr_ln, 0, 0x58c5, (uintptr_t*)&personalize_slsk);
	if (!personalize_slsk)
		return -1;

	LOG("personalizing stage 2 bootloader...\n");
	// pc (.enc->.enp); nonpc .enp->.enp_
	ret = personalize_slsk("second_loader.enc", "second_loader.enp", "second_loader.enp", "second_loader.enp_", bufaddr);
	if (!ret) {
		LOG("personalizing cMeP secure kernel...\n");
		// pc (.enc->.enp); nonpc .enp->.enp_
		ret = personalize_slsk("secure_kernel.enc", "secure_kernel.enp", "secure_kernel.enp", "secure_kernel.enp_", bufaddr);
		return ret;
	}
	LOG("failed 0x%X\n", ret);
	return ret;
}

// cmp crc of first [size] bytes of [buf] with [exp_crc]
int cmp_crc32(uint32_t exp_crc, void* buf, uint32_t size) {
	if (skip_int_chk || !exp_crc) {
		LOG("skipping crc checks\n");
		return 0;
	}
	uint32_t crc = crc32(0, buf, size);
	LOG("ccrc 0x%X vs 0x%X\n", crc, exp_crc);
	if (exp_crc != crc)
		return -1;
	return 0;
}

// find partition [part_id] with active flag set to [active] in sce mbr [master]
int find_part(master_block_t* master, uint8_t part_id, uint8_t active) {
	LOG("find_part %d %d\n", part_id, active);
	for (size_t i = 0; i < ARRAYSIZE(master->partitions); ++i) {
		if (master->partitions[i].code == part_id && master->partitions[i].active == active)
			return i;
	}
	LOG("not found\n");
	return -1;
}

// get enso install status (0/1)
int get_enso_state(void) {
	char sbr_char[BLOCK_SIZE];
	sceSdifReadSectorMmc(emmc, 1, sbr_char, 1);
	if (!memcmp(sbr_char, SCEMBR_MAGIC, 0x20) && !memcmp(mbr, sbr_char, BLOCK_SIZE) && memcmp(mbr, real_mbr, BLOCK_SIZE))
		return 1;
	return 0;
}

// mount [blkn] as grw0:
int cmount_part(const char* blkn) {
	LOG("cmount_part %s\n", blkn);
	SceIoMountPoint* (*sceIoFindMountPoint)(int id) = NULL;
	uint32_t iofindmp_off = (fw > 0x03630000) ? 0x182f5 : 0x138c1;
	if (fw > 0x03680011)
		iofindmp_off = 0x18735;
	if (module_get_offset(KERNEL_PID, sceKernelSearchModuleByName("SceIofilemgr"), 0, iofindmp_off, (uintptr_t*)&sceIoFindMountPoint) < 0 || !sceIoFindMountPoint)
		return -1;
	SceIoMountPoint* mountp;
	mountp = sceIoFindMountPoint(0xA00);
	if (!mountp)
		return -1;
	custom.dev = mountp->dev->dev;
	custom.dev2 = mountp->dev->dev2;
	custom.blkdev = custom.blkdev2 = blkn;
	custom.id = 0xA00;
	DACR_OFF(mountp->dev = &custom;);
	LOG("remounting...\n");
	sceIoUmount(0xA00, 0, 0, 0);
	sceIoUmount(0xA00, 1, 0, 0);
	return sceIoMount(0xA00, NULL, 0, 0, 0, 0);
}

// set high-perf mode
int set_perf_mode(int boost) {
	LOG("set_perf_mode(%d), cur: 0x%X H 0x%X\n", boost, *perv_clkm, *perv_clks);
	if (boost) { // arm boost
		*perv_clkm = 0xF;
		*perv_clks = 0x0;
	} else {
		scePowerSetArmClockFrequency(444); // arm hperf
		scePowerSetBusClockFrequency(222); // mem
		/*
		*(uint32_t*)(sceSdifGetSdContextGlobal(0) + 0x2424) = *(uint32_t*)(sceSdifGetSdContextGlobal(0) + 0x2420) + 0x810137f0;
		return sceSdifSetBusClockFrequency(*(uint32_t*)(sceSdifGetSdContextGlobal(0) + 0x2414), 0); // emmc hspeed mode
		*/
	}
	return *perv_clkm;
}

// update inactive slb2 bank sha256 in SNVS
int update_snvs_sha256(void) {
	int ret = -1;
	char bl_hash[0x20];
	int (*snvs_update_bl_sha)(int totrans_p, int totrans_sz, void* buf, int bufsz, int op, int ctx) = NULL;
	LOG("getting bootloaders sha256...\n");
	if (sceSha256Digest(bl_buf, ALIGN_SECTOR(*(int*)(bl_buf + 0x20 + (*(int*)(bl_buf + 0xc) * 0x30 + -0x30) + 4)) + *(int*)(bl_buf + 0x20 + (*(int*)(bl_buf + 0xc) * 0x30 + -0x30)) * BLOCK_SIZE, bl_hash) < 0)
		return ret;
	LOG("%s bl sha256 to SNVS...\n", (redir_writes) ? "SKIP write of" : "WRITING");
	if (redir_writes)
		return 0;
	module_get_offset(KERNEL_PID, upmgr_ln, 0, 0x854d, (uintptr_t*)&snvs_update_bl_sha);
	if (!snvs_update_bl_sha)
		return ret;
	ret = snvs_update_bl_sha(9, 0x20, bl_hash, 0x20, 1, -1);
	LOG("snvs_update_bl_sha ret 0x%X\n", ret);
	return ret;
}

// default storage write func
int default_write(uint32_t off, void* buf, uint32_t sz) {
	LOG("BLKWRITE 0x%X @ 0x%X to %s\n", sz, off, (redir_writes) ? "GC-SD" : "EMMC");
	if (!off && sz > 1 && sz != FSP_BUF_SZ_BLOCKS)
		return -1;
	if (redir_writes) {
		if (sceSdifWriteSectorSd(sceSdifGetSdContextPartValidateSd(1), off, buf, sz) < 0)
			LOG("BLKWRITE to GC-SD failed but return 0 anyways\n");
		return 0;
	}
	return sceSdifWriteSectorMmc(emmc, off, buf, sz);
}

// personalize bootloaders in fsp buf. set [fup] if should copy from bl buf first
int fwtool_personalize_bl(int fup) {
	int state = 0, opret = -1;
	ENTER_SYSCALL(state);
	LOG("fwtool_personalize_bl %d\n", fup);
	if (fup) {
		memset(bl_buf, 0, BL_BUF_SZ_BYTES);
		memcpy(bl_buf, fsp_buf, BL_BUF_SZ_BYTES);
		opret = personalize_buf(bl_buf);
		memset(fsp_buf, 0, BL_BUF_SZ_BYTES);
		memcpy(fsp_buf, bl_buf, BL_BUF_SZ_BYTES);
		if (*(uint32_t*)fsp_buf != *(uint32_t*)bl_buf)
			opret = -2;
		if (opret >= 0)
			use_new_bl = 1;
	} else
		opret = personalize_buf(fsp_buf);
	LOG("exit call || 0x%X\n", opret);
	EXIT_SYSCALL(state);
	return opret;
}

// update the mbr with the new partition offsets. set args to apply
int fwtool_update_mbr(int use_e2x, int swap_bl, int swap_os) {
	int state = 0, opret = -1;
	ENTER_SYSCALL(state);
	LOG("fwtool_update_mbr %d %d (p%d) %d\n", use_e2x, swap_bl, use_new_bl, swap_os);
	if (swap_bl && !use_new_bl)
		goto merr;

	master_block_t* master = (master_block_t*)mbr;
	uint8_t wp0 = 0, wp1 = 0;

	if (swap_os) {
		LOG("updating os0 info...\n");
		wp0 = find_part(master, SCEMBR_PART_KERNEL, 0);
		wp1 = find_part(master, SCEMBR_PART_KERNEL, 1);
		if (wp0 < 0 || wp1 < 0)
			goto merr;
		master->partitions[wp0].active = 1;
		master->partitions[wp1].active = 0;
		*(uint32_t*)(mbr + 0x44) = master->partitions[wp0].off;
	}

	if (swap_bl) {
		LOG("updating bootloaders info...\n");
		wp0 = find_part(master, SCEMBR_PART_SBLS, 0);
		wp1 = find_part(master, SCEMBR_PART_SBLS, 1);
		if (wp0 < 0 || wp1 < 0)
			goto merr;
		master->partitions[wp0].active = 1;
		master->partitions[wp1].active = 0;
		*(uint32_t*)(mbr + 0x30) = *(uint32_t*)(bl_buf + 0x50) + master->partitions[wp0].off;
		*(uint32_t*)(mbr + 0x34) = (uint32_t)(*(uint32_t*)(bl_buf + 0x54) / BLOCK_SIZE);
		*(uint32_t*)(mbr + 0x38) = master->partitions[wp0].off;
		if (!*(uint32_t*)(mbr + 0x30))
			goto merr;
	}

	if (use_e2x) {
		LOG("writing a backup MBR copy...\n");
		if (default_write(1, mbr, 1) < 0)
			goto merr;
		LOG("enabling os0 redirect for e2x...\n");
		wp1 = find_part(master, SCEMBR_PART_KERNEL, 1);
		if (wp1 < 0)
			goto merr;
		master->partitions[wp1].off = 2;
	}

	LOG("writing the MBR...\n");
	if (default_write(0, mbr, 1) < 0)
		goto merr;

	opret = 0; // next is optional for compat

	if (swap_bl) {
		LOG("writing a backup bootloaders copy...\n");
		if (default_write(master->partitions[find_part(master, SCEMBR_PART_SBLS, 0)].off, bl_buf, master->partitions[find_part(master, SCEMBR_PART_SBLS, 1)].sz) < 0)
			goto merr;
	}

merr:
	LOG("exit call || 0x%X\n", opret);
	EXIT_SYSCALL(state);
	return opret;
}

// write [size] bytes from fsp buf to sector 2
int fwtool_flash_e2x(uint32_t size) {
	if (!size || size > E2X_SIZE_BYTES || (size % BLOCK_SIZE))
		return -1;
	size = size / BLOCK_SIZE;
	int state = 0, opret = -1;
	ENTER_SYSCALL(state);
	LOG("fwtool_flash_e2x 0x%X\n", size);

	if (is_prenso) {
		LOG("cleaning the MBR...\n");
		if (default_write(0, mbr, 1) < 0)
			goto xerr;
	}

	LOG("writing enso...\n");
	if (default_write(2, fsp_buf, size) < 0)
		goto xerr;

	opret = 0;
xerr:
	LOG("exit call || 0x%X\n", opret);
	EXIT_SYSCALL(state);
	return opret;
}

// remove bootloader SNVS fw checks
int fwtool_unlink(void) {
	int state = 0, opret = -1;
	ENTER_SYSCALL(state);
	LOG("fwtool_unlink\n");
	opret = skip_bootloader_chk();
	LOG("exit call || 0x%X\n", opret);
	EXIT_SYSCALL(state);
	return opret;
}

// read [size] bytes from fwimage @ [offset] to fsp buf, check BLOCK_SIZE to match [exp_crc32], g[unzip] the output
int fwtool_read_fwimage(uint32_t offset, uint32_t size, uint32_t exp_crc32, uint32_t unzip) {
	if (size > FSP_BUF_SZ_BYTES || !size || !offset || unzip > FSP_BUF_SZ_BYTES)
		return -1;
	int state = 0, opret = -1;
	ENTER_SYSCALL(state);
	LOG("fwtool_read_fwimage 0x%X 0x%X 0x%X 0x%X\n", offset, size, exp_crc32, unzip);

	memset(fsp_buf, 0, FSP_BUF_SZ_BYTES);
	if (unzip)
		memset(gz_buf, 0, FSP_BUF_SZ_BYTES);

	LOG("reading fwimage...\n");
	SceIoStat stat;
	int ret = sceIoGetstat(fwimage, &stat);
	if (ret < 0 || stat.st_size < (offset + size))
		goto rerr;

	int fd = sceIoOpen(fwimage, SCE_O_RDONLY, 0);
	ret = sceIoPread(fd, (unzip) ? gz_buf : fsp_buf, size, offset);
	sceIoClose(fd);
	if (ret < 0)
		goto rerr;

	LOG("crchecking...\n");
	if (cmp_crc32(exp_crc32, (unzip) ? gz_buf : fsp_buf, (size > BLOCK_SIZE) ? BLOCK_SIZE : size) < 0)
		goto rerr;

	LOG("ungzipping...\n");
	if (unzip)
		opret = sceGzipDecompress(fsp_buf, unzip, gz_buf, NULL);
	else
		opret = 0;

rerr:
	LOG("exit call || 0x%X\n", opret);
	EXIT_SYSCALL(state);
	return opret;
}

// default_write [size] bytes from fsp buf to inactive [partition] @ [offset]
int fwtool_write_partition(uint32_t offset, uint32_t size, uint8_t partition) {
	if (!size || size > FSP_BUF_SZ_BYTES || (size % BLOCK_SIZE) || (offset % BLOCK_SIZE))
		return -1;
	if (partition == SCEMBR_PART_EMPTY) {
		int known_empty = 0;
		for (int i = 0; i < E2X_MISC_NOTYPE; i++) {
			if (offset == e2x_misc_type_offsets[i] && size <= e2x_misc_type_sizes[i])
				known_empty = 1;
		}
		if (!known_empty)
			return -2;
	}
	size = size / BLOCK_SIZE;
	offset = offset / BLOCK_SIZE;
	int state = 0, opret = -1;
	ENTER_SYSCALL(state);
	LOG("fwtool_write_partition 0x%X 0x%X %d (%s)\n", offset, size, partition, pcode_str[partition]);

	uint32_t main_off = 0;
	if (partition != SCEMBR_PART_EMPTY) {
		master_block_t* master = (master_block_t*)mbr;
		int pno = find_part(master, partition, 0);
		if (pno < 0)
			goto werr;

		LOG("getting partition off...\n");
		main_off = master->partitions[pno].off;
		if (!main_off || (offset + size) > master->partitions[pno].sz)
			goto werr;
	}

	LOG("writing partition...\n");
	if (default_write(main_off + offset, fsp_buf, size) < 0)
		goto werr;

	opret = 0;
werr:
	LOG("exit call || 0x%X\n", opret);
	EXIT_SYSCALL(state);
	return opret;
}

// [dump]/restore emmc to/from fwrpoint
int fwtool_rw_emmcimg(int dump) {
	int state = 0, opret = -1;
	ENTER_SYSCALL(state);
	LOG("fwtool_rw_emmcimg %s (%d)\n", fwrpoint, dump);
	int fd = 0, crcn = 0;
	uint32_t copied = 0, size = 0;
	memset(fsp_buf, 0, FSP_BUF_SZ_BYTES);
	emmcimg_super img_super;
	memset(&img_super, 0, sizeof(emmcimg_super));
	master_block_t* master = (master_block_t*)real_mbr;
	if (dump) {
		size = (dump == 2) ? master->partitions[find_part(master, SCEMBR_PART_USERDATA, 0)].off : master->device_size;
		img_super.magic = RPOINT_MAGIC;
		img_super.size = size;
		LOG("dumping (0x%X blocks)...\n", size);
		fd = sceIoOpen(fwrpoint, SCE_O_WRONLY | SCE_O_TRUNC | SCE_O_CREAT, 6);
		if (!size || fd < 0 || sceIoPwrite(fd, &img_super, sizeof(emmcimg_super), 0) < 0)
			goto exrwend;
		while ((copied + FSP_BUF_SZ_BLOCKS) <= size) {
			if (read_real_mmc(emmc, copied, fsp_buf, FSP_BUF_SZ_BLOCKS) < 0 || sceIoPwrite(fd, fsp_buf, FSP_BUF_SZ_BLOCKS * BLOCK_SIZE, sizeof(emmcimg_super) + (copied * BLOCK_SIZE)) < 0)
				goto exrwend;
			img_super.blk_crc[crcn] = crc32(0, fsp_buf, FSP_BUF_SZ_BLOCKS * BLOCK_SIZE);
			crcn -= -1;
			copied -= -FSP_BUF_SZ_BLOCKS;
		}
		if (copied < size && (size - copied) <= FSP_BUF_SZ_BLOCKS) {
			if (read_real_mmc(emmc, copied, fsp_buf, (size - copied)) < 0 || sceIoPwrite(fd, fsp_buf, (size - copied) * BLOCK_SIZE, sizeof(emmcimg_super) + (copied * BLOCK_SIZE)) < 0)
				goto exrwend;
			img_super.blk_crc[crcn] = crc32(0, fsp_buf, (size - copied) * BLOCK_SIZE);
			copied = size;
		}
		img_super.prev_crc = crc32(0, img_super.blk_crc, 0xF7 * 4);
		LOG("master crc 0x%X\n", img_super.prev_crc);
		if (!img_super.blk_crc[0] || sceIoPwrite(fd, &img_super, sizeof(emmcimg_super), 0) < 0)
			goto exrwend;
	} else {
		LOG("getting image size...\n");
		fd = sceIoOpen(fwrpoint, SCE_O_RDONLY, 0);
		if (fd < 0 || sceIoPread(fd, &img_super, sizeof(emmcimg_super), 0) < 0)
			goto exrwend;
		size = img_super.size;
		if (img_super.magic != RPOINT_MAGIC || !size || !img_super.blk_crc[0] || cmp_crc32(img_super.prev_crc, img_super.blk_crc, 0xF7 * 4) < 0)
			goto exrwend;
		LOG("restoring (0x%X blocks)...\n", size);
		while ((copied + FSP_BUF_SZ_BLOCKS) <= size) {
			if (sceIoPread(fd, fsp_buf, FSP_BUF_SZ_BLOCKS * BLOCK_SIZE, sizeof(emmcimg_super) + (copied * BLOCK_SIZE)) < 0 || cmp_crc32(img_super.blk_crc[crcn], fsp_buf, FSP_BUF_SZ_BLOCKS * BLOCK_SIZE) < 0 || default_write(copied, fsp_buf, FSP_BUF_SZ_BLOCKS) < 0)
				goto exrwend;
			crcn -= -1;
			copied -= -FSP_BUF_SZ_BLOCKS;
		}
		if (copied < size && (size - copied) <= FSP_BUF_SZ_BLOCKS) {
			if (sceIoPread(fd, fsp_buf, (size - copied) * BLOCK_SIZE, sizeof(emmcimg_super) + (copied * BLOCK_SIZE)) < 0 || cmp_crc32(img_super.blk_crc[crcn], fsp_buf, (size - copied) * BLOCK_SIZE) < 0 || default_write(copied, fsp_buf, (size - copied)) < 0)
				goto exrwend;
			copied = size;
		}
	}

	LOG("write done 0x%X=0x%X?\n", copied, size);
	if (copied != size)
		opret = -1;
	else
		opret = 0;

exrwend:
	sceIoClose(fd);
	LOG("exit call || 0x%X\n", opret);
	EXIT_SYSCALL(state);
	return opret;
}

// install dualOS on the default write device
int fwtool_dualos_create(void) {
	int state = 0;
	ENTER_SYSCALL(state);

	LOG("dualOS::create() started\n");

	master_block_t master_br, slave_br;
	master_block_t* current_br = (master_block_t*)mbr, * curreal_br = (master_block_t*)real_mbr;

	LOG("creating masterOS ptable\n");
	memcpy(&master_br, (void*)current_br, BLOCK_SIZE);
	if (master_br.partitions[11].code != SCEMBR_PART_USERDATA || master_br.partitions[11].off != 0x200000 || master_br.partitions[13].code != SCEMBR_PART_EMPTY) // Make sure its a default, masterOS partition table
		goto cdosbend;
	master_br.partitions[11].sz = DOS_UR0_SIZE; // 512MiB ur0
	master_br.partitions[12].code = SCEMBR_PART_USEREXT;
	master_br.partitions[12].type = SCEMBR_FS_EXFAT;
	master_br.partitions[12].flags = 0x00000fff;
	master_br.partitions[12].unk = master_br.partitions[11].unk;
	master_br.partitions[12].off = DOS_UX0_OFFSET;
	master_br.partitions[12].sz = DOS_UX0_SIZE;
	master_br.partitions[13].code = SCEMBR_PART_UNUSED;
	master_br.partitions[13].type = SCEMBR_FS_RAW;
	master_br.partitions[13].flags = 0x00000fff;
	master_br.partitions[13].unk = 0;
	master_br.partitions[13].off = DOS_SLAVE_START;
	master_br.partitions[13].sz = master_br.device_size - DOS_SLAVE_START;
	master_br.partitions[14].sz = master_br.partitions[13].sz;
	master_br.partitions[15].sz = master_br.device_size;

	LOG("creating slaveOS ptable\n");
	memcpy(&slave_br, &master_br, BLOCK_SIZE);
	int part_id = find_part(&slave_br, SCEMBR_PART_SYSTEM, 0);
	slave_br.partitions[part_id].off = DOS_SLAVE_START;
	part_id = find_part(&slave_br, SCEMBR_PART_REGISTRY, 0);
	slave_br.partitions[part_id].off = DOS_SLAVE_START + 0x80000; // + vs0 size
	slave_br.partitions[11].off = DOS_SLAVE_START + 0x80000 + 0x10000; // + vs0 & vd0 sizes
	slave_br.partitions[11].sz = slave_br.device_size - DOS_RESERVED_SZ - slave_br.partitions[11].off;
	slave_br.partitions[13].off = slave_br.device_size - DOS_RESERVED_SZ;
	slave_br.partitions[13].sz = DOS_RESERVED_SZ;
	slave_br.partitions[14].sz = slave_br.partitions[13].sz;

	LOG("creating dualOS superblock\n");
	memset(bl_buf, 0, BL_BUF_SZ_BYTES);
	dualos_super_t* dualos_br = (dualos_super_t*)bl_buf;
	dualos_br->magic = DUALOS_MAGIC;
	dualos_br->device_size = current_br->device_size;
	dualos_br->master_mode = 1;
	part_id = find_part(current_br, SCEMBR_PART_KERNEL, 1);
	if (part_id < 0 || read_real_mmc(emmc, 0, fsp_buf, DOS_BKP_BLOCK_SZ) < 0 || read_real_mmc(emmc, current_br->partitions[part_id].off, gz_buf, DOS_BKP_BLOCK_SZ) < 0)
		goto cdosbend;
	memcpy(fsp_buf + BLOCK_SIZE, &master_br, BLOCK_SIZE);
	if (curreal_br->partitions[part_id].off == 2)
		master_br.partitions[part_id].off = 2;
	memcpy(fsp_buf, &master_br, BLOCK_SIZE);
	LOG("calc masterOS swap crcs\n");
	dualos_br->master_crc[0] = crc32(0, fsp_buf, DOS_BKP_BLOCK_SZ * BLOCK_SIZE);
	dualos_br->master_crc[1] = crc32(0, gz_buf, DOS_BKP_BLOCK_SZ * BLOCK_SIZE);
	LOG("write - masterOS\n");
	if (default_write(current_br->device_size - DOS_RESERVED_SZ + DOS_MASTER_BKP_START, fsp_buf, DOS_BKP_BLOCK_SZ) < 0
		|| default_write(current_br->device_size - DOS_RESERVED_SZ + DOS_MASTER_OS0_START, gz_buf, DOS_BKP_BLOCK_SZ) < 0)
		goto cdosbend;
	LOG("calc slaveOS swap crcs\n");
	memcpy(fsp_buf + BLOCK_SIZE, &slave_br, BLOCK_SIZE);
	if (curreal_br->partitions[part_id].off == 2)
		slave_br.partitions[part_id].off = 2;
	memcpy(fsp_buf, &slave_br, BLOCK_SIZE);
	dualos_br->slave_crc[0] = crc32(0, fsp_buf, DOS_BKP_BLOCK_SZ * BLOCK_SIZE);
	dualos_br->slave_crc[1] = dualos_br->master_crc[1];
	LOG("write - slaveOS\n");
	if (default_write(current_br->device_size - DOS_RESERVED_SZ + DOS_SLAVE_BKP_START, fsp_buf, DOS_BKP_BLOCK_SZ) < 0
		|| default_write(current_br->device_size - DOS_RESERVED_SZ + DOS_SLAVE_OS0_START, gz_buf, DOS_BKP_BLOCK_SZ) < 0)
		goto cdosbend;

	LOG("copying masterOS vs0 to slaveOS vs0\n");
	memset(fsp_buf, 0, FSP_BUF_SZ_BYTES);
	part_id = find_part(current_br, SCEMBR_PART_SYSTEM, 0);
	uint32_t copied = 0, size = 0x80000, offset = current_br->partitions[part_id].off;
	if (offset < DOS_BKP_BLOCK_SZ)
		goto cdosbend;
	while ((copied + FSP_BUF_SZ_BLOCKS) <= size) {
		if (read_real_mmc(emmc, offset + copied, fsp_buf, FSP_BUF_SZ_BLOCKS) < 0 || default_write(DOS_SLAVE_START + copied, fsp_buf, FSP_BUF_SZ_BLOCKS) < 0)
			goto cdosbend;
		copied -= -FSP_BUF_SZ_BLOCKS;
	}

	LOG("writing dualOS superblock\n");
	if (default_write(current_br->device_size - DOS_RESERVED_SZ, bl_buf, DOS_MASTER_BKP_START) < 0)
		goto cdosbend;

	LOG("all done, writing the masterOS boot record\n");
	memset(gz_buf, 0, FSP_BUF_SZ_BYTES);
	if (read_real_mmc(emmc, current_br->device_size - DOS_RESERVED_SZ + DOS_MASTER_BKP_START, gz_buf, DOS_BKP_BLOCK_SZ) < 0
		|| default_write(0, gz_buf, DOS_BKP_BLOCK_SZ) < 0)
		goto cdosbend;

	LOG("all done!\n");

	EXIT_SYSCALL(state);
	return 0;
cdosbend:
	LOG("ERROR!\n");
	EXIT_SYSCALL(state);
	return -1;
}

// swap EMMC masterOS<->slaveOS if dualOS is installed on the EMMC
int fwtool_dualos_swap(void) {
	int state = 0;
	ENTER_SYSCALL(state);

	LOG("dualOS::swap() started\n");
	
	memset(bl_buf, 0, BLOCK_SIZE);
	memset(fsp_buf, 0, FSP_BUF_SZ_BYTES);
	memset(gz_buf, 0, FSP_BUF_SZ_BYTES);
	dualos_super_t* dualos_br = (dualos_super_t*)bl_buf;
	master_block_t* current_br = (master_block_t*)mbr;
	master_block_t* swap_br_0 = (master_block_t*)fsp_buf;
	master_block_t* swap_br_1 = (master_block_t*)(fsp_buf + BLOCK_SIZE);
	
	LOG("getting dualOS info\n");
	uint32_t dualos_info_off = current_br->device_size - DOS_RESERVED_SZ;
	if (read_real_mmc(emmc, dualos_info_off, bl_buf, 1) < 0 || dualos_br->magic != DUALOS_MAGIC)
		goto sdosbend;
	if (!skip_int_chk && dualos_br->device_size != current_br->device_size)
		goto sdosbend;
	int part_id = find_part(current_br, SCEMBR_PART_KERNEL, 1);
	if (part_id < 0)
		goto sdosbend;
	uint32_t os0_off = current_br->partitions[part_id].off;
	
	if (dualos_br->master_mode) { // masterOS->slaveOS
		LOG("backing up the current BR and os0\n");
		if (read_real_mmc(emmc, 0, fsp_buf, DOS_BKP_BLOCK_SZ) < 0 || read_real_mmc(emmc, os0_off, gz_buf, DOS_BKP_BLOCK_SZ) < 0)
			goto sdosbend;
		dualos_br->master_crc[0] = crc32(0, fsp_buf, DOS_BKP_BLOCK_SZ * BLOCK_SIZE);
		dualos_br->master_crc[1] = crc32(0, gz_buf, DOS_BKP_BLOCK_SZ * BLOCK_SIZE);
		if (default_write(dualos_info_off + DOS_MASTER_BKP_START, fsp_buf, DOS_BKP_BLOCK_SZ) < 0
			|| default_write(dualos_info_off + DOS_MASTER_OS0_START, gz_buf, DOS_BKP_BLOCK_SZ) < 0)
			goto sdosbend;
		LOG("updating dualOS superblock\n");
		dualos_br->master_mode = 0;
		if (default_write(dualos_info_off, bl_buf, 1) < 0)
			goto sdosbend;
		LOG("reading the new BR and os0 (slave)\n");
		if (read_real_mmc(emmc, dualos_info_off + DOS_SLAVE_BKP_START, fsp_buf, DOS_BKP_BLOCK_SZ) < 0 
			|| read_real_mmc(emmc, dualos_info_off + DOS_SLAVE_OS0_START, gz_buf, DOS_BKP_BLOCK_SZ) < 0)
			goto sdosbend;
		LOG("checking the new BR and os0 (slave)\n");
		if (cmp_crc32(dualos_br->slave_crc[0], fsp_buf, DOS_BKP_BLOCK_SZ * BLOCK_SIZE) < 0 || cmp_crc32(dualos_br->slave_crc[1], gz_buf, DOS_BKP_BLOCK_SZ * BLOCK_SIZE) < 0)
			goto sdosbend;
		part_id = find_part(swap_br_0, SCEMBR_PART_KERNEL, 1);
		if (part_id < 0)
			goto sdosbend;
		else if (swap_br_0->partitions[part_id].off == 2) {
			part_id = find_part(swap_br_1, SCEMBR_PART_KERNEL, 1);
			if (part_id < 0)
				goto sdosbend;
			os0_off = swap_br_1->partitions[part_id].off;
		} else
			os0_off = swap_br_0->partitions[part_id].off;
		if (os0_off < DOS_BKP_BLOCK_SZ)
			goto sdosbend;
		LOG("writing the new BR and os0 (slave)\n");
		if (default_write(0, fsp_buf, DOS_BKP_BLOCK_SZ) < 0 || default_write(os0_off, gz_buf, DOS_BKP_BLOCK_SZ) < 0)
			goto sdosbend;
	} else { // slaveOS->masterOS
		LOG("backing up the current BR and os0\n");
		if (read_real_mmc(emmc, 0, fsp_buf, DOS_BKP_BLOCK_SZ) < 0 || read_real_mmc(emmc, os0_off, gz_buf, DOS_BKP_BLOCK_SZ) < 0)
			goto sdosbend;
		dualos_br->slave_crc[0] = crc32(0, fsp_buf, DOS_BKP_BLOCK_SZ * BLOCK_SIZE);
		dualos_br->slave_crc[1] = crc32(0, gz_buf, DOS_BKP_BLOCK_SZ * BLOCK_SIZE);
		if (default_write(dualos_info_off + DOS_SLAVE_BKP_START, fsp_buf, DOS_BKP_BLOCK_SZ) < 0
			|| default_write(dualos_info_off + DOS_SLAVE_OS0_START, gz_buf, DOS_BKP_BLOCK_SZ) < 0)
			goto sdosbend;
		LOG("updating dualOS superblock\n");
		dualos_br->master_mode = 1;
		if (default_write(dualos_info_off, bl_buf, 1) < 0)
			goto sdosbend;
		LOG("reading the new BR and os0 (master)\n");
		if (read_real_mmc(emmc, dualos_info_off + DOS_MASTER_BKP_START, fsp_buf, DOS_BKP_BLOCK_SZ) < 0
			|| read_real_mmc(emmc, dualos_info_off + DOS_MASTER_OS0_START, gz_buf, DOS_BKP_BLOCK_SZ) < 0)
			goto sdosbend;
		LOG("checking the new BR and os0 (master)\n");
		if (cmp_crc32(dualos_br->master_crc[0], fsp_buf, DOS_BKP_BLOCK_SZ * BLOCK_SIZE) < 0 || cmp_crc32(dualos_br->master_crc[1], gz_buf, DOS_BKP_BLOCK_SZ * BLOCK_SIZE) < 0)
			goto sdosbend;
		part_id = find_part(swap_br_0, SCEMBR_PART_KERNEL, 1);
		if (part_id < 0)
			goto sdosbend;
		else if (swap_br_0->partitions[part_id].off == 2) {
			part_id = find_part(swap_br_1, SCEMBR_PART_KERNEL, 1);
			if (part_id < 0)
				goto sdosbend;
			os0_off = swap_br_1->partitions[part_id].off;
		} else
			os0_off = swap_br_0->partitions[part_id].off;
		if (os0_off < DOS_BKP_BLOCK_SZ)
			goto sdosbend;
		LOG("writing the new BR and os0 (master)\n");
		if (default_write(0, fsp_buf, DOS_BKP_BLOCK_SZ) < 0 || default_write(os0_off, gz_buf, DOS_BKP_BLOCK_SZ) < 0)
			goto sdosbend;
	}
	
	LOG("all done!\n");

	EXIT_SYSCALL(state);
	return 0;
sdosbend:
	LOG("ERROR!\n");
	EXIT_SYSCALL(state);
	return -1;
}

// check if device[id] can be flashed with fw that's header is [hdr2] & [hdr3]
int fwtool_check_rvk(int type, int id, uint32_t hdr2, uint32_t hdr3) {
	int state = 0, opret = -1;
	ENTER_SYSCALL(state);
	LOG("fwtool_check_rvk(%d, %s, 0x%X, 0x%X)\n", type, (type == FSPART_TYPE_DEV) ? dcode_str[id] : pcode_str[id], hdr2, hdr3);
	
	if (type < FSPART_TYPE_DEV) {
		uint32_t* list_entries = NULL;
		int (*get_list_vector_by_pscode)(uint32_t * vector) = NULL;
		module_get_offset(KERNEL_PID, upmgr_ln, 0, 0xcf8c, (uintptr_t*)&list_entries);
		module_get_offset(KERNEL_PID, upmgr_ln, 0, 0x7d01, (uintptr_t*)&get_list_vector_by_pscode);
		if (!list_entries || !get_list_vector_by_pscode)
			goto rvkchkend;

		uint32_t vector = 0;
		if (get_list_vector_by_pscode(&vector) < 0)
			goto rvkchkend;
		
		if (id)
			list_entries -= -8;

		opret = -2;
		if (hdr2 & list_entries[vector * 3])
			opret = 0;
		goto rvkchkend;
	}

	int (*check_dev_compat)() = NULL;
	int (*get_dev_fw)() = NULL;
	uint32_t dev_fw = 0;
	switch (id) {
	case DEV_SYSCON_FW:
	case DEV_SYSCON_CMPMGR:
	case DEV_SYSCON_DL:
		module_get_offset(KERNEL_PID, upmgr_ln, 0, (!id) ? 0x75c9 : ((id == DEV_SYSCON_CMPMGR) ? 0x7621 : 0x75f1), (uintptr_t*)&get_dev_fw);
		module_get_offset(KERNEL_PID, upmgr_ln, 0, 0x7285, (uintptr_t*)&check_dev_compat);
		if (check_dev_compat && get_dev_fw) {
			opret = get_dev_fw(&dev_fw);
			if (!opret)
				opret = check_dev_compat(hdr2, hdr3, id);
		}
		break;
	case DEV_MOTION0:
	case DEV_MOTION1:
		module_get_offset(KERNEL_PID, upmgr_ln, 0, 0x9559, (uintptr_t*)&get_dev_fw);
		module_get_offset(KERNEL_PID, upmgr_ln, 0, 0x91b5, (uintptr_t*)&check_dev_compat);
		if (check_dev_compat && get_dev_fw) {
			opret = get_dev_fw(&dev_fw);
			if (!opret)
				opret = check_dev_compat(hdr2, hdr3);
		}
		break;
	case DEV_CP:
		module_get_offset(KERNEL_PID, upmgr_ln, 0, 0xaee9, (uintptr_t*)&get_dev_fw);
		module_get_offset(KERNEL_PID, upmgr_ln, 0, 0xad05, (uintptr_t*)&check_dev_compat);
		if (check_dev_compat && get_dev_fw) {
			opret = get_dev_fw(&dev_fw);
			if (!opret)
				opret = check_dev_compat(hdr2, hdr3);
		}
		break;
	case DEV_BIC_FW:
	case DEV_BIC_DF:
		module_get_offset(KERNEL_PID, upmgr_ln, 0, (id == DEV_BIC_FW) ? 0x907d : 0x90a5, (uintptr_t*)&get_dev_fw);
		module_get_offset(KERNEL_PID, upmgr_ln, 0, 0x8b0d, (uintptr_t*)&check_dev_compat);
		if (check_dev_compat && get_dev_fw) {
			opret = get_dev_fw(&dev_fw);
			if (!opret)
				opret = check_dev_compat(hdr2, hdr3, id - DEV_BIC_FW);
		}
		break;
	case DEV_TOUCH_FW:
	case DEV_TOUCH_CFG:
		module_get_offset(KERNEL_PID, upmgr_ln, 0, (id == DEV_TOUCH_FW) ? 0xab39 : 0xab81, (uintptr_t*)&get_dev_fw);
		module_get_offset(KERNEL_PID, upmgr_ln, 0, 0x9805, (uintptr_t*)&check_dev_compat);
		if (check_dev_compat && get_dev_fw) {
			opret = get_dev_fw(0, &dev_fw);
			if (!opret)
				opret = check_dev_compat(hdr2, hdr3, id - DEV_TOUCH_FW);
		}
		break;
	case DEV_COM:
		module_get_offset(KERNEL_PID, upmgr_ln, 0, 0xb3f5, (uintptr_t*)&get_dev_fw);
		module_get_offset(KERNEL_PID, upmgr_ln, 0, 0xb461, (uintptr_t*)&check_dev_compat);
		if (check_dev_compat && get_dev_fw) {
			opret = get_dev_fw(&dev_fw);
			if (!opret)
				opret = check_dev_compat(hdr2, hdr3);
		}
		break;
	default:
		opret = -3;
		break;
	}

	if (!opret)
		dev_checked[id] = dev_fw;

rvkchkend:
	LOG("exit call || 0x%X\n", opret);
	EXIT_SYSCALL(state);
	return opret;
}

/*
	Update device[id] with the firmware in fsp buf
	WARNING: this may take a few minutes depending on the target
	TODO: implement proper callback handling for progress status
*/
int fwtool_update_dev(int id, uint32_t size, uint32_t u_hdr_data[3]) {
	int state = 0, opret = -1;
	ENTER_SYSCALL(state);
	if (!u_hdr_data)
		goto updevend;
	
	uint32_t hdr_data[3];
	sceKernelMemcpyUserToKernel(hdr_data, u_hdr_data, 12);
	uint32_t hdr2 = hdr_data[0];
	uint32_t hdr3 = hdr_data[1];
	uint32_t hdr4 = hdr_data[2];

	LOG("fwtool_update_dev(%s(0x%X, 0x%X), 0x%X) | checked: %s\n", dcode_str[id], hdr2, hdr3, size, dev_checked[id] ? "yes" : "no");
	if (!size || size > FSP_BUF_SZ_BYTES || !*(uint32_t*)fsp_buf)
		goto updevend;

	opret = -2;
	LOG("checking data...\n");
	if (!dev_checked[id])
		goto updevend;
	
	opret = 0;
	if (hdr4 && dev_checked[id] >= hdr4 && !force_scu) {
		LOG("already up-to-date, skipping\n");
		goto updevend;
	}

	opret = -3;
	LOG("updating %s\n", dcode_str[id]);
	int (*update_dev)() = NULL;
	char fake_callback[0x20];
	switch (id) {
	case DEV_SYSCON_FW:
	case DEV_SYSCON_CMPMGR:
	case DEV_SYSCON_DL:
		LOG("checking the ernie firmware header: 0x%X for 0x%X\n", *(uint32_t*)(fsp_buf + 0x4), *(uint32_t*)(fsp_buf + 0x8));
		if (*(uint8_t*)(fsp_buf + size - 0x18) != 0x20 && *(uint8_t*)(fsp_buf + size - 0x20) != 3)
			goto updevend;
		LOG("getting sysconUtilWriteFirmware vaddr\n");
		// int (*sysconUtilWriteFirmware)(int mode, uint8_t * buf, uint32_t size, uint32_t fw_offset, uint32_t callback_id, int* callback_struct) = NULL;
		opret = module_get_offset(KERNEL_PID, upmgr_ln, 0, 0x72bd, (uintptr_t*)&update_dev);
		if (!update_dev)
			goto updevend;
		LOG("writing ernie firmware, this may take a longer while\n");
		opret = update_dev(id, (uint8_t*)fsp_buf, size, 0, 0, NULL);
		break;
	case DEV_MOTION0:
	case DEV_MOTION1:
		LOG("getting MotionUtilWriteFirmware vaddr\n");
		opret = module_get_offset(KERNEL_PID, upmgr_ln, 0, 0x91f5, (uintptr_t*)&update_dev);
		if (!update_dev)
			goto updevend;
		LOG("writing berkley firmware, this may take a longer while\n");
		opret = update_dev(hdr2, hdr3, fsp_buf, fake_callback, size, 0, hdr4, 0x20, fake_callback);
		break;
	case DEV_CP:
		LOG("getting cpUtilWriteFirmware vaddr\n");
		opret = module_get_offset(KERNEL_PID, upmgr_ln, 0, 0xad75, (uintptr_t*)&update_dev);
		if (!update_dev)
			goto updevend;
		LOG("writing grover firmware, this may take a longer while\n");
		opret = update_dev(fsp_buf, NULL, size, 0, hdr4, 0, NULL);
		break;
	case DEV_BIC_FW:
	case DEV_BIC_DF:
		LOG("getting bicUtilWriteFirmware vaddr\n");
		opret = module_get_offset(KERNEL_PID, upmgr_ln, 0, 0x8d4d, (uintptr_t*)&update_dev);
		if (!update_dev)
			goto updevend;
		LOG("writing abby firmware, this may take a longer while\n");
		opret = update_dev(fsp_buf, fake_callback, size, 0, hdr4, 0x20, fake_callback);
		break;
	case DEV_TOUCH_FW:
	case DEV_TOUCH_CFG:
		LOG("getting touchUtilWriteFirmware vaddr\n");
		opret = module_get_offset(KERNEL_PID, upmgr_ln, 0, (id == DEV_TOUCH_FW) ? 0x9ab9 : 0xa361, (uintptr_t*)&update_dev);
		if (!update_dev)
			goto updevend;
		LOG("writing touch %s, this may take a longer while\n", (id == DEV_TOUCH_FW) ? "firmware" : "config");
		opret = update_dev(hdr2, hdr3, fsp_buf, NULL, size, 0, hdr4, 0, NULL);
		break;
	case DEV_COM:
		LOG("getting comUtilWriteFirmware vaddr\n");
		opret = module_get_offset(KERNEL_PID, upmgr_ln, 0, 0xb4f9, (uintptr_t*)&update_dev);
		if (!update_dev)
			goto updevend;
		LOG("writing bbmc firmware, this may take a longer while\n");
		opret = update_dev(fsp_buf, NULL, size, 0, hdr4, 0, 0x20, fake_callback, 0x20, fake_callback, 0, 0); // the updater seems messed up
		break;
	default:
		break;
	}

updevend:
	LOG("exit call || 0x%X\n", opret);
	EXIT_SYSCALL(state);
	return opret;
}

// misc
int fwtool_talku(int cmd, int cmdbuf) {
	int state = 0, opret = -1;
	ENTER_SYSCALL(state);

	LOG("fwtool_talKU %d\n", cmd);

	switch (cmd) {
	case CMD_SET_FWIMG_PATH: // set fwimage path
		opret = sceKernelMemcpyUserToKernel(src_k, (void *)cmdbuf, 64);
		fwimage = src_k;
		break;
	case CMD_GET_MBR: // copy x200 from fwtool mbr buf to user buf
	case CMD_GET_BL: // copy x400000 from fwtool bl buf to user buf
		opret = (cmd == CMD_GET_MBR) ? sceKernelMemcpyKernelToUser((void*)cmdbuf, mbr, BLOCK_SIZE) : sceKernelMemcpyKernelToUser((void*)cmdbuf, bl_buf, BL_BUF_SZ_BYTES);
		break;
	case CMD_GET_GZ: // copy x1000000 from gz buf to user buf
	case CMD_GET_FSP: // copy x1000000 from fsp buf to user buf
		opret = sceKernelMemcpyKernelToUser((void*)cmdbuf, (cmd == CMD_GET_GZ) ? gz_buf : fsp_buf, FSP_BUF_SZ_BYTES);
		break;
	case CMD_SET_FILE_LOGGING: // file debug logs flag
		enable_f_logging = !enable_f_logging;
		if (enable_f_logging)
			LOG_START("file logging enabled\n");
		opret = enable_f_logging;
		break;
	case CMD_CMP_TARGET: // check if user type = kernel type
		opret = (cur_dev == (uint8_t)cmdbuf);
		break;
	case CMD_BL_TO_FSP: // copy bl buf to fsp buf with bl checks
		memset(fsp_buf, 0, FSP_BUF_SZ_BYTES);
		cleainv_dcache(bl_buf, BL_BUF_SZ_BYTES);
		memcpy(fsp_buf, bl_buf, BL_BUF_SZ_BYTES);
		opret = (*(uint32_t*)fsp_buf == SBLS_MAGIC_H) ? (*(uint32_t*)fsp_buf == *(uint32_t*)bl_buf) : 0;
		cleainv_dcache(fsp_buf, FSP_BUF_SZ_BYTES);
		break;
	case CMD_UMOUNT: // unmount partition with id [cmdbuf]
		sceIoUmount(cmdbuf, 0, 0, 0);
		sceIoUmount(cmdbuf, 1, 0, 0);
		opret = sceIoMount(cmdbuf, NULL, 2, 0, 0, 0);
		break;
	case CMD_WRITE_REDIRECT: // redirect writes to gc-sd & skip snvs bl sha update flag
		redir_writes = !redir_writes;
		opret = redir_writes;
		break;
	case CMD_GRW_MOUNT: // mount custom blkpath as grw0
		memset(cpart_k, 0, 64);
		sceKernelMemcpyUserToKernel(cpart_k, (void*)cmdbuf, 64);
		opret = cmount_part(cpart_k);
		break;
	case CMD_SET_INACTIVE_BL_SHA256: // update inactive bl sha256 in SNVS
		opret = siofix(update_snvs_sha256);
		break;
	case CMD_GET_ENSO_STATUS: // get enso install state (0/1)
		opret = is_prenso;
		break;
	case CMD_NO_BL_PERSONALIZE: // skip bl personalize
		memset(bl_buf, 0, BL_BUF_SZ_BYTES);
		memcpy(bl_buf, fsp_buf, BL_BUF_SZ_BYTES);
		use_new_bl = 1;
		opret = (*(uint32_t*)bl_buf == SBLS_MAGIC_H) ? 0 : -1;
		break;
	case CMD_SET_FWRP_PATH: // set fwrpoint path
		opret = sceKernelMemcpyUserToKernel(src_k, (void*)cmdbuf, 64);
		fwrpoint = src_k;
		break;
	case CMD_GET_REAL_MBR: // copy x200 from fwtool real mbr buf to user buf
		opret = sceKernelMemcpyKernelToUser((void*)cmdbuf, real_mbr, BLOCK_SIZE);
		break;
	case CMD_GET_LOCK_STATE: // get client-lock state
		opret = (is_locked) ? -1 : 0;
		is_locked = 1;
		break;
	case CMD_SKIP_CRC: // skip crc checks flag
		skip_int_chk = !skip_int_chk;
		opret = skip_int_chk;
		break;
	case CMD_VALIDATE_KBLFW: // check if ARM KBL FWV is not below minfw
		if (*(uint32_t*)(bl_buf + (*(uint32_t*)(bl_buf + 0xE0) * BLOCK_SIZE)) != SCE_MAGIC_H || *(uint32_t*)(bl_buf + (*(uint32_t*)(bl_buf + 0xE0) * BLOCK_SIZE) + 0x92) < minfw)
			opret = -1;
		else
			opret = 0;
		break;
	case CMD_SET_PERF_MODE: // change perf mode
		opret = set_perf_mode(cmdbuf);
		break;
	case CMD_GET_DUALOS_HEADER: // copy x200 from dualOS boot record to user
		opret = sceKernelMemcpyKernelToUser((void*)cmdbuf, dualos_smallbr, BLOCK_SIZE);
		break;
	case CMD_WIPE_DUALOS: // wipe the dualOS superblock
		if (*(uint32_t*)(dualos_smallbr + 12))
			opret = default_write(*(uint32_t*)(real_mbr + 0x24) - DOS_RESERVED_SZ, (bl_buf + 0x800), 1);
		break;
	case CMD_GET_HW_REV: // get hw revision
		if (cmdbuf)
			opret = (cmdbuf == hw_rev);
		else
			opret = hw_rev;
		break;
	case CMD_FORCE_DEV_UPDATE: // force component update
		force_scu = !force_scu;
		opret = force_scu;
		break;
	case CMD_REBOOT: // reboot | TODO: add some checks & cleanup
		if (!cmdbuf) // normal reboot
			scePowerRequestColdReset();
		else if (cmdbuf == 1) // ernie reboot (watchdog)
			scePowerRequestErnieShutdown(1);
		else if (cmdbuf == 2) // ernie reboot (only pre-system)
			sceSysconErnieShutdown(1);
		else if (cmdbuf == 3) // abby reset (only pre-system)
			sceSysconBatterySWReset();
		opret = 0;
		break;
	case CMD_GET_CURRENT_FWV: // get current firmware version
		opret = fw;
		break;
	case CMD_SET_TSMP_FWV: // set sdk version in deci4p tsmp
		opret = sceSysrootSetTsmpVersionInt("SdkVersion", 10, (uint32_t)cmdbuf);
		break;
	default:
		break;
	}

	LOG("exit call || 0x%X\n", opret);
	EXIT_SYSCALL(state);
	return opret;
}

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize argc, const void* args) {
	LOG_START("\nhello world from fwtool vx!\n");
	LOG("initializing the patch ss...\n");
	if (tai_init() < 0)
		return SCE_KERNEL_START_FAILED;
	upmgr_ln = sceKernelSearchModuleByName("SceSblUpdateMgr");
	if (upmgr_ln < 0)
		return SCE_KERNEL_START_FAILED;

	LOG("reserving 16+16 MiB cached & 4 MiB uncached...\n");
	sceKernelGetMemBlockBase(sceKernelAllocMemBlock("fsw_p", MB_K_DEF | MB_I_BK | MB_C_Y | MB_A_RW, FSP_BUF_SZ_BYTES, NULL), (void**)&fsp_buf);
	sceKernelGetMemBlockBase(sceKernelAllocMemBlock("gz_p", MB_K_DEF | MB_I_BK | MB_C_Y | MB_A_RW, FSP_BUF_SZ_BYTES, NULL), (void**)&gz_buf);
	sceKernelGetMemBlockBase(sceKernelAllocMemBlock("bl_p", MB_K_DEF | MB_I_BK | MB_C_N | MB_A_RW, BL_BUF_SZ_BYTES, NULL), (void**)&bl_buf);
	if (!fsp_buf || !gz_buf || !bl_buf)
		return SCE_KERNEL_START_FAILED;

	memset(mbr, 0, BLOCK_SIZE);
	memset(real_mbr, 0, BLOCK_SIZE);
	memset(dev_checked, 0, DEV_NODEV * 4);
	fwimage = "ux0:data/fwtool/" CFWIMG_NAME;
	fwrpoint = "ux0:data/fwtool/" RPOINT_NAME;

	LOG("getting console info...\n");
	fw = *(uint32_t*)(*(int*)(sceSysrootGetSysrootBase() + 0x6c) + 4);
	minfw = *(uint32_t*)(*(int*)(sceSysrootGetSysrootBase() + 0x6c) + 8);
	hw_rev = sceSysconGetHardwareInfo();
	cur_dev = sceSblAimgrIsTest() ? FWTARGET_EMU : (sceSblAimgrIsTool() ? FWTARGET_DEVTOOL : (sceSblAimgrIsDEX() ? FWTARGET_TESTKIT : (sceSblAimgrIsCEX() ? FWTARGET_RETAIL : FWTARGET_UNKNOWN)));
	emmc = sceSdifGetSdContextPartValidateMmc(0);
	if (!emmc || module_get_offset(KERNEL_PID, sceKernelSearchModuleByName("SceSdif"), 0, 0x3e7d, (uintptr_t*)&read_real_mmc) < 0 || !read_real_mmc)
		return SCE_KERNEL_START_FAILED;
	if (sceSdifReadSectorMmc(emmc, 0, mbr, 1) < 0 // read currently used MBR
		|| sceSdifReadSectorMmc(emmc, 2, fsp_buf, E2X_SIZE_BLOCKS) < 0 // read enso area
		|| read_real_mmc(emmc, 0, real_mbr, 1) < 0 // read the real MBR
		|| read_real_mmc(emmc, *(uint32_t*)(real_mbr + 0x24) - DOS_RESERVED_SZ, dualos_smallbr, 1) < 0) // read dualOS boot record
		return SCE_KERNEL_START_FAILED;
	is_prenso = get_enso_state();
	perv_clkm = (uint32_t*)pa2va(0xE3103000);
	perv_clks = (uint32_t*)pa2va(0xE3103004);
	LOG("firmware: 0x%08X\nmin firmware: 0x%08X\nhardware: 0x%08X\ndevice: %s\nenso: %s\nemmctx: 0x%X\n", fw, minfw, hw_rev, target_dev[cur_dev], (is_prenso) ? "YES" : "NO", emmc);
	if (fw < FWTOOL_MINFW || fw > FWTOOL_MAXFW)
		return SCE_KERNEL_START_FAILED;

	LOG("init done\n\n---------WAITING_4_U_CMDN---------\n\n");

	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void* args) {
	return SCE_KERNEL_STOP_SUCCESS;
}
