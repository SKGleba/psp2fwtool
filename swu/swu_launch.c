#include <stdio.h>
#include <string.h>
#include <psp2kern/ctrl.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/appmgr.h>

static int launch_thread(SceSize args, void* argp) {
    int opt[52 / 4];
    memset(opt, 0, sizeof(opt));
    opt[0] = sizeof(opt);

    ksceAppMgrLaunchAppByPath("ud0:PSP2UPDATE/psp2swu.self", NULL, 0, 0, (SceAppMgrLaunchParam *)opt, NULL);

    return ksceKernelExitDeleteThread(0);
}

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize args, void* argp) {
    uint32_t state;
    ENTER_SYSCALL(state);

    SceUID thid = ksceKernelCreateThread("launch_thread", (SceKernelThreadEntry)launch_thread, 0x40, 0x1000, 0, 0, NULL);
    if (thid < 0) {
        EXIT_SYSCALL(state);
        return thid;
    }

    ksceKernelStartThread(thid, 0, NULL);

    EXIT_SYSCALL(state);
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void* argp) {
    return SCE_KERNEL_STOP_SUCCESS;
}