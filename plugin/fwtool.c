#include "tai_compat.h"
#include "fwtool.h"
#include "logging.h"
#include "crc32.c"

static int opret = -1, pinp = 0, debugging_mode = 0, cur_dev = 6, e2xflashed = 0;
static int (* init_sc)() = NULL;
static int (* prep_sm)() = NULL;
static int (* personalize_slsk)() = NULL;
void *fsp_buf = NULL, *gz_buf = NULL;
static uint32_t s2loff = 0, s2lsz = 0;

volatile il_mode ilm;
volatile pkg_fs_etr fs_args;

static char src_k[64];
static char dst_k[64];

// Skip firmware ver checks on bootloaders 0xdeadbeef
static int skip_bootloader_chk(void) {
	uint32_t arg0, arg1;
	int ret = -1;
	tai_module_info_t info;			
	info.size = sizeof(info);		
	LOG("getting mod info for SceSblUpdateMgr... ");
	if (taiGetModuleInfoForKernel(KERNEL_PID, "SceSblUpdateMgr", &info) >= 0) {
		LOG("gud\n");
		LOG("patching sc_init for unused\n");
		char cmp_err[2] = {0x69, 0x28};
		char cmp_ret[4] = {0x46, 0xf2, 0x05, 0x23};
		INJECT("SceSblUpdateMgr", 0x87a4, cmp_err, sizeof(cmp_err));
		INJECT("SceSblUpdateMgr", 0x898c, cmp_ret, sizeof(cmp_ret));
		module_get_offset(KERNEL_PID, info.modid, 0, 0x8639, &prep_sm); 
		module_get_offset(KERNEL_PID, info.modid, 0, 0x8705, &init_sc); 
		LOG("calling sm prep... ");
		ret = prep_sm(&arg0, &arg1, 1, 0xffffffff);
		LOG("ret 0x%X, 0x%lX, 0x%lX\n", ret, arg0, arg1);
		arg0 = 0xFFFFFFFF;
		arg1 = 0xFFFFFFFF;
		LOG("calling sc init 0x%lX, 0x%lX... ", arg0, arg1);
		ret = init_sc(&arg0, &arg1);
		LOG("ret 0x%X\n", ret);
		if (ret == 0x800F6205)
			return 0;
	} else {
		LOG("NG\n");
	}
	return 1;
}

// Skip firmware ver checks on bootloaders PM
static int (* pload_sm)() = NULL;
static int (* pset_pm)() = NULL;
static int (* pget_pm)() = NULL;
static int (* pstop_sm)() = NULL;
static int skip_pm_chk(void) {
	uint32_t arg0, arg1;
	int ret = -1;
	tai_module_info_t info;			
	info.size = sizeof(info);		
	LOG("getting mod info for SceSblPostSsMgr... ");
	if (taiGetModuleInfoForKernel(KERNEL_PID, "SceSblPostSsMgr", &info) >= 0) {
		LOG("gud\n");
		module_get_offset(KERNEL_PID, info.modid, 0, 0x8825, &pload_sm); 
		module_get_offset(KERNEL_PID, info.modid, 0, 0x89a1, &pget_pm); 
		module_get_offset(KERNEL_PID, info.modid, 0, 0x8a41, &pset_pm); 
		module_get_offset(KERNEL_PID, info.modid, 0, 0x8891, &pstop_sm); 
		LOG("calling sm prep... ");
		ret = pload_sm("os0:sm/pm_sm.self");
		ret = pget_pm(&arg0, &arg1);
		LOG("get 0x%lX, 0x%lX\n", arg0, arg1);
		arg0 = 0xFFFFFFFF;
		arg1 = 0xFFFFFFFF;
		LOG("calling pset_pm 0x%lX, 0x%lX, 0x%X... ", arg0, arg1, 7);
		ret = pset_pm(&arg0, &arg1, 7);
		LOG("ret 0x%X\n", ret);
		ret = pget_pm(&arg0, &arg1);
		LOG("get 0x%lX, 0x%lX\n", arg0, arg1);
		pstop_sm();
		return 0;
	} else {
		LOG("NG\n");
	}
	return 1;
}

// Personalize slb2 data @ bufaddr
static int personalize_buf(void *bufaddr) {
	int ret = -1, sm_ret = -1;
	tai_module_info_t info;			
	info.size = sizeof(info);		
	LOG("magic: 0x%lX\n", *(uint32_t *)bufaddr);
	LOG("magic2: 0x%lX\n", *(uint32_t *)fsp_buf);
	LOG("getting mod info for SceSblUpdateMgr... ");
	if (taiGetModuleInfoForKernel(KERNEL_PID, "SceSblUpdateMgr", &info) >= 0) {
		LOG("gud\n");
		module_get_offset(KERNEL_PID, info.modid, 0, 0x58c5, &personalize_slsk); 
		LOG("personalize_sk... ");
		ret = personalize_slsk("second_loader.enc", "second_loader.enp", "second_loader.enp", "second_loader.enp_", fsp_buf);
		if (ret == 0) 
			ret = personalize_slsk("secure_kernel.enc", "secure_kernel.enp", "secure_kernel.enp", "secure_kernel.enp_", fsp_buf);
		if (ret == 0) {
			LOG("gud\n");
			return 0;
		} else {
			LOG("NG; ret=0x%X\n", ret);
		}
	} else {
		LOG("NG\n");
	}
	return 22;
}

