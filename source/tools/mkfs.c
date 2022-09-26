/* THIS FILE IS A PART OF PSP2FWTOOL
 *
 * Copyright (C) 2019-2022 skgleba
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdbool.h>

#include "../fwtool.h"

#define MKMBR_SLAVE
#include "mkmbr.c"

#ifdef WINDOWS
#include <windows.h>
#include <io.h>
int mkdir_proxy(char* path, uint32_t perms) {
    return mkdir(path);
}
ssize_t pread(int fd, void* buf, size_t count, uint64_t offset) {
    long unsigned int read_bytes = 0;

    OVERLAPPED overlapped;
    memset(&overlapped, 0, sizeof(OVERLAPPED));

    overlapped.OffsetHigh = (uint32_t)((offset & 0xFFFFFFFF00000000LL) >> 32);
    overlapped.Offset = (uint32_t)(offset & 0xFFFFFFFFLL);

    HANDLE file = (HANDLE)_get_osfhandle(fd);
    SetLastError(0);
    bool RF = ReadFile(file, buf, count, &read_bytes, &overlapped);

    // For some reason it errors when it hits end of file so we don't want to check that
    if ((RF == 0) && GetLastError() != ERROR_HANDLE_EOF) {
        errno = GetLastError();
        // printf ("Error reading file : %d\n", GetLastError());
        return -1;
    }

    return read_bytes;
}
ssize_t pwrite(int fd, void* buf, size_t count, uint64_t offset) {
    long unsigned int write_bytes = 0;

    OVERLAPPED overlapped;
    memset(&overlapped, 0, sizeof(OVERLAPPED));

    overlapped.OffsetHigh = (uint32_t)((offset & 0xFFFFFFFF00000000LL) >> 32);
    overlapped.Offset = (uint32_t)(offset & 0xFFFFFFFFLL);

    HANDLE file = (HANDLE)_get_osfhandle(fd);
    SetLastError(0);
    bool RF = WriteFile(file, buf, count, &write_bytes, &overlapped);

    // For some reason it errors when it hits end of file so we don't want to check that
    if ((RF == 0) && GetLastError() != ERROR_HANDLE_EOF) {
        errno = GetLastError();
        // printf ("Error reading file : %d\n", GetLastError());
        return -1;
    }

    return write_bytes;
}
#else
#define O_BINARY 0
int mkdir_proxy(char* path, uint32_t perms) {
    return mkdir(path, perms);
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

bool file_exists(char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    close(fd);
    return 1;
}

int x_tractor(char* device, char *partition, char* dest, int active, int replace) {

    uint8_t req_id = (partition) ? partition_name2id(partition) : 0;

    if (!req_id && partition) {
        if (!strcmp("mbr", partition))
            req_id = 0x20;
        else if (!strcmp("rpoint_mbr", partition))
            req_id = 0x21;
        else if (!strcmp("enso", partition))
            req_id = 0x22;
        else if (!strcmp("emumbr", partition))
            req_id = 0x23;
    }

    mkdir_proxy(dest, 0777);

    char mbr_raw[512];
    memset(mbr_raw, 0, 512);
    master_block_t* mbr = (master_block_t*)mbr_raw;
    int fp = open(device, O_RDWR | O_BINARY);
    if (fp < 0) {
        printf("could not open %s\n", device);
        return -1;
    }
    pread(fp, mbr_raw, BLOCK_SIZE, 0);

    uint64_t max_size = 0, rpoint_mv = 0;
    if (*(uint32_t*)mbr->magic == RPOINT_MAGIC) {
        if (replace) {
            printf("operation not supported on rpoint!\n");
            close(fp);
            return -2;
        }
        max_size = *(uint32_t*)(mbr->magic + 4);
        max_size *= BLOCK_SIZE;
        max_size += sizeof(emmcimg_super);
        rpoint_mv = sizeof(emmcimg_super);
        pread(fp, mbr_raw, BLOCK_SIZE, sizeof(emmcimg_super));
    }

    if (memcmp(mbr->magic, SCEMBR_MAGIC, sizeof(SCEMBR_MAGIC) - 1) != 0) {
        printf("no SCE magic found\n");
        close(fp);
        return -3;
    }

    if (!max_size) {
        max_size = mbr->device_size;
        max_size *= BLOCK_SIZE;
    }

    void* copybuf = calloc(FSP_BUF_SZ_BLOCKS, BLOCK_SIZE);
    if (!copybuf) {
        printf("could not alloc\n");
        close(fp);
        return -4;
    }

    char dest_path[512];

    if (req_id > 0x10) {
        if (req_id == 0x21) {
            if (rpoint_mv) {
                if (replace) {
                    snprintf(dest_path, 512, "%s/%s", dest, "rpoint_mbr");
                    printf("Replacing 0x%08X from %s\n", 0, dest_path);
                    int fr = open(dest_path, O_RDONLY);
                    if (fr < 0) {
                        printf("could not open %s\n", dest_path);
                        close(fp);
                        free(copybuf);
                        return -10;
                    }
                    pread(fr, copybuf, rpoint_mv, 0);
                    pwrite(fp, copybuf, rpoint_mv, 0);
                    close(fr);
                } else {
                    pread(fp, copybuf, rpoint_mv, 0);
                    snprintf(dest_path, 512, "%s/%s", dest, "rpoint_mbr");
                    printf("Extracting 0x%08X to %s\n", 0, dest_path);
                    int fw = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC);
                    if (fw < 0) {
                        printf("could not open %s\n", dest_path);
                        close(fp);
                        free(copybuf);
                        return -11;
                    }
                    pwrite(fw, copybuf, rpoint_mv, 0);
                    close(fw);
                }
            } else
                printf("not rpoint!\n");
        } else if (req_id == 0x22) {
            if (replace) {
                snprintf(dest_path, 512, "%s/%s", dest, "enso");
                printf("Replacing 0x%08X from %s\n", (rpoint_mv + 0x400) / BLOCK_SIZE, dest_path);
                int fr = open(dest_path, O_RDONLY);
                if (fr < 0) {
                    printf("could not open %s\n", dest_path);
                    close(fp);
                    free(copybuf);
                    return -10;
                }
                pread(fr, copybuf, E2X_SIZE_BLOCKS * BLOCK_SIZE, 0);
                pwrite(fp, copybuf, E2X_SIZE_BLOCKS * BLOCK_SIZE, rpoint_mv + 0x400);
                close(fr);
            } else {
                pread(fp, copybuf, E2X_SIZE_BLOCKS* BLOCK_SIZE, rpoint_mv + 0x400);
                snprintf(dest_path, 512, "%s/%s", dest, "enso");
                printf("Extracting 0x%08X to %s\n", (rpoint_mv + 0x400) / BLOCK_SIZE, dest_path);
                int fw = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC);
                if (fw < 0) {
                    printf("could not open %s\n", dest_path);
                    close(fp);
                    free(copybuf);
                    return -11;
                }
                pwrite(fw, copybuf, E2X_SIZE_BLOCKS * BLOCK_SIZE, 0);
                close(fw);
            }
        } else if (req_id == 0x23) {
            if (replace) {
                snprintf(dest_path, 512, "%s/%s", dest, "emumbr");
                printf("Replacing 0x%08X from %s\n", (rpoint_mv + BLOCK_SIZE) / BLOCK_SIZE, dest_path);
                int fr = open(dest_path, O_RDONLY);
                if (fr < 0) {
                    printf("could not open %s\n", dest_path);
                    close(fp);
                    free(copybuf);
                    return -10;
                }
                pread(fr, copybuf, E2X_SIZE_BLOCKS * BLOCK_SIZE, 0);
                pwrite(fp, copybuf, E2X_SIZE_BLOCKS * BLOCK_SIZE, rpoint_mv + BLOCK_SIZE);
                close(fr);
            } else {
                pread(fp, copybuf, E2X_SIZE_BLOCKS* BLOCK_SIZE, rpoint_mv + BLOCK_SIZE);
                snprintf(dest_path, 512, "%s/%s", dest, "emumbr");
                printf("Extracting 0x%08X to %s\n", (rpoint_mv + BLOCK_SIZE) / BLOCK_SIZE, dest_path);
                int fw = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC);
                if (fw < 0) {
                    printf("could not open %s\n", dest_path);
                    close(fp);
                    free(copybuf);
                    return -11;
                }
                pwrite(fw, copybuf, E2X_SIZE_BLOCKS * BLOCK_SIZE, 0);
                close(fw);
            }
        } else if (req_id == 0x20) {
            if (replace) {
                snprintf(dest_path, 512, "%s/%s", dest, "mbr");
                printf("Replacing 0x%08X from %s\n", rpoint_mv / BLOCK_SIZE, dest_path);
                int fr = open(dest_path, O_RDONLY);
                if (fr < 0) {
                    printf("could not open %s\n", dest_path);
                    close(fp);
                    free(copybuf);
                    return -10;
                }
                pread(fr, copybuf, BLOCK_SIZE, 0);
                pwrite(fp, copybuf, BLOCK_SIZE, rpoint_mv);
                close(fr);
            } else {
                snprintf(dest_path, 512, "%s/%s", dest, "mbr");
                printf("Extracting 0x%08X to %s\n", rpoint_mv / BLOCK_SIZE, dest_path);
                int fw = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC);
                if (fw < 0) {
                    printf("could not open %s\n", dest_path);
                    close(fp);
                    free(copybuf);
                    return -11;
                }
                pwrite(fw, mbr_raw, BLOCK_SIZE, 0);
                close(fw);
            }
        }

        close(fp);
        free(copybuf);

        return 0;
    }
    
    for (size_t i = 0; i < ARRAYSIZE(mbr->partitions); ++i) {
        partition_t* p = &mbr->partitions[i];

        if (!p->code)
            continue;

        if (req_id) {
            if (p->code != req_id)
                continue;
            if (p->active != active)
                continue;
        }

        uint64_t offset = p->off;
        uint64_t size = p->sz;
        offset *= BLOCK_SIZE;
        size *= BLOCK_SIZE;
        
        offset += rpoint_mv;

        if (offset + size > max_size)
            continue;

        memset(dest_path, 0, 512);
        snprintf(dest_path, 512, "%s/%s-%d", dest, pcode_str[p->code], p->active);

        if (replace) {
            printf("Replacing 0x%08X from %s\n", p->off, dest_path);
            
            int fr = open(dest_path, O_RDONLY);
            if (fr < 0) {
                printf("could not open %s\n", dest_path);
                close(fp);
                free(copybuf);
                return -5;
            }

            uint64_t copied_size = 0;
            while (copied_size < size) {
                memset(copybuf, 0, FSP_BUF_SZ_BYTES);
                if (copied_size + FSP_BUF_SZ_BYTES > size) {
                    pread(fr, copybuf, size - copied_size, copied_size);
                    pwrite(fp, copybuf, size - copied_size, copied_size + offset);
                } else {
                    pread(fr, copybuf, FSP_BUF_SZ_BYTES, copied_size);
                    pwrite(fp, copybuf, FSP_BUF_SZ_BYTES, copied_size + offset);
                }

                copied_size -= -FSP_BUF_SZ_BYTES;
            }

            close(fr);
        } else {
            printf("Extracting 0x%08X to %s\n", p->off, dest_path);

            int fw = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC);
            if (fw < 0) {
                printf("could not open %s\n", dest_path);
                close(fp);
                free(copybuf);
                return -6;
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
    }
    
    free(copybuf);
    close(fp);

    return 0;
}

void y_tractor(char* source_dir, char* dest) {
    printf("checking the mbr\n");
    
    char path_concat[0x200];
    memset(path_concat, 0, 0x200);
    snprintf(path_concat, 0x200, "%s/%s", source_dir, "mbr");

    uint8_t mbr_raw[BLOCK_SIZE];
    memset(mbr_raw, 0, BLOCK_SIZE);
    int fr = open(path_concat, O_RDONLY);
    if (fr < 0) {
        printf("MBR (%s) open failed\n", fr);
        return;
    }
    pread(fr, mbr_raw, BLOCK_SIZE, 0);
    close(fr);

    master_block_t* mbr = (master_block_t*)mbr_raw;
    if (memcmp(mbr->magic, SCEMBR_MAGIC, sizeof(SCEMBR_MAGIC) - 1) != 0) {
        printf("no SCE magic found\n");
        return;
    }

    printf("making sure all partition images are there\n");
    for (size_t i = 0; i < ARRAYSIZE(mbr->partitions); ++i) {
        partition_t* p = &mbr->partitions[i];

        if (!p->code)
            continue;
        
        memset(path_concat, 0, 0x200);
        snprintf(path_concat, 0x200, "%s/%s-%d", source_dir, pcode_str[p->code], p->active);
        
        if (!file_exists(path_concat)) {
            printf("source (%s) does not exist\n", path_concat);
            return;
        }
    }

    uint64_t device_size = 0;
    device_size = mbr->device_size;
    device_size *= BLOCK_SIZE;

    printf("creating a blank image template of size %llX, please wait...\n", device_size);
    
    void* copybuf = calloc(FSP_BUF_SZ_BLOCKS, BLOCK_SIZE);
    if (!copybuf) {
        printf("could not alloc\n");
        return;
    }

    memcpy(copybuf, mbr_raw, BLOCK_SIZE);

    int fw = open(dest, O_WRONLY | O_CREAT | O_TRUNC);
    if (fw < 0) {
        printf("target (%s) open failed\n", fw);
        free(copybuf);
        return;
    }

    uint64_t blanked_size = 0;
    while (blanked_size < device_size) {
        pwrite(fw, copybuf, FSP_BUF_SZ_BYTES, blanked_size);
        blanked_size -= -FSP_BUF_SZ_BYTES;
    }

    close(fw);
    free(copybuf);

    printf("template created, adding partitions\n");
    for (size_t i = 0; i < ARRAYSIZE(mbr->partitions); ++i) {
        partition_t* p = &mbr->partitions[i];

        if (!p->code)
            continue;

        if (x_tractor(dest, pcode_str[p->code], source_dir, p->active, 1))
            printf("failed to add %s-%d(%d)\n", pcode_str[p->code], p->active, p->code);
    }
}

void mounter(bool do_mount, char* device, char* dest, char* partition, bool active) {
    mkdir_proxy(dest, 0777);
    
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
            
            mkdir_proxy(mount_path, 0777);

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
        printf(" 'pack [src] [dev]' : pack partitions from the [src] directory to [dev]\n");
        printf(" 'extract [dev] [dest] [partition] <active>' : extract [partition] from [dev] to the [dest] directory\n");
        printf(" 'replace [dev] [src] [partition] <active>' : replace [partition] in [dev] with one from [src] directory\n");
        printf(" 'strip [rpoint] [dev]' : convert fwtool restore point -> raw EMMC image\n");
        printf("supported devices/formats:\n");
        printf(" EMMC device or dump\n");
        printf(" GameCard device or dump\n");
        printf(" Sony MC device or dump\n");
        printf(" FWTOOL restore point (only info/unpack/extract/strip)\n");
        printf("supported partitions:\n");
        for (int i = 1; i < 16; i++) {
            if (i == 11) printf("\n");
            printf(" '%s',", pcode_str[i]);
        }
        printf(" 'mbr', 'rpoint_mbr', 'enso', 'emumbr'\n");
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
    else if (strcmp("pack", argv[1]) == 0)
        y_tractor(argv[2], argv[3]);
    else if (strcmp("unpack", argv[1]) == 0)
        x_tractor(argv[2], NULL, argv[3], 0, 0);
    else if (strcmp("extract", argv[1]) == 0)
        x_tractor(argv[2], argv[4], argv[3], argc > 5, 0);
    else if (strcmp("replace", argv[1]) == 0)
        x_tractor(argv[2], argv[4], argv[3], argc > 5, 1);

    return 0;
}