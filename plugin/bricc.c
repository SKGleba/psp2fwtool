#include "tai_compat.h"
#include "bricc.h"

int ctx = -1;
static int (* init_sc)() = NULL;
static int (* prep_sm)() = NULL;
static int (* personalize_slsk)() = NULL;
void *fsp_buf = NULL;

typedef struct{
	char* inp;
	char* oup;
	uint32_t sz;
	int wmode;
	int ret;
}il_args;

volatile il_args dt;

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
			LOG("git gud\n");
			return 0;
		} else {
			LOG("NG; ret=0x%X\n", ret);
		}
	} else {
		LOG("NG\n");
	}
	return 22;
}

// Personalize & prep slb2
static int work_slb2() {
	char *src = dt.inp;
	char *dest = dt.oup;
	uint32_t size = dt.sz;
	int wmode = dt.wmode;
	LOG("personalize_work_slb2: %s -> %s, 0x%lX, wm: 0x%X\n", src, dest, size, wmode);
	int fd;
	ksceKernelCpuDcacheAndL2WritebackInvalidateRange(fsp_buf, size);
	memset(fsp_buf, 0xAA, size);
	SceIoStat stat;
	int stat_ret = ksceIoGetstat(src, &stat);
	if(stat_ret < 0){
		LOG("wonew_fopread_err\n");
		dt.ret = 2;
		return 0;
	} else {
		fd = ksceIoOpen(src, SCE_O_RDONLY, 0);
		ksceIoRead(fd, fsp_buf, stat.st_size);
		ksceIoClose(fd);
	}
	dt.ret = personalize_buf(fsp_buf);
	LOG("WRITING... ");
	if (wmode == 1) {
		fd = ksceIoOpen(dest, SCE_O_RDWR, 0777);
	} else {
		fd = ksceIoOpen(dest, SCE_O_WRONLY | SCE_O_TRUNC | SCE_O_CREAT, 6);
	}
	ksceIoWrite(fd, fsp_buf, size);
	ksceIoClose(fd);
	LOG("OK!\n");
	return 0;
}

// Work a fs
static int work_fs() {
	char *src = dt.inp;
	char *dest = dt.oup;
	uint32_t fsz = dt.sz;
	int wmode = dt.wmode;
	LOG("work_fs: %s -> %s, 0x%lX, wm: 0x%X\n", src, dest, fsz, wmode);
	int fd, wfd;
	uint32_t size = fsz;
	if (size > 0x800000) size = 0x800000;
	ksceKernelCpuDcacheAndL2WritebackInvalidateRange(fsp_buf, size);
	LOG("WRITING... ");
	SceIoStat stat;
	int stat_ret = ksceIoGetstat(src, &stat);
	if(stat_ret < 0){
		LOG("wonew_fopread_err\n");
		dt.ret = 2;
		return 0;
	} else {
		fd = ksceIoOpen(src, SCE_O_RDONLY, 0);
	}
	if (wmode == 1) {
		wfd = ksceIoOpen(dest, SCE_O_RDWR, 0777);
	} else {
		wfd = ksceIoOpen(dest, SCE_O_WRONLY | SCE_O_TRUNC | SCE_O_CREAT, 6);
	}
	uint32_t i = 0;
	while ((i + size) < (fsz + 1)) {
		memset(fsp_buf, 0xAA, size);
		ksceIoRead(fd, fsp_buf, size);
		ksceIoWrite(wfd, fsp_buf, size);
		i = i + size;
	}
	if (i < fsz) {
		size = fsz - i;
		memset(fsp_buf, 0xAA, size);
		ksceIoRead(fd, fsp_buf, size);
		ksceIoWrite(wfd, fsp_buf, size);
	}
	ksceIoClose(fd);
	ksceIoClose(wfd);
	LOG("OK!\n");
	return 0;
}