static uint32_t check_block_crc32(void *buf, uint32_t exp_crc) {
	char crcbuf[0x200];
	memcpy(&crcbuf, buf, 0x200);
	uint32_t crc = crc32(0, &crcbuf, 0x200);
	if (exp_crc > 0 && exp_crc != crc)
		return -1;
	return 0;
}

// Work a fs
static void work_fs() {
	
	int usegz = (fs_args.magic == 0x69) ? 1 : ((fs_args.magic == 0x34 && fs_args.dst_sz == fs_args.pkg_sz) ? 0 : 3);
	if (usegz == 3) {
		opret = 1;
		LOG("bad entry!\n");
		return;
	}
	
	int is_blstor = (pinp == 1) ? 1 : 0;
	
	if (ilm.fmode == 1)
		snprintf(dst_k, sizeof(dst_k), "sdstor0:%s-lp-%s-%s", stor_st[fs_args.dst_etr[0]], stor_rd[fs_args.dst_etr[1]], stor_th[fs_args.dst_etr[2]]);
	
	LOG("work_fs: %s[0x%lx] -> %s[0x%lx], 0x%lX[c0x%lX], wm: 0x%X, fm: 0x%X, pb: %d\n", ilm.inp, fs_args.pkg_off, ilm.oup, fs_args.dst_off, fs_args.dst_sz, fs_args.pkg_sz, ilm.wmode, ilm.fmode, is_blstor);
	memset(gz_buf, 0, 0x1000000);
	
	LOG("R->");
	int fd, wfd;
	SceIoStat stat;
	int stat_ret = ksceIoGetstat(ilm.inp, &stat);
	opret = 2;
	if(stat_ret < 0)
		return;
	else
		fd = ksceIoOpen(ilm.inp, SCE_O_RDONLY, 0);
	ksceIoPread(fd, gz_buf, fs_args.pkg_sz, fs_args.pkg_off);
	ksceIoClose(fd);
	
	LOG("C->");
	if (check_block_crc32(gz_buf, fs_args.crc32) < 0 && !debugging_mode) {
		opret = 6;
		return;
	}
	
	LOG("D->");
	if (usegz) {
		if (ksceGzipDecompress(fsp_buf, fs_args.dst_sz, gz_buf, NULL) < 0) {
			opret = 3;
			return;
		}
	} else
		memcpy(fsp_buf, gz_buf, fs_args.pkg_sz);
	
	if (is_blstor) {
		LOG("(personalize)\n");
		opret = 5;
		if (personalize_buf(fsp_buf) != 0)
			return;
		if (ilm.target < 6) {
			s2loff = *(uint32_t *)(fsp_buf + 0x50);
			s2lsz = (uint32_t)(*(uint32_t *)(fsp_buf + 0x54) / 0x200);
		}
		opret = 7;
		if (ilm.fw_minor > 0 && *(uint16_t *)(fsp_buf + 0x245) != (uint16_t)ilm.fw_minor)
			return;
	}
		
	LOG("W... ");
	if (ilm.wmode == 1)
		wfd = ksceIoOpen(ilm.oup, SCE_O_RDWR, 0777);
	else
		wfd = ksceIoOpen(ilm.oup, SCE_O_WRONLY | SCE_O_TRUNC | SCE_O_CREAT, 6);
	opret = 4;
	if (wfd < 0)
		return;
	ksceIoPwrite(wfd, fsp_buf, fs_args.dst_sz, fs_args.dst_off);
	ksceIoClose(wfd);
	
	if (pinp == 4)
		e2xflashed = 1;
	
	LOG("OK!\n");
	
	opret = 0;
}

int find_active_os0(master_block_t *master) {
	int active_os0 = 69;
	for (size_t i = 0; i < ARRAYSIZE(master->partitions); ++i) {
		partition_t *p = &master->partitions[i];
		if (p->active == 1 && p->code == 3)
			active_os0 = i;
	}
	return active_os0;
}

