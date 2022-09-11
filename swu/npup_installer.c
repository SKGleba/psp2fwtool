/* THIS FILE IS A PART OF PSP2FWTOOL
 *
 * Copyright (C) 2019-2021 skgleba
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <malloc.h>
#include <taihen.h>
#include <psp2/ctrl.h>
#include <psp2/power.h>
#include <psp2/appmgr.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/vshbridge.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include "debugScreen.h"
#include "../plugin/fwtool.h"
#include "../app/fwtool_funcs.h"
#include "Archives.h"
#include "../compile_fwtool.h"

#define KMOD_LOC "ud0:fwtool.skprx"
#define ADDC_LOC "ud0:fwtool/"
#define NPUP_LOC "ud0:PSP2UPDATE/PSP2UPDAT.PUP"

#define DBG(...) sceClibPrintf(__VA_ARGS__);

#define printf(...)                        \
	do {                                   \
		if (uout)						   \
			sceClibPrintf(__VA_ARGS__);    \
		psvDebugScreenPrintf(__VA_ARGS__); \
	} while (0)

#define COLORPRINTF(color, ...)                \
	do {                                       \
		psvDebugScreenSetFgColor(color);       \
		if (uout)						   	   \
			sceClibPrintf(__VA_ARGS__);		   \
		psvDebugScreenPrintf(__VA_ARGS__);     \
		psvDebugScreenSetFgColor(COLOR_WHITE); \
	} while (0)

static char src_u[64];
const int redir_writes = 0, skip_int_chk = 0, uout = 0;

void main_check_stop(uint32_t code) {
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(0, &pad, 1);
    if (!(pad.buttons & SCE_CTRL_TRIANGLE))
        return;
    COLORPRINTF(COLOR_RED, "REQ_STOP | CODE: 0x%X\n", code);
    COLORPRINTF(COLOR_YELLOW, "Feature not supported in this version!\n");
}

#include "../app/fwimg.c"

int unzip(const char* src, const char* dst) {
    Zip* handle = ZipOpen(src);
    int ret = ZipExtract(handle, NULL, dst);
    ZipClose(handle);
    return ret;
}

int rmDir(const char* path) {
    SceUID dfd = sceIoDopen(path);
    if (dfd >= 0) {
        sceClibPrintf("Removing %s (dir)\n", path);
        SceIoStat stat;
        sceClibMemset(&stat, 0, sizeof(SceIoStat));
        sceIoGetstatByFd(dfd, &stat);

        stat.st_mode |= SCE_S_IWUSR;

        int res = 0;

        do {
            SceIoDirent dir;
            sceClibMemset(&dir, 0, sizeof(SceIoDirent));

            res = sceIoDread(dfd, &dir);
            if (res > 0) {
                char* new_path = malloc(strlen(path) + strlen(dir.d_name) + 2);
                sceClibSnprintf(new_path, 1024, "%s%s%s", path, hasEndSlash(path) ? "" : "/", dir.d_name);

                int ret = 0;

                if (SCE_S_ISDIR(dir.d_stat.st_mode)) {
                    ret = rmDir(new_path);
                } else {
                    ret = sceIoRemove(new_path);
                }

                free(new_path);

                if (ret < 0) {
                    sceIoDclose(dfd);
                    return ret;
                }
            }
        } while (res > 0);

        sceIoDclose(dfd);
    } else
        return sceIoRemove(path);

    return 1;
}


bool file_exists(const char* file) {
    int fd = sceIoOpen(file, SCE_O_RDONLY, 0);
    if (fd >= 0) {
        sceIoClose(fd);
        return true;
    }
    return false;
}

void erroff() {
    COLORPRINTF(COLOR_RED, "ERR_REQ_OFF\n");
    SceCtrlData pad;
    COLORPRINTF(COLOR_YELLOW, "Press %s.\n", "CROSS to reset or CIRCLE to exit");
    while (1) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        if (pad.buttons & SCE_CTRL_CIRCLE)
            break;
        else if (pad.buttons & SCE_CTRL_CROSS) {
            scePowerRequestColdReset();
            break;
        }
        sceKernelDelayThread(200 * 1000);
    }
    sceKernelExitProcess(0);
    while (1) {};
}

int main(int argc, char* argv[]) {
    psvDebugScreenInit();
    psvDebugScreenClear(COLOR_BLACK);
    COLORPRINTF(COLOR_CYAN, FWTOOL_VERSION_STR "\n");

    if (!file_exists(NPUP_LOC)) {
        printf("update package not found!\n");
        erroff();
    }

    if (!file_exists(KMOD_LOC)) {
        printf("\nUnpacking the supply module...\n");
        int kfd = sceIoOpen(KMOD_LOC, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        sceIoWrite(kfd, (void*)compile_fwtool_skprx, compile_fwtool_skprx_len);
        sceIoClose(kfd);
        printf("\nLoading the supply module...\n");
        tai_module_args_t argg;
        argg.size = sizeof(argg);
        argg.pid = KERNEL_PID;
        argg.args = 0;
        argg.argp = NULL;
        argg.flags = 0;
        // load fwtool kernel
        if (taiLoadStartKernelModuleForUser(KMOD_LOC, &argg) < 0) {
            printf("failed to load the kernel module!\n");
            erroff();
        } else
            sceAppMgrLoadExec("app0:psp2swu.self", NULL, NULL);
    } else
        sceIoRemove(KMOD_LOC);

    if (fwtool_talku(CMD_GET_LOCK_STATE, 0) < 0) {
        printf("get_bind_status failed, the kernel module was probably locked by another process\n");
        erroff();
    }

    printf("\nChecking the update package...\n");
    npup_hdr puphdr;
    memset(&puphdr, 0, sizeof(npup_hdr));
    int fd = sceIoOpen(NPUP_LOC, SCE_O_RDONLY, 0);
    sceIoPread(fd, &puphdr, sizeof(npup_hdr), 0);
    sceIoClose(fd);
    if (puphdr.magic != NPUP_MAGIC || puphdr.package_version != NPUP_VERSION) {
        printf("unknown update package magic/version!\n");
        erroff();
    }

    printf("Unpacking the update package...\n");
    
    rmDir(ADDC_LOC);
    sceIoMkdir(ADDC_LOC, 6);
    if (puphdr.addcont_all_info.data_length) {
        void* tmpb = calloc(1, (uint32_t)puphdr.addcont_all_info.data_length);
        if (!tmpb) {
            printf("could not alloc addcont_all buf!\n");
            erroff();
        }
        fd = sceIoOpen(NPUP_LOC, SCE_O_RDONLY, 0);
        sceIoPread(fd, tmpb, puphdr.addcont_all_info.data_length, puphdr.addcont_all_info.data_offset);
        sceIoClose(fd);
        fd = sceIoOpen(ADDC_LOC "addcont_all.zip", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        sceIoWrite(fd, tmpb, puphdr.addcont_all_info.data_length);
        sceIoClose(fd);
        free(tmpb);

        if (unzip(ADDC_LOC "addcont_all.zip", ADDC_LOC) < 0) {
            printf("could not extract addcont (ALL)!\n");
            erroff();
        }
    }

    if (vshSblAimgrIsGenuineDolce()) {
        if (puphdr.addcont_dolce_info.data_length) {
            void* tmpb = calloc(1, (uint32_t)puphdr.addcont_dolce_info.data_length);
            if (!tmpb) {
                printf("could not alloc addcont_dolce buf!\n");
                erroff();
            }
            fd = sceIoOpen(NPUP_LOC, SCE_O_RDONLY, 0);
            sceIoPread(fd, tmpb, puphdr.addcont_dolce_info.data_length, puphdr.addcont_dolce_info.data_offset);
            sceIoClose(fd);
            fd = sceIoOpen(ADDC_LOC "addcont_dolce.zip", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
            sceIoWrite(fd, tmpb, puphdr.addcont_dolce_info.data_length);
            sceIoClose(fd);
            free(tmpb);

            if (unzip(ADDC_LOC "addcont_dolce.zip", ADDC_LOC) < 0) {
                printf("could not extract addcont (DOLCE)!\n");
                erroff();
            }
        }
    } else if (puphdr.addcont_vita_info.data_length) {
        void* tmpb = calloc(1, (uint32_t)puphdr.addcont_vita_info.data_length);
        if (!tmpb) {
            printf("could not alloc addcont_vita buf!\n");
            erroff();
        }
        fd = sceIoOpen(NPUP_LOC, SCE_O_RDONLY, 0);
        sceIoPread(fd, tmpb, puphdr.addcont_vita_info.data_length, puphdr.addcont_vita_info.data_offset);
        sceIoClose(fd);
        fd = sceIoOpen(ADDC_LOC "addcont_vita.zip", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        sceIoWrite(fd, tmpb, puphdr.addcont_vita_info.data_length);
        sceIoClose(fd);
        free(tmpb);

        if (unzip(ADDC_LOC "addcont_vita.zip", ADDC_LOC) < 0) {
            printf("could not extract addcont (VITA)!\n");
            erroff();
        }
    }

    if (puphdr.disclaimer_info.data_length) {
        void* tmpb = calloc(1, (uint32_t)puphdr.disclaimer_info.data_length);
        if (!tmpb) {
            printf("could not alloc disclaimer buf!\n");
            erroff();
        }
        fd = sceIoOpen(NPUP_LOC, SCE_O_RDONLY, 0);
        sceIoPread(fd, tmpb, puphdr.disclaimer_info.data_length, puphdr.disclaimer_info.data_offset);
        sceIoClose(fd);

        psvDebugScreenClear(COLOR_BLACK);
        COLORPRINTF(COLOR_YELLOW, "Disclaimer from the repack author:\n\n");
        psvDebugScreenSetFgColor(COLOR_WHITE);
        psvDebugScreenPuts(tmpb);
        COLORPRINTF(COLOR_YELLOW, "\n\nPress CROSS to accept and continue or CIRCLE to exit\n");
        SceCtrlData pad;
        while (1) {
            sceCtrlPeekBufferPositive(0, &pad, 1);
            if (pad.buttons & SCE_CTRL_CIRCLE) {
                free(tmpb);
                scePowerRequestColdReset();
                sceKernelExitProcess(0);
            }
            if (pad.buttons & SCE_CTRL_CROSS)
                break;
            sceKernelDelayThread(100 * 1000);
        }
        free(tmpb);
    }

    psvDebugScreenClear(COLOR_BLACK);
    printf("FWTOOL::FLASHTOOL started\n");

    int ret = update_default(NPUP_LOC, 1, (uint32_t)puphdr.fwimage_info.data_offset);
    if (!ret) {
        COLORPRINTF(COLOR_CYAN, "\nALL DONE. ");
        fwimg_get_pkey(0);
        sceKernelDelayThread(1 * 1000 * 1000);
        sceKernelExitProcess(0);
    } else if (ret == 70) {
        COLORPRINTF(COLOR_CYAN, "\nALL DONE. REBOOTING");
        rmDir(ADDC_LOC);
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