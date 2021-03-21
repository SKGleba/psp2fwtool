/* THIS FILE IS A PART OF PSP2FWTOOL
 *
 * Copyright (C) 2019-2021 skgleba
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "tai_compat.c"
#include "fwtool.h"
#include "crc32.c"
#include "kutils.h"

static SceIoDevice custom;
char* fwimage = NULL, * fwrpoint = NULL;
void* fsp_buf = NULL, * gz_buf = NULL, * bl_buf = NULL;
static uint32_t fw = 0, minfw = 0, * perv_clkm = NULL, * perv_clks = NULL;
static int (*read_real_mmc)(int target, uint32_t off, void* dst, uint32_t sz) = NULL;
static char src_k[64], mbr[0x200], cpart_k[64], real_mbr[0x200], dualos_smallbr[0x200];
static int emmc = 0, cur_dev = 0, use_new_bl = 0, redir_writes = 0, upmgr_ln = 0, is_prenso = 0, is_locked = 0, skip_int_chk = 0;

/*
	BYPASS1: Skip firmware ver checks on bootloaders 0xdeadbeef
	This sets system firmware version to 0xDEADBEEF which makes stage 2 loader use its hardcoded ver
	All offsets are specific to 3.60-3.73
	TODO: proper REimplementation, its EZ.
*/
static int skip_bootloader_chk(void) {
	uint32_t arg0 = 0, arg1 = 0;
	int (*init_sc)(uint32_t * pm0, uint32_t * pm1) = NULL, (*prep_sm)(uint32_t * pm0, uint32_t * pm1, int mode, int ctx) = NULL;
	LOG("skip_bootloader_chk\n");
	if (upmgr_ln < 0)
		return upmgr_ln;

	LOG("patching PMx2 erret...\n");
	// make sc_init error before overwriting PMx2 flags
	char cmp_err[2] = { 0x69, 0x28 }, cmp_ret[4] = { 0x46, 0xf2, 0x05, 0x23 };
	INJECT_NOGET(upmgr_ln, 0x87a4, cmp_err, sizeof(cmp_err));
	INJECT_NOGET(upmgr_ln, 0x898c, cmp_ret, sizeof(cmp_ret));

	// if previous patch fails make sure it will not write the flags
	char nop_32[4] = { 0xaf, 0xf3, 0x00, 0x80 };
	INJECT_NOGET(upmgr_ln, 0x88da, nop_32, 4);
	INJECT_NOGET(upmgr_ln, 0x88f4, nop_32, 4);
	INJECT_NOGET(upmgr_ln, 0x890c, nop_32, 4);
	INJECT_NOGET(upmgr_ln, 0x8928, nop_32, 4);
	INJECT_NOGET(upmgr_ln, 0x8944, nop_32, 4);

	// get req funcs
	module_get_offset(KERNEL_PID, upmgr_ln, 0, 0x8639, (uintptr_t*)&prep_sm);
	module_get_offset(KERNEL_PID, upmgr_ln, 0, 0x8705, (uintptr_t*)&init_sc);
	if (prep_sm == NULL || init_sc == NULL)
		return -1;

	LOG("starting update sm...\n");
	// load SM, get PMx2
	if (prep_sm(&arg0, &arg1, 1, -1) < 0)
		return -1;

	// pointless but always fun
	arg0 = 0xFFFFFFFF;
	arg1 = 0xFFFFFFFF;

	LOG("calling sc_init, good luck!\n");
	// initialize sc, aka unk flags & PMx2 default & ver=0xdeadbeef (skips ver checks on bl2)
	int ret = init_sc(&arg0, &arg1);
	LOG("sc_init ret 0x%X exp 0x800F6205\n", ret);
	if (ret == 0x800F6205) // correct CUSTOM ret
		return 0;

	return ret;
}

// Personalize slb2 data @ bufaddr
static int personalize_buf(void* bufaddr) {
	int ret = -1, (*personalize_slsk)(const char* enc_in, const char* enc_out, const char* dbg_enp_in, const char* dbg_enp_out, void* workbuf) = NULL;
	LOG("personalize_buf\n");
	if (upmgr_ln < 0)
		return ret;

	// get req func, same for 3.60-3.73
	module_get_offset(KERNEL_PID, upmgr_ln, 0, 0x58c5, (uintptr_t*)&personalize_slsk);
	if (personalize_slsk == NULL)
		return -1;

	LOG("personalizing stage 2 bootloader...\n");
	// pc (.enc->.enp); nonpc .enp->.enp_
	ret = personalize_slsk("second_loader.enc", "second_loader.enp", "second_loader.enp", "second_loader.enp_", bufaddr);
	if (ret == 0) {
		LOG("personalizing cMeP secure kernel...\n");
		// pc (.enc->.enp); nonpc .enp->.enp_
		ret = personalize_slsk("secure_kernel.enc", "secure_kernel.enp", "secure_kernel.enp", "secure_kernel.enp_", bufaddr);
		return ret;
	}
	LOG("failed 0x%X\n", ret);
	return ret;
}