// Rewrite a fs
static int rewrite_fs() {
	char *src = dt.inp;
	char *dest = dt.oup;
	uint32_t fsz = dt.sz;
	LOG("rewrite_fs: %s -> %s, 0x%lX\n", src, dest, fsz);
	int wfd;
	uint32_t size = fsz;
	if (size > 0x800000) {
		dt.ret = 7;
		return 0;
	}
	ksceKernelCpuDcacheAndL2WritebackInvalidateRange(fsp_buf, size);
	LOG("REWRITING... ");
	wfd = ksceIoOpen(dest, SCE_O_RDWR, 0777);
	memset(fsp_buf, 0, size);
	ksceIoRead(wfd, fsp_buf, size);
	ksceIoClose(wfd);
	wfd = ksceIoOpen(dest, SCE_O_RDWR, 0777);
	ksceIoWrite(wfd, fsp_buf, size);
	ksceIoClose(wfd);
	LOG("OK!\n");
	return 0;
}

int siofix(void *func) {
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

static int personalize_prep_slb2(char *src, char *dest, uint32_t size) {
	dt.inp = src;
	dt.oup = dest;
	dt.sz = size;
	dt.wmode = 0;
	dt.ret = 0;
	work_slb2();
	return dt.ret;
}

static int personalize_flash_slb2(char *src, char *dest, uint32_t size) {
	int state = 0;
	ENTER_SYSCALL(state);
	dt.inp = src;
	dt.oup = dest;
	dt.sz = size;
	dt.wmode = 1;
	dt.ret = 0;
	siofix(work_slb2);
	EXIT_SYSCALL(state);
	return dt.ret;
}

static int prep_fs(char *src, char *dest, uint32_t fsz) {
	dt.inp = src;
	dt.oup = dest;
	dt.sz = fsz;
	dt.wmode = 0;
	dt.ret = 0;
	work_fs();
	return dt.ret;
}

static int flash_fs(char *src, char *dest, uint32_t fsz) {
	int state = 0;
	ENTER_SYSCALL(state);
	dt.inp = src;
	dt.oup = dest;
	dt.sz = fsz;
	dt.wmode = 1;
	dt.ret = 0;
	siofix(work_fs);
	EXIT_SYSCALL(state);
	return dt.ret;
}

static int proxy_rewrite(char *src, char *dest, uint32_t fsz) {
	int state = 0;
	ENTER_SYSCALL(state);
	dt.inp = src;
	dt.oup = dest;
	dt.sz = fsz;
	dt.ret = 0;
	siofix(rewrite_fs);
	EXIT_SYSCALL(state);
	return dt.ret;
}

int skbricWork(int opn) {
	int ret = -1;
	if (opn == 0) {
		ret = personalize_prep_slb2("ux0:data/bricc/slb2.bin", "ux0:data/bricc/slb2_act.img", 0x400000);
		LOG("personalize_prep_slb2: 0x%X\n", ret);
	} else if (opn == 1) {
		ret = prep_fs("ux0:data/bricc/os0.bin", "ux0:data/bricc/os0_act.img", 0x1000000);
		LOG("prep_os0: 0x%X\n", ret);
	} else if (opn == 2) {
		ret = prep_fs("ux0:data/bricc/vs0.bin", "ux0:data/bricc/vs0.img", 0x10000000);
		LOG("prep_vs0: 0x%X\n", ret);
	} else if (opn == 3) {
		ret = personalize_flash_slb2("ux0:data/bricc/slb2.bin", "sdstor0:int-lp-act-sloader", 0x400000);
		LOG("personalize_flash_slb2: 0x%X\n", ret);
	} else if (opn == 4) {
		ret = flash_fs("ux0:data/bricc/os0.bin", "sdstor0:int-lp-act-os", 0x1000000);
		LOG("flash_os0: 0x%X\n", ret);
	} else if (opn == 5) {
		ret = flash_fs("ux0:data/bricc/vs0.bin", "sdstor0:int-lp-ign-vsh", 0x10000000);
		LOG("flash_vs0: 0x%X\n", ret);
	} else if (opn == 6) {
		ret = proxy_rewrite("sdstor0:int-lp-act-entire", "sdstor0:int-lp-act-entire", 0x200);
		LOG("rewrite_mbr: 0x%X\n", ret);
	} else if (opn == 53) {
		ret = skip_bootloader_chk();
		LOG("skip_bl_chk: 0x%X\n", ret);
	} else if (opn == 69) {
		ret = dt.ret;
	}
	return ret;
}

int module_start(SceSize argc, const void *args)
{
	LOG_START("\nbricc vx started!\n");
	dt.ret = 34;
	int sysroot = ksceKernelGetSysbase();
	int ret = 0, doflash = 69;
	int memblk = ksceKernelAllocMemBlock("fsw_p", 0x10C0D006, 0x800000, NULL);
    ksceKernelGetMemBlockBase(memblk, (void**)&fsp_buf);
	LOG("fw: 0x%lX\n", *(uint32_t *)(*(int *)(sysroot + 0x6c) + 4));
	if (doflash == 0) {
		LOG("\n---------STAGE 2: SC_INIT---------\n\n");
		ret = skip_bootloader_chk();
		if (ret != 0)
			return SCE_KERNEL_START_FAILED;
		LOG("\n---------STAGE 3: PREP_FS---------\n\n");
		ret = personalize_prep_slb2("ux0:data/bricc/slb2.bin", "ux0:data/bricc/slb2_act.img", 0x400000);
		LOG("personalize_prep_slb2: 0x%X\n", ret);
		if (ret != 0)
			return SCE_KERNEL_START_FAILED;
		ret = prep_fs("ux0:data/bricc/os0.bin", "ux0:data/bricc/os0_act.img", 0x1000000);
		LOG("prep_os0: 0x%X\n", ret);
		if (ret != 0)
			return SCE_KERNEL_START_FAILED;
		ret = prep_fs("ux0:data/bricc/vs0.bin", "ux0:data/bricc/vs0.img", 0x10000000);
		LOG("prep_vs0: 0x%X\n", ret);
		if (ret != 0)
			return SCE_KERNEL_START_FAILED;
		ksceIoRemove("ux0:id.dat");
		LOG("\nbricc vx finished!\n");
		if (ret != 0)
			return SCE_KERNEL_START_FAILED;
		ksceKernelFreeMemBlock(memblk);
	} else if (doflash == 1) {
		LOG("\n---------STAGE 2: SC_INIT---------\n\n");
		ret = skip_bootloader_chk();
		if (ret != 0)
			return SCE_KERNEL_START_FAILED;
		LOG("\n---------STAGE 420: FLASH---------\n\n");
		ret = personalize_flash_slb2("ux0:data/bricc/slb2.bin", "sdstor0:int-lp-act-sloader", 0x400000);
		LOG("personalize_flash_slb2: 0x%X\n", ret);
		if (ret != 0)
			return SCE_KERNEL_START_FAILED;
		ret = flash_fs("ux0:data/bricc/os0.bin", "sdstor0:int-lp-act-os", 0x1000000);
		LOG("flash_os0: 0x%X\n", ret);
		if (ret != 0)
			return SCE_KERNEL_START_FAILED;
		ret = flash_fs("ux0:data/bricc/vs0.bin", "sdstor0:int-lp-ign-vsh", 0x10000000);
		LOG("flash_vs0: 0x%X\n", ret);
		if (ret != 0)
			return SCE_KERNEL_START_FAILED;
		ksceIoRemove("ux0:id.dat");
		LOG("\nbricc vx finished!\n");
		if (ret != 0)
			return SCE_KERNEL_START_FAILED;
		ksceKernelFreeMemBlock(memblk);
	} else if (doflash == 2) {
		LOG("\n---------STAGE 2: SC_INIT---------\n\n");
		ret = skip_bootloader_chk();
		if (ret != 0)
			return SCE_KERNEL_START_FAILED;
		LOG("\n---------STAGE WTF_NUFFIN---------\n\n");
		return SCE_KERNEL_START_FAILED;
	} else if (doflash == 69) {
		LOG("\n---------WAITING_4_U_CMDN---------\n\n");
		return SCE_KERNEL_START_SUCCESS;
	}
	
	return SCE_KERNEL_START_SUCCESS;
}

void _start() __attribute__ ((weak, alias ("module_start")));

int module_stop(SceSize argc, const void *args)
{
	return SCE_KERNEL_STOP_SUCCESS;
}
