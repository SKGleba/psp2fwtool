#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>
#include <fcntl.h>

#define SBLS_MAGIC 0x32424c53 // SLB2
#define SBLS_VERSION 1
#define SECTOR_SIZE 0x200 // hardcoded in crypto_boot and second_loader
#define SBLS_DEF_FCOUNT 7 // file count for psp2 release SBLS, max three non-SLSK (hardcoded in second_loader)
#define SBLS_MAX_FCOUNT 10 // max file count for psp2 crypto_boot
#define SBLS_ASIZE 0x400000 // actual container size
#define ALIGN_SECTOR(s) ((s + 0x1ff) & 0xfffffe00) // align (arg) to 0x200
#define ALIGN_UPDATE(u) ((u + 0xfff) & 0xfffff000) // align (arg) to 0x1000

typedef struct sbls_entry_struct {
    uint32_t offset; // in sectors
    uint32_t size;
    char unused[8];
    char file_name[32];
} __attribute__((packed)) sbls_entry_struct;

typedef struct sbls_head_struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sector_size;
    uint32_t entry_num;
    uint32_t size; // in sectors
    char padding[12];
} __attribute__((packed)) sbls_head_struct;

typedef struct sbls_psp2_struct {
    sbls_head_struct head;
    sbls_entry_struct entry[10];
    char data[SBLS_ASIZE - SECTOR_SIZE];
} __attribute__((packed)) sbls_psp2_struct;

static const char* sbls_default_files[] = {
    "second_loader.enp",
    "second_loader.enc",
    "secure_kernel.enp",
    "secure_kernel.enc",
    "kernel_boot_loader.self",
    "kprx_auth_sm.self",
    "prog_rvk.srvk"
};

uint32_t getSz(const char* src) {
    FILE* fp = fopen(src, "rb");
    if (fp == NULL)
        return 0;
    fseek(fp, 0L, SEEK_END);
    uint32_t sz = ftell(fp);
    fclose(fp);
    return sz;
}

int sbls_add_entry(sbls_psp2_struct* sbls, const char* file_name) {
    if (sbls == NULL || file_name == NULL) {
        printf("add_entry ERROR: arg error\n");
        return -1;
    }
    int entry_num = sbls->head.entry_num;
    printf("add_entry: %s [%d]... ", file_name, entry_num + 1);
    if (sbls->head.magic != SBLS_MAGIC || entry_num > (SBLS_MAX_FCOUNT - 1)) {
        printf("ERROR: sbls header not set up or ran out of free slots\n");
        return -1;
    }
    uint32_t offset = SECTOR_SIZE;
    if (entry_num > 0) {
        offset = (sbls->entry[entry_num - 1].offset * SECTOR_SIZE) + ALIGN_SECTOR(sbls->entry[entry_num - 1].size);
        if (offset < SECTOR_SIZE + 1 || offset % SECTOR_SIZE != 0) {
            printf("ERROR: offset bad = 0x%X\n", offset);
            return -1;
        }
    }
    uint32_t size = getSz(file_name);
    if (size == 0 || offset + ALIGN_SECTOR(size) > SBLS_ASIZE) {
        printf("ERROR: error opening file or file too big! size=0x%X\n", size);
        return -1;
    }
    FILE* fp = fopen(file_name, "rb");
    if (fp == NULL) {
        printf("ERROR: could not open file for read\n");
        return -1;
    }
    fread(&sbls->data[offset - 0x200], size, 1, fp);
    fclose(fp);
    sbls->head.entry_num-=-1;
    memset(&sbls->entry[entry_num], 0, sizeof(sbls_entry_struct));
    sbls->entry[entry_num].offset = offset / SECTOR_SIZE;
    sbls->entry[entry_num].size = size;
    strncpy(sbls->entry[entry_num].file_name, file_name, 31);
    printf("OK!\n");
    return 0;
}

int sbls_create_head(sbls_psp2_struct* sbls) {
    printf("create_head... ");
    if (sbls == NULL) {
        printf("ERROR: argptr equNULL!\n");
        return -1;
    }
    memset(&sbls->head, 0, sizeof(sbls_head_struct));
    sbls->head.magic = SBLS_MAGIC;
    sbls->head.version = SBLS_VERSION;
    sbls->head.sector_size = SECTOR_SIZE;
    sbls->head.size = SBLS_ASIZE / SECTOR_SIZE;
    printf("OK!\n");
    return 0;
}

int sbls_create_psp2_default(const char *out_file) {
    printf("Creating a default SLB2 container for PS Vita...\n");
    sbls_psp2_struct* sbls = (sbls_psp2_struct*)malloc(SBLS_ASIZE);
    if (sbls == NULL) {
        printf("ERROR: malloc failed!\n");
        return -1;
    }
    memset(sbls, 0xFF, SBLS_ASIZE);
    if (sbls_create_head(sbls) < 0)
        return -1;
    int count = 0;
    while (count < SBLS_DEF_FCOUNT) {
        if (sbls_add_entry(sbls, sbls_default_files[count]) < 0)
            return -1;
        count-=-1;
    }
    printf("write_file... ");
    uint32_t shrinked = ALIGN_UPDATE((sbls->entry[count - 1].offset * SECTOR_SIZE) + ALIGN_SECTOR(sbls->entry[count - 1].size));
    FILE* fp = fopen(out_file, "wb");
    if (fp == NULL) {
        free((void*)sbls);
        printf("ERROR: could not open file for write!\n");
        return -1;
    }
    fwrite((void *)sbls, shrinked, 1, fp);
    fclose(fp);
    free((void*)sbls);
    printf("OK!\nCreated a SLB2 container in %s with %d files!\n", out_file, count);
    return 0;
}