// Rewrite mbr
static void rewrite_mbr() {
	int wfd;
	LOG("REWRITING MBR... ");
	opret = 1;
	wfd = ksceIoOpen("sdstor0:int-lp-act-entire", SCE_O_RDWR, 0777);
	if (wfd < 0)
		return;
	ksceIoRead(wfd, fsp_buf, 0x200);
	ksceIoClose(wfd);
	uint32_t cblpoff = (*(uint32_t *)(fsp_buf + 0x30) < 0x6000) ? 0x4000 : 0x6000;
	if (s2loff > 0)
		*(uint32_t *)(fsp_buf + 0x30) = (uint32_t)(cblpoff + s2loff);
	if (s2lsz > 0)
		*(uint32_t *)(fsp_buf + 0x34) = (uint32_t)s2lsz;
	opret = 2;
	wfd = ksceIoOpen("sdstor0:int-lp-act-entire", SCE_O_RDWR, 0777);
	if (wfd < 0)
		return;
	if (e2xflashed) {
		master_block_t master;
		memcpy(&master, fsp_buf, 0x200);
		int active_os0 = find_active_os0(&master);
		if (active_os0 < 69) {
			master.partitions[active_os0].off = 2;
			ksceIoWrite(wfd, &master, 0x200);
		}
	}
	ksceIoWrite(wfd, fsp_buf, 0x200);
	ksceIoClose(wfd);
	LOG("OK!\n");
	opret = 0;
}

static int siofix(void *func) {
	int ret = 0;
	int res = 0;
	int uid = 0;
	ret = uid = ksceKernelCreateThread("siofix", func, 64, 0x10000, 0, 0, 0);
	if (ret < 0){ret = -1; goto cleanup;}
	if ((ret = ksceKernelStartThread(uid, 0, NULL)) < 0) {ret = -1; goto cleanup;}
	if ((ret = ksceKernelWaitThreadEnd(uid, &res, NULL)) < 0) {ret = -1; goto cleanup;}
	ret = res;
cleanup:
	if (uid > 0) ksceKernelDeleteThread(uid);
	return ret;
}

int fwtool_cmd_handler(int cmd, void *cmdbuf) {
	int state = 0;
	ENTER_SYSCALL(state);
	opret = -1;
	
	if (ilm.version == 0 && cmd != 0 && cmd != 69) {
		opret = 1;
		return 1;
	}
	
	switch (cmd) {
		case 0:
			ksceKernelMemcpyUserToKernel((void *)&ilm, (uintptr_t)cmdbuf, sizeof(il_mode));
			ksceKernelMemcpyUserToKernel(src_k, (uintptr_t)ilm.inp, 64);
			ksceKernelMemcpyUserToKernel(dst_k, (uintptr_t)ilm.oup, 64);
			ilm.inp = src_k;
			ilm.oup = dst_k;
			if (debugging_mode || ilm.target > 4)
				ilm.target = cur_dev;
			opret = (ilm.version == 1) ? ((ilm.target == cur_dev) ? 0 : 3) : 2;
			LOG("set_infobuf: 0x%X\n", opret);
			break;
		case 1:
		case 2:
		case 4:
			ksceKernelMemcpyUserToKernel((void *)&fs_args, (uintptr_t)cmdbuf, sizeof(pkg_fs_etr));
			pinp = cmd;
			siofix(work_fs);
			LOG("proxy_work_fs: 0x%X\n", opret);
			break;
		case 3:
			siofix(rewrite_mbr);
			LOG("proxy_rewrite_mbr: 0x%X\n", opret);
			break;
		case 34:
			opret = skip_bootloader_chk();
			LOG("skip_bl_chk: 0x%X\n", opret);
			break;
		case 69:
			debugging_mode = 1;
			LOG("debugging mode set! skipping all checks.\n");
			opret = 0;
			break;
		default:
			break;
	}
	EXIT_SYSCALL(state);
	return opret;
}

int module_start(SceSize argc, const void *args)
{
	LOG_START("\nfwtool vx started!\n");
	
    ksceKernelGetMemBlockBase(ksceKernelAllocMemBlock("fsw_p", 0x10C0D006, 0x1000000, NULL), (void**)&fsp_buf);
	ksceKernelGetMemBlockBase(ksceKernelAllocMemBlock("gz_p", 0x10C0D006, 0x1000000, NULL), (void**)&gz_buf);
	
	LOG("fw: 0x%lX\n", *(uint32_t *)(*(int *)(ksceKernelGetSysbase() + 0x6c) + 4));
	cur_dev = ksceSblAimgrIsTest() ? 0 : (ksceSblAimgrIsTool() ? 1 : (ksceSblAimgrIsDEX() ? 2 : (ksceSblAimgrIsCEX() ? 3 : 4)));
	LOG("device: %s\n", target_dev[cur_dev]);
	
	memset((void *)&ilm, 0, sizeof(il_mode));
	
	LOG("\n---------WAITING_4_U_CMDN---------\n\n");
	
	return SCE_KERNEL_START_SUCCESS;
}

void _start() __attribute__ ((weak, alias ("module_start")));

int module_stop(SceSize argc, const void *args)
{
	return SCE_KERNEL_STOP_SUCCESS;
}
