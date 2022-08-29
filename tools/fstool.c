/* THIS FILE IS A PART OF PSP2FWTOOL
 *
 * Copyright (C) 2019-2022 skgleba
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "stdbool.h"

#include "../plugin/fwtool.h"

#define MKMBR_SLAVE
#include "mkmbr.c"

#ifdef WINDOWS
uint32_t pread(int fd, void* buf, size_t count, off_t offset) {
    lseek(fd, offset, SEEK_SET);
    return read(fd, buf, count);
}
uint32_t pwrite(int fd, void* buf, size_t count, off_t offset) {
    lseek(fd, offset, SEEK_SET);
    return write(fd, buf, count);
}
#endif

uint64_t getSz(const char* src) {
    FILE* fp = fopen(src, "rb");
    if (fp == NULL)
        return 0;
    fseek(fp, 0L, SEEK_END);
    uint64_t sz = ftell(fp);
    fclose(fp);
    return sz;
}

uint8_t partition_name2id(char* partition) {
    for (uint8_t i = 0; i < 0x10; i++) {
        if (strcmp(partition, pcode_str[i]) == 0)
            return i;
    }
    return 0;
}

void unpacker(char* device, char* dest) {
    mkdir(dest, 0777);

    char mbr_raw[512];
    memset(mbr_raw, 0, 512);
    master_block_t* mbr = (master_block_t*)mbr_raw;
    int fp = open(device, O_RDONLY);
    if (fp < 0) {
        printf("could not open %s\n", device);
        return;
    }
    pread(fp, mbr_raw, BLOCK_SIZE, 0);

    uint64_t max_size = 0;
    if (*(uint32_t*)mbr->magic == RPOINT_MAGIC) {
        max_size = *(uint32_t*)(mbr->magic + 4);
        max_size *= BLOCK_SIZE;
        max_size += sizeof(emmcimg_super);
        pread(fp, mbr_raw, BLOCK_SIZE, sizeof(emmcimg_super));
    }

    if (memcmp(mbr->magic, SCEMBR_MAGIC, sizeof(SCEMBR_MAGIC) - 1) != 0) {
        printf("no SCE magic found\n");
        close(fp);
        return;
    }

    if (!max_size) {
        max_size = mbr->device_size;
        max_size *= BLOCK_SIZE;
    }

    void* copybuf = calloc(FSP_BUF_SZ_BLOCKS, BLOCK_SIZE);
    if (!copybuf) {
        printf("could not alloc\n");
        close(fp);
        return;
    }

    char dest_path[512];
    for (size_t i = 0; i < ARRAYSIZE(mbr->partitions); ++i) {
        partition_t* p = &mbr->partitions[i];

        if (!p->code)
            continue;

        uint64_t offset = p->off;
        uint64_t size = p->sz;
        offset *= BLOCK_SIZE;
        size *= BLOCK_SIZE;

        if (offset + size > max_size)
            continue;

        memset(dest_path, 0, 512);
        snprintf(dest_path, 512, "%s/%s-%d", dest, pcode_str[p->code], p->active);
        printf("Unpacking 0x%08X to %s\n", p->off, dest_path);
        
        int fw = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC);
        if (fw < 0) {
            printf("could not open %s\n", dest_path);
            close(fp);
            free(copybuf);
            return;
        }

        uint64_t copied_size = 0;
        while (copied_size < size) {
            memset(copybuf, 0, FSP_BUF_SZ_BYTES);
            if (copied_size + FSP_BUF_SZ_BYTES > size) {
                pread(fp, copybuf, size - copied_size, copied_size + offset);
                pwrite(fw, copybuf, size - copied_size, copied_size);
            } else {
                pread(fp, copybuf, FSP_BUF_SZ_BYTES, copied_size + offset);
                pwrite(fw, copybuf, FSP_BUF_SZ_BYTES, copied_size);
            }

            copied_size -= -FSP_BUF_SZ_BYTES;
        }

        close(fw);
    }
    
    free(copybuf);
    close(fp);
}

void mounter(bool do_mount, char* device, char* dest, char* partition, bool active) {
    mkdir(dest, 0777);
    
    uint8_t req_id = 0;
    if (partition)
        req_id = partition_name2id(partition);

    char mbr_raw[512];
    memset(mbr_raw, 0, 512);
    master_block_t* mbr = (master_block_t *)mbr_raw;
    int fp = open(device, O_RDONLY);
    if (fp < 0) {
        printf("could not open %s\n", device);
        return;
    }
    pread(fp, mbr_raw, BLOCK_SIZE, 0);
    close(fp);

    if (memcmp(mbr->magic, SCEMBR_MAGIC, sizeof(SCEMBR_MAGIC) - 1)) {
        printf("no SCE magic found\n");
        return;
    }

    char cmd[1024];
    char mount_path[448];

    for (int i = 0; i < ARRAYSIZE(mbr->partitions); ++i) {
        partition_t* p = &mbr->partitions[i];
        if (partition && (p->code != req_id || p->active != !(!active)))
            continue;

        if (p->code < SCEMBR_PART_KERNEL)
            continue;

        memset(mount_path, 0, 448);
        snprintf(mount_path, 448, "%s/%s-%d", dest, pcode_str[p->code], p->active);
        
        if (do_mount) {
            printf("Mounting 0x%08X to %s\n", p->off, mount_path);
            
            mkdir(mount_path, 0777);

            uint64_t offset = p->off;
            uint64_t size = p->sz;
            memset(cmd, 0, 1024);
            snprintf(cmd, 1024, "mount -o offset=0x%X,sizelimit=0x%X %s %s", offset, size, device, mount_path);
            system(cmd);
        } else {
            printf("uMounting 0x%08X from %s\n", p->off, mount_path);

            memset(cmd, 0, 1024);
            snprintf(cmd, 1024, "umount %s", mount_path);
            system(cmd);

            rmdir(mount_path);
        }
    }
}

void stripper(char* source, char *dest) {
    uint32_t magic = 0;
    int fp = open(source, O_RDONLY);
    if (fp < 0) {
        printf("could not open %s\n", source);
        return;
    }
    pread(fp, &magic, 4, 0);
    if (magic != RPOINT_MAGIC) {
        printf("type: unknown magic\n");
        close(fp);
        return;
    }

    int fw = open(dest, O_WRONLY | O_CREAT | O_TRUNC);
    if (fw < 0) {
        printf("could not open %s\n", dest);
        return;
    }

    uint64_t size = getSz(source);
    uint64_t processed_size = sizeof(emmcimg_super);
    void* copybuf = calloc(FSP_BUF_SZ_BLOCKS, BLOCK_SIZE);
    if (!copybuf) {
        printf("could not alloc\n");
        close(fp);
        close(fw);
        return;
    }
    
    while (processed_size < size) {
        memset(copybuf, 0, FSP_BUF_SZ_BYTES);
        if (processed_size + FSP_BUF_SZ_BYTES > size) {
            pread(fp, copybuf, size - processed_size, processed_size);
            pwrite(fw, copybuf, size - processed_size, processed_size - sizeof(emmcimg_super));
        } else {
            pread(fp, copybuf, FSP_BUF_SZ_BYTES, processed_size);
            pwrite(fw, copybuf, FSP_BUF_SZ_BYTES, processed_size - sizeof(emmcimg_super));
        }

        processed_size -= -FSP_BUF_SZ_BYTES;
    }

    free(copybuf);
    close(fw);
    close(fp);
}

void info(char* path) {
    void* master = calloc(1, BLOCK_SIZE);
    if (!master)
        return;
    
    int fp = open(path, O_RDONLY);
    if (fp < 0) {
        printf("could not open %s\n", path);
        return;
    }
    pread(fp, master, BLOCK_SIZE, 0);
    if (*(uint32_t*)master == RPOINT_MAGIC) {
        printf("type: FWTOOL restore point\n");
        emmcimg_super* rp_descr = master;
        printf(" - Size: 0x%X blocks\n", rp_descr->size);
        printf(" - ConsoleID: ");
        for (int i = 0; i < 16; i++) printf("%02X ", rp_descr->target[i]);
        printf("\n");
        pread(fp, master, BLOCK_SIZE, sizeof(emmcimg_super));
        printf("printing restore point contents:\n");
    }
    
    if (memcmp(master, SCEMBR_MAGIC, sizeof(SCEMBR_MAGIC) - 1) == 0) {
        printf("type: SCE PSP2 storage\n");
        parseMbrFull((master_block_t*)master, 69);
    } else printf("type: UNK\n");

    close(fp);
    free(master);
}

int main(int argc, char* argv[]) {

    if (argc < 2) {
        printf("\nusage: %s [mode] [dev] <...>\n\n", argv[0]);
        printf("modes:\n");
        printf(" 'mkmbr' : use the embedded mkmbr tool\n");
        printf(" 'info [dev]' : display info about the [dev] device/dump\n");
#ifndef WINDOWS
        printf(" 'mount [dev] [dest] <p> <a>' : mount [dev] partitions to the [dest] directory\n");
        printf(" 'umount [dev] [dest] <p> <a>' : umount [dev] partitions from the [dest] directory\n");
#endif
        printf(" 'unpack [dev] [dest]' : unpack [dev] partitions to the [dest] directory\n");
        printf(" 'pack [dev] [src]' : pack partitions from the [dest] directory to [dev]\n");
        printf(" 'extract [dev] [partition] [dest] <active>' : extract [partition] from [dev] to [dest]\n");
        printf(" 'replace [dev] [partition] [src] <active>' : replace [partition] in [dev] with [src]\n");
        printf(" 'strip [rpoint] [dev]' : convert fwtool restore point -> raw EMMC image\n");
        printf("supported devices/formats:\n");
        printf(" EMMC device or dump\n");
        printf(" GameCard device or dump\n");
        printf(" Sony MC device or dump\n");
        printf(" FWTOOL restore point (only info/unpack/extract/strip)\n");
        printf("supported partitions:\n");
        for (int i = 0; i < 16; i++) {
            if (i == 9) printf("\n");
            printf(" '%s',", pcode_str[i]);
        }
        printf(" 'mbr', 'rpoint_mbr'\n");
        return -1;
    }

    if (strcmp("mkmbr", argv[1]) == 0)
        mkmbr_main(--argc, ++argv);
    else if (strcmp("info", argv[1]) == 0)
        info(argv[2]);
    else if (strcmp("mount", argv[1]) == 0)
        mounter(true, argv[2], argv[3], (argc >= 5) ? argv[4] : NULL, (argc == 6) ? true : false);
    else if (strcmp("umount", argv[1]) == 0)
        mounter(false, argv[2], argv[3], (argc >= 5) ? argv[4] : NULL, (argc == 6) ? true : false);
    else if (strcmp("strip", argv[1]) == 0)
        stripper(argv[2], argv[3]);
    else if (strcmp("unpack", argv[1]) == 0)
        unpacker(argv[2], argv[3]);

    return 0;
}