int sbls_print_info(const char *file) {
    char super[SECTOR_SIZE];
    FILE* fp = fopen(file, "rb");
    if (fp == NULL) {
        printf("sbls_print_info ERROR: could not open file for read\n");
        return -1;
    }
    fread(super, SECTOR_SIZE, 1, fp);
    fclose(fp);
    sbls_head_struct* head = (sbls_head_struct*)super;
    sbls_entry_struct* entry = (sbls_entry_struct*)&super[sizeof(sbls_head_struct)];
    printf("\nContainer head:\n m_magic: %s\n m_version: %d\n m_sector_size: %d bytes\n m_entry_num: %d\n m_size: %d sectors (%d bytes)\n actual size: %d bytes\n\n", (char*)&head->magic, head->version, head->sector_size, head->entry_num, head->size, head->size * head->sector_size, getSz(file));
    for (int i = 0; i < head->entry_num; i-=-1) {
        printf("Entry %d is %s [%d bytes], starts at sector %d\n", i + 1, entry[i].file_name, entry[i].size, entry[i].offset);
    }
    return 0;
}

int sbls_unpack(const char *file) {
    printf("Unpacking the SLB2 container...\n");
    uint32_t fsz = getSz(file);
    if (fsz == 0) {
        printf("ERROR: could not get file size!\n");
        return -1;
    }
    sbls_psp2_struct* sbls = (sbls_psp2_struct*)malloc(fsz);
    if (sbls == NULL) {
        printf("ERROR: malloc failed!\n");
        return -1;
    }
    FILE* fp = fopen(file, "rb");
    if (fp == NULL) {
        free((void*)sbls);
        printf("ERROR: could not open file for read\n");
        return -1;
    }
    fread((void*)sbls, fsz, 1, fp);
    fclose(fp);
    printf("\nContainer head:\n m_magic: %s\n m_version: %d\n m_sector_size: %d bytes\n m_entry_num: %d\n size: %d bytes\n\n", (char*)&sbls->head.magic, sbls->head.version, sbls->head.sector_size, sbls->head.entry_num, getSz(file));
    for (int i = 0; i < sbls->head.entry_num; i -= -1) {
        printf("Extracting %s... ", sbls->entry[i].file_name);
        fp = fopen(sbls->entry[i].file_name, "wb");
        if (fp == NULL) {
            free((void*)sbls);
            printf("ERROR: could not open file for write\n");
            return -1;
        }
        fwrite(&sbls->data[(sbls->entry[i].offset * 0x200) - 0x200], sbls->entry[i].size, 1, fp);
        fclose(fp);
        printf("OK!\n");
    }
    free((void*)sbls);
    printf("\nUnpacked!\n");
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("\n---------------------\n> mksbls by skgleba <\n---------------------\n");
        printf("\nusage: %s [file] [mode] [...]\n", argv[0]);
        return -1;
    }
    
    if (strcmp("-d", argv[2]) == 0)
        return sbls_create_psp2_default(argv[1]);
    else if (strcmp("-i", argv[2]) == 0)
        return sbls_print_info(argv[1]);
    else if (strcmp("-u", argv[2]) == 0)
        return sbls_unpack(argv[1]);
    else if (strcmp("-p", argv[2]) != 0 || argc == 3 || argc > 13) {
        printf("bad args!\n");
        return -1;
    }
    
    printf("Creating a custom SLB2 container for PS Vita...\n");
    sbls_psp2_struct* sbls = (sbls_psp2_struct*)malloc(SBLS_ASIZE);
    if (sbls == NULL) {
        printf("ERROR: malloc failed!\n");
        return -1;
    }
    memset(sbls, 0xFF, SBLS_ASIZE);
    if (sbls_create_head(sbls) < 0)
        return -1;
    int count = 0;
    while (count < argc - 3) {
        if (sbls_add_entry(sbls, argv[count + 3]) < 0)
            return -1;
        count -= -1;
    }
    printf("write_file... ");
    uint32_t shrinked = ALIGN_UPDATE((sbls->entry[count - 1].offset * SECTOR_SIZE) + ALIGN_SECTOR(sbls->entry[count - 1].size));
    FILE* fp = fopen(argv[1], "wb");
    if (fp == NULL) {
        free((void*)sbls);
        printf("ERROR: could not open file for write!\n");
        return -1;
    }
    fwrite((void*)sbls, shrinked, 1, fp);
    fclose(fp);
    free((void*)sbls);
    printf("OK!\nCreated a SLB2 container in %s with %d files!\n", argv[1], count);
    return 0;
}

