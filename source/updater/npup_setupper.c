/* THIS FILE IS A PART OF PSP2FWTOOL
 *
 * Copyright (C) 2019-2021 skgleba
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <string.h>
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
#include "../compile_swu_launch.h"

#define printf sceClibPrintf

#define KMOD_LOC "ud0:swu_launch.skprx"
#define NPUP_LOC "ud0:PSP2UPDATE/PSP2UPDAT.PUP"

bool file_exists(const char* file) {
    int fd = sceIoOpen(file, SCE_O_RDONLY, 0);
    if (fd >= 0) {
        sceIoClose(fd);
        return true;
    }
    return false;
}

#define CHUNK_SIZE 256 * 1024

int fcp(const char* src, const char* dst) {
    printf("[SUP] Copying %s -> %s (file)... ", src, dst);
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
    printf("%s\n", (res < 0) ? "FAILED" : "OK");
    return res;
}

int remove_dir(const char* path) {
    SceUID dfd = sceIoDopen(path);
    if (dfd >= 0) {
        int res = 0;

        do {
            SceIoDirent dir;
            memset(&dir, 0, sizeof(SceIoDirent));

            res = sceIoDread(dfd, &dir);
            if (res > 0) {
                char* new_path = malloc(strlen(path) + strlen(dir.d_name) + 2);
                snprintf(new_path, 1024, "%s/%s", path, dir.d_name);
                remove_dir(new_path);
                free(new_path);
            }
        } while (res > 0);

        sceIoDclose(dfd);

        return sceIoRmdir(path);
    } else {
        return sceIoRemove(path);
    }
}

// by yifanlu
int extract(const char* pup, const char* psp2swu) {
    int inf, outf;

    if ((inf = sceIoOpen(pup, SCE_O_RDONLY, 0)) < 0) {
        return -1;
    }

    if ((outf = sceIoOpen(psp2swu, SCE_O_CREAT | SCE_O_WRONLY | SCE_O_TRUNC, 6)) < 0) {
        return -1;
    }

    int ret = -1;
    int count;

    if (sceIoLseek(inf, 0x18, SCE_SEEK_SET) < 0) {
        goto end;
    }

    if (sceIoRead(inf, &count, 4) < 4) {
        goto end;
    }

    if (sceIoLseek(inf, 0x80, SCE_SEEK_SET) < 0) {
        goto end;
    }

    struct {
        uint64_t id;
        uint64_t off;
        uint64_t len;
        uint64_t field_18;
    } __attribute__((packed)) file_entry;

    for (int i = 0; i < count; i++) {
        if (sceIoRead(inf, &file_entry, sizeof(file_entry)) != sizeof(file_entry)) {
            goto end;
        }

        if (file_entry.id == 0x200) {
            break;
        }
    }

    if (file_entry.id == 0x200) {
        char buffer[1024];
        size_t rd;

        if (sceIoLseek(inf, file_entry.off, SCE_SEEK_SET) < 0) {
            goto end;
        }

        while (file_entry.len && (rd = sceIoRead(inf, buffer, sizeof(buffer))) > 0) {
            if (rd > file_entry.len) {
                rd = file_entry.len;
            }
            sceIoWrite(outf, buffer, rd);
            file_entry.len -= rd;
        }

        if (file_entry.len == 0) {
            ret = 0;
        }
    }

end:
    sceIoClose(inf);
    sceIoClose(outf);
    return ret;
}

int main(int argc, char* argv[]) {
    printf("[SUP] Hello from cui_setupper.self!\n[SUP] argc : %d\n", argc);
    
    sceKernelPowerLock(0);
    
    char* package = "host0:PSP2UPDAT.PUP";
    if (argc) {
        for (int i = 0; i < argc; i++) {
            printf("[SUP] argv %d : %s\n", i, argv[i]);
        }
        package = argv[2];
    }

    if (!file_exists(package)) {
        printf("[SUP] update package not found! %s\n", package);
        goto BYESUP;
    }

    printf("[SUP] Cleaning ud0:...");
    remove_dir("ud0:");
    sceIoMkdir("ud0:PSP2UPDATE", 0777);
    printf("OK\n");

    printf("[SUP] Unpacking the swu launcher...");
    int kfd = sceIoOpen(KMOD_LOC, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (kfd < 0) {
        printf("FAILED\n");
        goto BYESUP;
    }
    sceIoWrite(kfd, (void*)compile_swu_launch_skprx, compile_swu_launch_skprx_len);
    sceIoClose(kfd);
    printf("OK\n");

    if (fcp(package, NPUP_LOC) < 0)
        goto BYESUP;

    printf("[SUP] Extracting psp2swu.self...");
    if (extract("ud0:PSP2UPDATE/PSP2UPDAT.PUP", "ud0:PSP2UPDATE/psp2swu.self") < 0) {
        printf("FAILED\n");
        goto BYESUP;
    }
    printf("OK\n");

    sceKernelPowerUnlock(0);

    printf("[SUP] Launching the updater...");
    tai_module_args_t argg;
    argg.size = sizeof(argg);
    argg.pid = KERNEL_PID;
    argg.args = 0;
    argg.argp = NULL;
    argg.flags = 0;
    if (taiLoadStartKernelModuleForUser(KMOD_LOC, &argg) < 0)
        printf("FAILED\n");
    else {
        sceKernelDelayThread(1 * 1000 * 1000);
        printf("OK\n");
        sceKernelDelayThread(2 * 1000 * 1000);
        printf("[SUP] you shouldnt be seeing this\n");
        sceKernelDelayThread(2 * 1000 * 1000);
        printf("[SUP] probs the kmodule failed (again)\n");
        sceKernelDelayThread(2 * 1000 * 1000);
        printf("[SUP] ehh, bug this\n");
        sceKernelDelayThread(10 * 1000 * 1000);
    }

BYESUP:
    sceKernelPowerUnlock(0);
    printf("[SUP] bye from cui_setupper.self!\n");
    return 0;
}