// cmp crc of first [size] bytes of [buf] with [exp_crc]
uint32_t cmp_crc32(uint32_t exp_crc, void* buf, uint32_t size) {
	if (skip_int_chk || exp_crc == 0) {
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
	char sbr_char[0x200];
	ksceSdifReadSectorMmc(emmc, 1, sbr_char, 1);
	if (memcmp(sbr_char, "Sony Computer Entertainment Inc.", 0x20) == 0 && memcmp(mbr, sbr_char, 0x200) == 0 && memcmp(mbr, real_mbr, 0x200) != 0)
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
	if (module_get_offset(KERNEL_PID, ksceKernelSearchModuleByName("SceIofilemgr"), 0, iofindmp_off, (uintptr_t*)&sceIoFindMountPoint) < 0 || sceIoFindMountPoint == NULL)
		return -1;
	SceIoMountPoint* mountp;
	mountp = sceIoFindMountPoint(0xA00);
	if (mountp == NULL)
		return -1;
	custom.dev = mountp->dev->dev;
	custom.dev2 = mountp->dev->dev2;
	custom.blkdev = custom.blkdev2 = blkn;
	custom.id = 0xA00;
	DACR_OFF(mountp->dev = &custom;);
	LOG("remounting...\n");
	ksceIoUmount(0xA00, 0, 0, 0);
	ksceIoUmount(0xA00, 1, 0, 0);
	return ksceIoMount(0xA00, NULL, 0, 0, 0, 0);
}

// set high-perf mode
int set_perf_mode(int boost) {
	LOG("set_perf_mode(%d), cur: 0x%X H 0x%X\n", boost, *perv_clkm, *perv_clks);
	if (boost) { // arm boost
		*perv_clkm = 0xF;
		*perv_clks = 0x0;
	} else {
		kscePowerSetArmClockFrequency(444); // arm hperf
		kscePowerSetBusClockFrequency(222); // mem
		/*
		*(uint32_t*)(ksceSdifGetSdContextGlobal(0) + 0x2424) = *(uint32_t*)(ksceSdifGetSdContextGlobal(0) + 0x2420) + 0x810137f0;
		return ksceSdifSetBusClockFrequency(*(uint32_t*)(ksceSdifGetSdContextGlobal(0) + 0x2414), 0); // emmc hspeed mode
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
	if (ksceSha256Digest(bl_buf, (*(int*)(bl_buf + 0x20 + (*(int*)(bl_buf + 0xc) * 0x30 + -0x30) + 4) + 0x1ffU & 0xfffffe00) + *(int*)(bl_buf + 0x20 + (*(int*)(bl_buf + 0xc) * 0x30 + -0x30)) * 0x200, bl_hash) < 0)
		return ret;
	LOG("%s bl sha256 to SNVS...\n", (redir_writes) ? "SKIP write of" : "WRITING");
	if (redir_writes)
		return 0;
	module_get_offset(KERNEL_PID, upmgr_ln, 0, 0x854d, (uintptr_t*)&snvs_update_bl_sha);
	if (snvs_update_bl_sha == NULL)
		return ret;
	ret = snvs_update_bl_sha(9, 0x20, bl_hash, 0x20, 1, -1);
	LOG("snvs_update_bl_sha ret 0x%X\n", ret);
	return ret;
}

// default storage write func
int default_write(uint32_t off, void* buf, uint32_t sz) {
	LOG("BLKWRITE 0x%X @ 0x%X to %s\n", sz, off, (redir_writes) ? "GC-SD" : "EMMC");
	if (off == 0 && sz > 1 && sz != 0x8000)
		return -1;
	if (redir_writes) {
		if (ksceSdifWriteSectorSd(ksceSdifGetSdContextPartValidateSd(1), off, buf, sz) < 0)
			LOG("BLKWRITE to GC-SD failed but return 0 anyways\n");
		return 0;
	}
	return ksceSdifWriteSectorMmc(emmc, off, buf, sz);
}

// swap EMMC masterOS<->slaveOS if dualOS is installed on the EMMC
int fwtool_dualos_swap(void) {
	int state = 0;
	ENTER_SYSCALL(state);

	LOG("dualOS::swap() started\n");
	
	memset(bl_buf, 0, 0x200);
	memset(fsp_buf, 0, 0x1000000);
	memset(gz_buf, 0, 0x1000000);
	dualos_super_t* dualos_br = (dualos_super_t*)bl_buf;
	master_block_t* current_br = (master_block_t*)mbr;
	master_block_t* swap_br_0 = (master_block_t*)fsp_buf;
	master_block_t* swap_br_1 = (master_block_t*)(fsp_buf + 0x200);
	
	LOG("getting dualOS info\n");
	uint32_t dualos_info_off = current_br->device_size - DOS_RESERVED_SZ;
	if (read_real_mmc(emmc, dualos_info_off, bl_buf, 1) < 0 || dualos_br->magic != DUALOS_MAGIC)
		goto sdosbend;
	if (!skip_int_chk && dualos_br->device_size != current_br->device_size)
		goto sdosbend;
	int part_id = find_part(current_br, 3, 1);
	if (part_id < 0)
		goto sdosbend;
	uint32_t os0_off = current_br->partitions[part_id].off;
	
	if (dualos_br->master_mode) { // masterOS->slaveOS
		LOG("backing up the current BR and os0\n");
		if (read_real_mmc(emmc, 0, fsp_buf, DOS_BKP_BLOCK_SZ) < 0 || read_real_mmc(emmc, os0_off, gz_buf, DOS_BKP_BLOCK_SZ) < 0)
			goto sdosbend;
		dualos_br->master_crc[0] = crc32(0, fsp_buf, DOS_BKP_BLOCK_SZ * 0x200);
		dualos_br->master_crc[1] = crc32(0, gz_buf, DOS_BKP_BLOCK_SZ * 0x200);
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
		if (cmp_crc32(dualos_br->slave_crc[0], fsp_buf, DOS_BKP_BLOCK_SZ) < 0 || cmp_crc32(dualos_br->slave_crc[1], gz_buf, DOS_BKP_BLOCK_SZ) < 0)
			goto sdosbend;
		part_id = find_part(swap_br_0, 3, 1);
		if (part_id < 0)
			goto sdosbend;
		else if (swap_br_0->partitions[part_id].off == 2) {
			part_id = find_part(swap_br_1, 3, 1);
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
		dualos_br->slave_crc[0] = crc32(0, fsp_buf, DOS_BKP_BLOCK_SZ * 0x200);
		dualos_br->slave_crc[1] = crc32(0, gz_buf, DOS_BKP_BLOCK_SZ * 0x200);
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
		if (cmp_crc32(dualos_br->master_crc[0], fsp_buf, DOS_BKP_BLOCK_SZ) < 0 || cmp_crc32(dualos_br->master_crc[1], gz_buf, DOS_BKP_BLOCK_SZ) < 0)
			goto sdosbend;
		part_id = find_part(swap_br_0, 3, 1);
		if (part_id < 0)
			goto sdosbend;
		else if (swap_br_0->partitions[part_id].off == 2) {
			part_id = find_part(swap_br_1, 3, 1);
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

// install dualOS on the default write device
int fwtool_dualos_create(void) {
	int state = 0;
	ENTER_SYSCALL(state);
	
	LOG("dualOS::create() started\n");
	
	master_block_t master_br, slave_br;
	master_block_t* current_br = (master_block_t*)mbr, * curreal_br = (master_block_t*)real_mbr;
	
	LOG("creating masterOS ptable\n");
	memcpy(&master_br, (void*)current_br, 0x200);
	if (master_br.partitions[11].code != 0x7 || master_br.partitions[11].off != 0x200000 || master_br.partitions[13].code != 0) // Make sure its a default, masterOS partition table
		goto cdosbend;
	master_br.partitions[11].sz = 0x100000; // 512MB ur0
	master_br.partitions[12].code = 0x8;
	master_br.partitions[12].type = 0x7;
	master_br.partitions[12].flags = 0x00000fff;
	master_br.partitions[12].unk = master_br.partitions[11].unk;
	master_br.partitions[12].off = 0x300000;
	master_br.partitions[12].sz = 0x100000;
	master_br.partitions[13].code = 0xF;
	master_br.partitions[13].type = 0xDA;
	master_br.partitions[13].flags = 0x00000fff;
	master_br.partitions[13].unk = 0;
	master_br.partitions[13].off = DOS_SLAVE_START;
	master_br.partitions[13].sz = master_br.device_size - DOS_SLAVE_START;
	master_br.partitions[14].sz = master_br.partitions[13].sz;
	master_br.partitions[15].sz = master_br.device_size;
	
	LOG("creating slaveOS ptable\n");
	memcpy(&slave_br, &master_br, 0x200);
	int part_id = find_part(&slave_br, 4, 0);
	slave_br.partitions[part_id].off = DOS_SLAVE_START;
	slave_br.partitions[11].off = DOS_SLAVE_START + DOS_VS0_SIZE;
	slave_br.partitions[11].sz = slave_br.device_size - DOS_RESERVED_SZ - slave_br.partitions[11].off;
	slave_br.partitions[13].off = slave_br.device_size - DOS_RESERVED_SZ;
	slave_br.partitions[13].sz = DOS_RESERVED_SZ;
	slave_br.partitions[14].sz = slave_br.partitions[13].sz;
	
	LOG("creating dualOS superblock\n");
	memset(bl_buf, 0, 0x400000);
	dualos_super_t* dualos_br = (dualos_super_t *)bl_buf;
	dualos_br->magic = DUALOS_MAGIC;
	dualos_br->device_size = current_br->device_size;
	dualos_br->master_mode = 1;
	part_id = find_part(current_br, 3, 1);
	if (part_id < 0 || read_real_mmc(emmc, 0, fsp_buf, DOS_BKP_BLOCK_SZ) < 0 || read_real_mmc(emmc, current_br->partitions[part_id].off, gz_buf, DOS_BKP_BLOCK_SZ) < 0)
		goto cdosbend;
	memcpy(fsp_buf + 0x200, &master_br, 0x200);
	if (curreal_br->partitions[part_id].off == 2)
		master_br.partitions[part_id].off = 2;
	memcpy(fsp_buf, &master_br, 0x200);
	LOG("calc masterOS swap crcs\n");
	dualos_br->master_crc[0] = crc32(0, fsp_buf, DOS_BKP_BLOCK_SZ * 0x200);
	dualos_br->master_crc[1] = crc32(0, gz_buf, DOS_BKP_BLOCK_SZ * 0x200);
	LOG("write - masterOS\n");
	if (default_write(current_br->device_size - DOS_RESERVED_SZ + DOS_MASTER_BKP_START, fsp_buf, DOS_BKP_BLOCK_SZ) < 0
		|| default_write(current_br->device_size - DOS_RESERVED_SZ + DOS_MASTER_OS0_START, gz_buf, DOS_BKP_BLOCK_SZ) < 0)
		goto cdosbend;
	LOG("calc slaveOS swap crcs\n");
	memcpy(fsp_buf + 0x200, &slave_br, 0x200);
	if (curreal_br->partitions[part_id].off == 2)
		slave_br.partitions[part_id].off = 2;
	memcpy(fsp_buf, &slave_br, 0x200);
	dualos_br->slave_crc[0] = crc32(0, fsp_buf, DOS_BKP_BLOCK_SZ * 0x200);
	dualos_br->slave_crc[1] = dualos_br->master_crc[1];
	LOG("write - slaveOS\n");
	if (default_write(current_br->device_size - DOS_RESERVED_SZ + DOS_SLAVE_BKP_START, fsp_buf, DOS_BKP_BLOCK_SZ) < 0
		|| default_write(current_br->device_size - DOS_RESERVED_SZ + DOS_SLAVE_OS0_START, gz_buf, DOS_BKP_BLOCK_SZ) < 0)
		goto cdosbend;
	
	LOG("copying masterOS vs0 to slaveOS vs0\n");
	memset(fsp_buf, 0, 0x1000000);
	part_id = find_part(current_br, 4, 0);
	uint32_t copied = 0, size = DOS_VS0_SIZE, offset = current_br->partitions[part_id].off;
	if (offset < DOS_BKP_BLOCK_SZ)
		goto cdosbend;
	while ((copied + 0x8000) <= size) {
		if (read_real_mmc(emmc, offset + copied, fsp_buf, 0x8000) < 0 || default_write(DOS_SLAVE_START + copied, fsp_buf, 0x8000) < 0)
			goto cdosbend;
		copied-=-0x8000;
	}
	
	LOG("writing dualOS superblock\n");
	if (default_write(current_br->device_size - DOS_RESERVED_SZ, bl_buf, DOS_MASTER_BKP_START) < 0)
		goto cdosbend;
	
	LOG("all done, writing the masterOS boot record\n");
	memset(gz_buf, 0, 0x1000000);
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

// [dump]/restore emmc to/from fwrpoint
int fwtool_rw_emmcimg(int dump) {
	int state = 0, opret = -1;
	ENTER_SYSCALL(state);
	LOG("fwtool_rw_emmcimg %s (%d)\n", fwrpoint, dump);
	int fd = 0, crcn = 0;
	uint32_t copied = 0, size = 0;
	memset(fsp_buf, 0, 0x1000000);
	emmcimg_super img_super;
	memset(&img_super, 0, sizeof(emmcimg_super));
	master_block_t* master = (master_block_t*)real_mbr;
	if (dump > 0) {
		size = (dump == 2) ? master->partitions[find_part(master, 7, 0)].off : master->device_size;
		img_super.magic = RPOINT_MAGIC;
		img_super.size = size;
		LOG("dumping (0x%X blocks)...\n", size);
		fd = ksceIoOpen(fwrpoint, SCE_O_WRONLY | SCE_O_TRUNC | SCE_O_CREAT, 6);
		if (size == 0 || fd < 0 || ksceIoWrite(fd, &img_super, sizeof(emmcimg_super)) < 0)
			goto exrwend;
		while ((copied + 0x8000) <= size) {
			if (read_real_mmc(emmc, copied, fsp_buf, 0x8000) < 0 || ksceIoPwrite(fd, fsp_buf, 0x8000 * 0x200, sizeof(emmcimg_super) + (copied * 0x200)) < 0)
				goto exrwend;
			img_super.blk_crc[crcn] = crc32(0, fsp_buf, 0x8000 * 0x200);
			crcn -= -1;
			copied -= -0x8000;
		}
		if (copied < size && (size - copied) <= 0x8000) {
			if (read_real_mmc(emmc, copied, fsp_buf, (size - copied)) < 0 || ksceIoPwrite(fd, fsp_buf, (size - copied) * 0x200, sizeof(emmcimg_super) + (copied * 0x200)) < 0)
				goto exrwend;
			img_super.blk_crc[crcn] = crc32(0, fsp_buf, (size - copied) * 0x200);
			copied = size;
		}
		img_super.prev_crc = crc32(0, img_super.blk_crc, 0xed * 4);
		LOG("master crc 0x%X\n", img_super.prev_crc);
		if (img_super.blk_crc[0] == 0 || ksceIoPwrite(fd, &img_super, sizeof(emmcimg_super), 0) < 0)
			goto exrwend;
	} else {
		LOG("getting image size...\n");
		fd = ksceIoOpen(fwrpoint, SCE_O_RDONLY, 0);
		if (fd < 0 || ksceIoRead(fd, &img_super, sizeof(emmcimg_super)) < 0)
			goto exrwend;
		size = img_super.size;
		if (img_super.magic != RPOINT_MAGIC || size == 0 || img_super.blk_crc[0] == 0 || cmp_crc32(img_super.prev_crc, img_super.blk_crc, 0xed * 4) < 0)
			goto exrwend;
		LOG("restoring (0x%X blocks)...\n", size);
		while ((copied + 0x8000) <= size) {
			if (ksceIoPread(fd, fsp_buf, 0x8000 * 0x200, sizeof(emmcimg_super) + (copied * 0x200)) < 0 || cmp_crc32(img_super.blk_crc[crcn], fsp_buf, 0x8000 * 0x200) < 0 || default_write(copied, fsp_buf, 0x8000) < 0)
				goto exrwend;
			crcn -= -1;
			copied -= -0x8000;
		}
		if (copied < size && (size - copied) <= 0x8000) {
			if (ksceIoPread(fd, fsp_buf, (size - copied) * 0x200, sizeof(emmcimg_super) + (copied * 0x200)) < 0 || cmp_crc32(img_super.blk_crc[crcn], fsp_buf, (size - copied) * 0x200) < 0 || default_write(copied, fsp_buf, (size - copied)) < 0)
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
	ksceIoClose(fd);
	LOG("exit call || 0x%X\n", opret);
	EXIT_SYSCALL(state);
	return opret;
}

// read [size] bytes from fwimage @ [offset] to fsp buf, check 0x200 to match [exp_crc32], g[unzip] the output
int fwtool_read_fwimage(uint32_t offset, uint32_t size, uint32_t exp_crc32, uint32_t unzip) {
	if (size > 0x1000000 || size == 0 || offset == 0 || unzip > 0x1000000)
		return -1;
	int state = 0, opret = -1;
	ENTER_SYSCALL(state);
	LOG("fwtool_read_fwimage 0x%X 0x%X 0x%X 0x%X\n", offset, size, exp_crc32, unzip);

	memset(fsp_buf, 0, 0x1000000);
	if (unzip > 0)
		memset(gz_buf, 0, 0x1000000);

	LOG("reading fwimage...\n");
	SceIoStat stat;
	int ret = ksceIoGetstat(fwimage, &stat);
	if (ret < 0 || stat.st_size < (offset + size))
		goto rerr;

	int fd = ksceIoOpen(fwimage, SCE_O_RDONLY, 0);
	ret = ksceIoPread(fd, (unzip > 0) ? gz_buf : fsp_buf, size, offset);
	ksceIoClose(fd);
	if (ret < 0)
		goto rerr;

	LOG("crchecking...\n");
	if (cmp_crc32(exp_crc32, (unzip > 0) ? gz_buf : fsp_buf, 0x200) < 0)
		goto rerr;

	LOG("ungzipping...\n");
	if (unzip > 0)
		opret = ksceGzipDecompress(fsp_buf, unzip, gz_buf, NULL);
	else
		opret = 0;

rerr:
	LOG("exit call || 0x%X\n", opret);
	EXIT_SYSCALL(state);
	return opret;
}

// default_write [size] bytes from fsp buf to inactive [partition] @ [offset]
int fwtool_write_partition(uint32_t offset, uint32_t size, uint8_t partition) {
	if (size == 0 || size > 0x1000000 || (size % 0x200) != 0 || (offset % 0x200) != 0 || partition == 0)
		return -1;
	size = size / 0x200;
	offset = offset / 0x200;
	int state = 0, opret = -1;
	ENTER_SYSCALL(state);
	LOG("fwtool_write_partition 0x%X 0x%X %d (%s)\n", size, offset, partition, pcode_str[partition]);

	master_block_t* master = (master_block_t*)mbr;
	int pno = find_part(master, partition, 0);
	if (pno < 0)
		goto werr;

	LOG("getting partition off...\n");
	uint32_t main_off = master->partitions[pno].off;
	if (main_off == 0 || (offset + size) > master->partitions[pno].sz)
		goto werr;

	LOG("writing partition...\n");
	if (default_write(main_off + offset, fsp_buf, size) < 0)
		goto werr;

	opret = 0;
werr:
	LOG("exit call || 0x%X\n", opret);
	EXIT_SYSCALL(state);
	return opret;
}

// personalize bootloaders in fsp buf. set [fup] if should copy from bl buf first
int fwtool_personalize_bl(int fup) {
	int state = 0, opret = -1;
	ENTER_SYSCALL(state);
	LOG("fwtool_personalize_bl %d\n", fup);
	if (fup) {
		memset(bl_buf, 0, 0x400000);
		memcpy(bl_buf, fsp_buf, 0x400000);
		opret = personalize_buf(bl_buf);
		memset(fsp_buf, 0, 0x400000);
		memcpy(fsp_buf, bl_buf, 0x400000);
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
		wp0 = find_part(master, 3, 0);
		wp1 = find_part(master, 3, 1);
		if (wp0 < 0 || wp1 < 0)
			goto merr;
		master->partitions[wp0].active = 1;
		master->partitions[wp1].active = 0;
		*(uint32_t*)(mbr + 0x44) = master->partitions[wp0].off;
	}

	if (swap_bl) {
		LOG("updating bootloaders info...\n");
		wp0 = find_part(master, 2, 0);
		wp1 = find_part(master, 2, 1);
		if (wp0 < 0 || wp1 < 0)
			goto merr;
		master->partitions[wp0].active = 1;
		master->partitions[wp1].active = 0;
		*(uint32_t*)(mbr + 0x30) = *(uint32_t*)(bl_buf + 0x50) + master->partitions[wp0].off;
		*(uint32_t*)(mbr + 0x34) = (uint32_t)(*(uint32_t*)(bl_buf + 0x54) / 0x200);
		*(uint32_t*)(mbr + 0x38) = master->partitions[wp0].off;
		if (*(uint32_t*)(mbr + 0x30) == 0)
			goto merr;
	}

	if (use_e2x) {
		LOG("writing a backup MBR copy...\n");
		if (default_write(1, mbr, 1) < 0)
			goto merr;
		LOG("enabling os0 redirect for e2x...\n");
		wp1 = find_part(master, 3, 1);
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
		if (default_write(master->partitions[find_part(master, 2, 0)].off, bl_buf, master->partitions[find_part(master, 2, 1)].sz) < 0)
			goto merr;
	}

merr:
	LOG("exit call || 0x%X\n", opret);
	EXIT_SYSCALL(state);
	return opret;
}

// write [size] bytes from fsp buf to sector 2
int fwtool_flash_e2x(uint32_t size) {
	if (size == 0 || size > (0x2E * 0x200) || (size % 0x200) != 0)
		return -1;
	size = size / 0x200;
	int state = 0, opret = -1;
	ENTER_SYSCALL(state);
	LOG("fwtool_flash_e2x 0x%X\n", size);

	if (!is_prenso) {
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

// misc
int fwtool_talku(int cmd, int cmdbuf) {
	int state = 0, opret = -1;
	ENTER_SYSCALL(state);

	LOG("fwtool_talKU %d\n", cmd);

	switch (cmd) {
	case 0: // set fwimage path
		opret = ksceKernelMemcpyUserToKernel(src_k, (uintptr_t)cmdbuf, 64);
		fwimage = src_k;
		break;
	case 1: // copy x200 from fwtool mbr buf to user buf
	case 2: // copy x400000 from fwtool bl buf to user buf
		opret = (cmd == 1) ? ksceKernelMemcpyKernelToUser((uintptr_t)cmdbuf, mbr, 0x200) : ksceKernelMemcpyKernelToUser((uintptr_t)cmdbuf, bl_buf, 0x400000);
		break;
	case 3: // copy x1000000 from gz buf to user buf
	case 4: // copy x1000000 from fsp buf to user buf
		opret = ksceKernelMemcpyKernelToUser((uintptr_t)cmdbuf, (cmd == 3) ? gz_buf : fsp_buf, 0x1000000);
		break;
	case 5: // file debug logs flag
		enable_f_logging = !enable_f_logging;
		if (enable_f_logging)
			LOG_START("file logging enabled\n");
		opret = enable_f_logging;
		break;
	case 6: // check if user type = kernel type
		opret = (cur_dev == (uint8_t)cmdbuf);
		break;
	case 7: // copy bl buf to fsp buf with bl checks
		memset(fsp_buf, 0, 0x1000000);
		cleainv_dcache(bl_buf, 0x400000);
		memcpy(fsp_buf, bl_buf, 0x400000);
		opret = (*(uint32_t*)fsp_buf == 0x32424c53) ? (*(uint32_t*)fsp_buf == *(uint32_t*)bl_buf) : 0;
		cleainv_dcache(fsp_buf, 0x1000000);
		break;
	case 8: // unmount partition with id [cmdbuf]
		ksceIoUmount(cmdbuf, 0, 0, 0);
		ksceIoUmount(cmdbuf, 1, 0, 0);
		opret = ksceIoMount(cmdbuf, NULL, 2, 0, 0, 0);
		break;
	case 9: // redirect writes to gc-sd & skip snvs bl sha update flag
		redir_writes = !redir_writes;
		opret = redir_writes;
		break;
	case 10: // mount custom blkpath as grw0
		memset(cpart_k, 0, 64);
		ksceKernelMemcpyUserToKernel(cpart_k, (uintptr_t)cmdbuf, 64);
		opret = cmount_part(cpart_k);
		break;
	case 11: // update inactive bl sha256 in SNVS
		opret = siofix(update_snvs_sha256);
		break;
	case 12: // get enso install state (0/1)
		opret = is_prenso;
		break;
	case 13: // skip bl personalize
		memset(bl_buf, 0, 0x400000);
		memcpy(bl_buf, fsp_buf, 0x400000);
		use_new_bl = 1;
		opret = (*(uint32_t*)bl_buf == 0x32424c53) ? 0 : -1;
		break;
	case 14: // set fwrpoint path
		opret = ksceKernelMemcpyUserToKernel(src_k, (uintptr_t)cmdbuf, 64);
		fwrpoint = src_k;
		break;
	case 15: // copy x200 from fwtool real mbr buf to user buf
		opret = ksceKernelMemcpyKernelToUser((uintptr_t)cmdbuf, real_mbr, 0x200);
		break;
	case 16: // get client-lock state
		opret = (is_locked) ? -1 : 0;
		is_locked = 1;
		break;
	case 17: // skip crc checks flag
		skip_int_chk = !skip_int_chk;
		opret = skip_int_chk;
		break;
	case 18: // check if ARM KBL FWV is not below minfw
		if (*(uint32_t*)(bl_buf + (*(uint32_t*)(bl_buf + 0xE0) * 0x200)) != 0x454353 || *(uint32_t*)(bl_buf + (*(uint32_t*)(bl_buf + 0xE0) * 0x200) + 0x92) < minfw)
			opret = -1;
		else
			opret = 0;
		break;
	case 19: // change perf mode
		opret = set_perf_mode(cmdbuf);
		break;
	case 20: // copy x200 from dualOS boot record to user
		opret = ksceKernelMemcpyKernelToUser((uintptr_t)cmdbuf, dualos_smallbr, 0x200);
		break;
	case 21: // wipe the dualOS superblock
		if (*(uint32_t*)(dualos_smallbr + 12))
			opret = default_write(*(uint32_t*)(real_mbr + 0x24) - DOS_RESERVED_SZ, (bl_buf + 0x800), 1);
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
	upmgr_ln = ksceKernelSearchModuleByName("SceSblUpdateMgr");
	if (upmgr_ln < 0)
		return SCE_KERNEL_START_FAILED;

	LOG("reserving 32MB cached & 4MB uncached...\n");
	ksceKernelGetMemBlockBase(ksceKernelAllocMemBlock("fsw_p", 0x10C0D006, 0x1000000, NULL), (void**)&fsp_buf);
	ksceKernelGetMemBlockBase(ksceKernelAllocMemBlock("gz_p", 0x10C0D006, 0x1000000, NULL), (void**)&gz_buf);
	ksceKernelGetMemBlockBase(ksceKernelAllocMemBlock("bl_p", 0x10C08006, 0x400000, NULL), (void**)&bl_buf);
	if (fsp_buf == NULL || gz_buf == NULL || bl_buf == NULL)
		return SCE_KERNEL_START_FAILED;

	memset(mbr, 0, 0x200);
	memset(real_mbr, 0, 0x200);
	fwimage = "ux0:data/fwtool/fwimage.bin";
	fwrpoint = "ux0:data/fwtool/fwrpoint.bin";

	LOG("getting console info...\n");
	fw = *(uint32_t*)(*(int*)(ksceSysrootGetSysbase() + 0x6c) + 4);
	minfw = *(uint32_t*)(*(int*)(ksceSysrootGetSysbase() + 0x6c) + 8);
	cur_dev = ksceSblAimgrIsTest() ? 0 : (ksceSblAimgrIsTool() ? 1 : (ksceSblAimgrIsDEX() ? 2 : (ksceSblAimgrIsCEX() ? 3 : 4)));
	emmc = ksceSdifGetSdContextPartValidateMmc(0);
	if (emmc == 0 || module_get_offset(KERNEL_PID, ksceKernelSearchModuleByName("SceSdif"), 0, 0x3e7d, (uintptr_t*)&read_real_mmc) < 0 || read_real_mmc == NULL)
		return SCE_KERNEL_START_FAILED;
	if (ksceSdifReadSectorMmc(emmc, 0, mbr, 1) < 0 // read currently used MBR
		|| ksceSdifReadSectorMmc(emmc, 2, fsp_buf, 0x2E) < 0 // read enso area
		|| read_real_mmc(emmc, 0, real_mbr, 1) < 0 // read the real MBR
		|| read_real_mmc(emmc, *(uint32_t*)(real_mbr + 0x24) - DOS_RESERVED_SZ, dualos_smallbr, 1) < 0) // read dualOS boot record
		return SCE_KERNEL_START_FAILED;
	is_prenso = get_enso_state();
	perv_clkm = (uint32_t*)pa2va(0xE3103000);
	perv_clks = (uint32_t*)pa2va(0xE3103004);
	LOG("firmware: 0x%08X\nmin firmware: 0x%08X\ndevice: %s\nenso: %s\nemmctx: 0x%X\n", fw, minfw, target_dev[cur_dev], (is_prenso) ? "YES" : "NO", emmc);
	if (fw < 0x03600000 || fw > 0x03730011)
		return SCE_KERNEL_START_FAILED;

	LOG("init done\n\n---------WAITING_4_U_CMDN---------\n\n");

	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void* args) {
	return SCE_KERNEL_STOP_SUCCESS;
}
