/* THIS FILE IS A PART OF PSP2FWTOOL
 *
 * Copyright (C) 2019-2021 skgleba
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

#define AES_BLOCKLEN 16
#define AES_KEYLEN AES_BLOCKLEN // 128bit
#define AES_keyExpSize 176

struct AES_ctx {
    uint8_t RoundKey[AES_keyExpSize];
    uint8_t Iv[AES_BLOCKLEN];
};

extern int sha1digest(uint8_t* digest, char* hexdigest, const uint8_t* data, size_t databytes);
extern void AES_CBC_decrypt_buffer(struct AES_ctx* ctx, uint8_t* buf, size_t length);
extern void AES_CBC_encrypt_buffer(struct AES_ctx* ctx, uint8_t* buf, size_t length);
extern void AES_init_ctx_iv(struct AES_ctx* ctx, const uint8_t* key, const uint8_t* iv);

typedef struct {
    uint8_t content;
    uint8_t hdr_size;
    uint16_t size_be;
} __attribute__((packed)) block_hdr_hdr;

typedef struct {
    block_hdr_hdr hdr;
    uint32_t fw_ver;
    uint32_t hw_info;
    uint32_t unused;
} __attribute__((packed)) block_x1;

typedef struct {
    block_hdr_hdr hdr;
    uint32_t img_size;
    uint32_t ernie_type;
    uint32_t unused;
} __attribute__((packed)) block_x2;

typedef struct {
    block_hdr_hdr hdr;
    unsigned char data[0x18];
    uint32_t unused;
} __attribute__((packed)) block_x3;

typedef struct {
    block_hdr_hdr hdr;
    uint32_t segment;
    uint32_t size;
    uint32_t unused;
    unsigned char data[];
} __attribute__((packed)) block_x10;

typedef struct {
    block_hdr_hdr hdr;
    unsigned char img_hash[0x14];
} __attribute__((packed)) block_x20;

typedef struct {
    unsigned char dec[0x100000];
    unsigned char enc[0x100000];
    unsigned char ref[0x100000];
    unsigned char work[0x200000];
} mem_buffers;

const unsigned char key_ids[6][2] = {
    {0x10, 0}, {0x30, 0}, {0x40, 0},
    {0x60, 1}, {0x70, 2}, {0x80, 3}
};

unsigned char key_pairs[4][2][16] = {
    {{0x12, 0xB5, 0x40, 0x8F, 0xD1, 0x89, 0xE2, 0x23, 0xB6, 0x18, 0x90, 0xF4, 0x88, 0x53, 0x60, 0x08}, {0x82, 0xD6, 0x52, 0x8A, 0x87, 0xBC, 0x55, 0xB3, 0x8E, 0xF2, 0x9A, 0x45, 0x73, 0x0E, 0xF1, 0x30}},
    {{0x8C, 0x9E, 0xD3, 0x90, 0x8C, 0x41, 0x43, 0xAE, 0x02, 0x85, 0x57, 0x94, 0xC0, 0x25, 0xBE, 0x1A}, {0xC8, 0x5A, 0xE1, 0x57, 0x6D, 0x5E, 0x20, 0x5F, 0xE8, 0x04, 0x35, 0x73, 0xF5, 0x5F, 0x4E, 0x11}},
    {{0x67, 0xC3, 0x42, 0x53, 0xA7, 0xDE, 0x13, 0x51, 0x7E, 0xC9, 0x03, 0xFE, 0x11, 0x19, 0xC0, 0x4C}, {0xDB, 0x30, 0x26, 0x73, 0xD6, 0x9F, 0x0D, 0x51, 0x3A, 0x63, 0x5E, 0x68, 0xA4, 0x70, 0xF9, 0xC1}},
    {{0x52, 0x3B, 0xEB, 0x53, 0xFC, 0xB9, 0x5D, 0xC7, 0x72, 0xAA, 0x1B, 0xFB, 0x0A, 0x96, 0xCD, 0x10}, {0x38, 0x5D, 0x67, 0xE5, 0x0C, 0xE7, 0x66, 0x9E, 0xCD, 0x17, 0x1F, 0xE5, 0x76, 0x81, 0x43, 0x43}}
};

/*
unsigned char key_pairs[4][2][16] = {
    {{ KEY MD5 : 88ecb256d1b1b968962342e267f92efd }, { IV MD5 : 27cca90e5ca7c2629341499709658232 }},
    {{ KEY MD5 : 7ac8ce1893030510919e3bc79dcbc32e }, { IV MD5 : 81b77af9c217cd00a9bf9a968a05ac18 }},
    {{ KEY MD5 : 01bfb7d51b1e1880694a97290e08bb27 }, { IV MD5 : 7905c429ac8eb7ce0b3a9b3b4e4fb242 }},
    {{ KEY MD5 : f8d1ed04eff433910fe5be1468c3e5ec }, { IV MD5 : 09b86d5f940a48f140dd05c3871c420c }}
};
*/

const char selftest_hashes[4][0x29] = { "5292fee53248169d714ce7b96959095467ee0b8a", "a58b94021b4c9eabfa5bea8f2a510445f9350221", "66676269e46f371c12a280ae831cf79116511c63", "795d2a7a032a31ccb9650f581028fbdadad35972" };

void* getKey(int iv, int id) {
    for (int i = 0; i < 6; i -= -1) {
        if (key_ids[i][0] == id)
            return key_pairs[key_ids[i][1]][iv];
    }
    return NULL;
}

uint32_t getSz(const char* src) {
    FILE* fp = fopen(src, "rb");
    if (!fp)
        return 0;
    fseek(fp, 0L, SEEK_END);
    uint32_t sz = ftell(fp);
    fclose(fp);
    return sz;
}

int aes_cbc(uint8_t* key, uint8_t* iv, uint8_t* data, uint32_t len, int encrypt) {
    struct AES_ctx aesctx;
    memset(&aesctx, 0, sizeof(aesctx));
    AES_init_ctx_iv(&aesctx, key, iv);
    if (encrypt)
        AES_CBC_encrypt_buffer(&aesctx, data, len);
    else
        AES_CBC_decrypt_buffer(&aesctx, data, len);
}

int encrypt_patch(void* work_buf, void* decrypted, void* output, uint16_t* list, int list_count, uint32_t fw_ver, uint32_t hw_info, int ernie_type) {
    uint16_t segment_size = ernie_type ? 0x400 : 0x800;
    
    char shastr[41];
    memset(shastr, 0, 41);

    printf("get id for %X\n", hw_info);
    int id = ((hw_info & 0x00F00000) >> 0x10);
    if (!getKey(0, id))
        return -1;
    printf("id 0x%X\n", id);

    printf("verifying segment list\n");
    if (!list_count || !list)
        return -1;
    if (id == 0x80) {
        for (int i = 0; i < list_count; i -= -1) {
            if (list[i] >= 0xD5 && list[i] < (0xD5 + 0x80)) {
                printf("this id cannot write to segment %X!\n", list[i]);
                return -1;
            }
        }
    }

    block_x1* hdrblock = output;
    block_x2* typeblock = (output + sizeof(block_x1));
    block_x10* datablock = NULL;
    block_x20* hashblock = (output + sizeof(block_x1) + sizeof(block_x2) + (list_count * sizeof(block_x10)) + (list_count * segment_size));

    printf("writing block 0x1\n");
    hdrblock->hdr.content = 1;
    hdrblock->hdr.hdr_size = sizeof(block_x1);
    hdrblock->fw_ver = fw_ver;
    hdrblock->hw_info = hw_info;

    printf("writing block 0x2\n");
    typeblock->hdr.content = 2;
    typeblock->hdr.hdr_size = sizeof(block_x2);
    typeblock->ernie_type = ernie_type;

    printf("concating segments\n");
    uint16_t segment = 0;
    for (int i = 0; i < list_count; i -= -1) {
        segment = list[i];
        memcpy(work_buf + (i * segment_size), decrypted + (segment * segment_size), segment_size);
    }

    printf("witing blocks 0x20\n");
    hashblock->hdr.content = 0x20;
    hashblock->hdr.hdr_size = sizeof(block_x20);
    sha1digest(hashblock->img_hash, shastr, work_buf, list_count * segment_size);
    printf("hash %s\n", shastr);

    printf("encrypting segments\n");
    aes_cbc(getKey(0, id), getKey(1, id), work_buf, list_count * segment_size, 1);

    printf("witing blocks 0x10\n");
    segment = 0;
    for (int i = 0; i < list_count; i -= -1) {
        segment = list[i];
        if (id == 0x80 && segment > (0xD4 + 0x80))
            segment -= 0x80;
        datablock = (output + sizeof(block_x1) + sizeof(block_x2) + (i * sizeof(block_x10)) + (i * segment_size));
        datablock->hdr.content = 0x10;
        datablock->hdr.hdr_size = sizeof(block_x10);
        datablock->hdr.size_be = segment_size / 0x100;
        datablock->segment = segment;
        datablock->size = segment_size;
        memcpy(datablock->data, work_buf + (i * segment_size), segment_size);
        printf("added segment 0x%X\n", segment);
    }

    printf("patch built!\n");
    return sizeof(block_x1) + sizeof(block_x2) + (list_count * sizeof(block_x10)) + (list_count * segment_size) + sizeof(block_x20);
}

int decrypt_patch(void* work_buf, void* encrypted, void* output, uint32_t size) {
    int id = 0;
    uint32_t off = 0, work_off = 0;
    char* sha = NULL;
    block_x1* hdrblock = NULL;
    block_x10* datablock = NULL;
    block_x20* hashblock = NULL;
    printf("concating...\n", size);
    while (off < size) {
        switch (*(uint8_t*)(encrypted + off)) {
        case 1:
            hdrblock = encrypted + off;
            id = ((hdrblock->hw_info & 0x00F00000) >> 0x10);
            if (!getKey(0, id))
                return -1;
            printf("id 0x%X\n", id);
        case 2:
            off -= -0x10;
            break;
        case 3:
            off -= -0x20;
            break;
        case 0x10:
            if (!id)
                return -1;
            datablock = encrypted + off;
            memcpy(work_buf + work_off, datablock->data, datablock->size);
            if (id == 0x80 && datablock->segment > 0xD4)
                datablock->segment -= -0x80;
            off -= -(datablock->size + 0x10);
            work_off -= -datablock->size;
            break;
        case 0x20:
            hashblock = encrypted + off;
            off -= -0x18;
            break;
        default:
            printf("unk hdr 0x%X\n", *(uint8_t*)(encrypted + off));
            break;
        }
    }

    printf("decrypting...\n");
    aes_cbc(getKey(0, id), getKey(1, id), work_buf, work_off, 0);

    if (hashblock) {
        char sha[0x14], shastr[41];
        memset(sha, 0, 0x14);
        memset(shastr, 0, 41);
        sha1digest(sha, shastr, work_buf, work_off);
        printf("hash %s\n", shastr);
        if (memcmp(hashblock->img_hash, sha, 0x14))
            return -1;
        printf("hash ok");
    }

    printf("building...\n");
    off = 0;
    work_off = 0;
    while (off < size) {
        switch (*(uint8_t*)(encrypted + off)) {
        case 1:
        case 2:
            off -= -0x10;
            break;
        case 3:
            off -= -0x20;
            break;
        case 0x10:
            datablock = encrypted + off;
            memcpy(output + (datablock->segment * datablock->size), work_buf + (work_off * datablock->size), datablock->size);
            off -= -(datablock->size + 0x10);
            work_off -= -1;
            break;
        case 0x20:
            off -= -0x18;
            break;
        default:
            printf("unk hdr 0x%X\n", *(uint8_t*)(encrypted + off));
            break;
        }
    }

    printf("built %d blocks\n", work_off);

    return 0;
}

int diff(char* a, char* b, int type, char* output) {
    uint16_t segment_size = type ? 0x400 : 0x800;
    printf("comparing %s and %s with segment size = 0x%X\n", a, b, segment_size);
    uint32_t size = getSz(a);
    uint32_t bsize = getSz(b);
    if (size != bsize) {
        printf("not same size!\n");
        return -1;
    }
    
    void* cmpbuf = calloc(2, size);
    if (!cmpbuf)
        return -1;
    
    FILE* cp = fopen(a, "rb");
    if (!cp) {
        printf("error opening %s for read\n", a);
        return -1;
    }
    fread(cmpbuf, size, 1, cp);
    fclose(cp);
    
    cp = fopen(b, "rb");
    if (!cp) {
        printf("error opening %s for read\n", b);
        return -1;
    }
    fread(cmpbuf + size, size, 1, cp);
    fclose(cp);
    
    cp = fopen(output, "wb");
    if (!cp) {
        printf("error opening list for write\n");
        return -1;
    }
    for (uint16_t segment = 0; segment < (size / segment_size); segment++) {
        if (type == 2 && segment == 0xD5)
            segment -= -0x80;
        if (memcmp(cmpbuf + segment * segment_size, cmpbuf + size + segment * segment_size, segment_size)) {
            printf("diff 0x%X [0x%X - 0x%X]\n", segment, segment * segment_size, segment * segment_size + (segment_size - 1));
            fwrite(&segment, 2, 1, cp);
        }
    }
    fclose(cp);

    free(cmpbuf);
    
    printf("diff finished [%s]\n", output);
    return 0;
}

int selftest(void);
int patch_seggs(void);

int main(int argc, char* argv[]) {

    if (argc == 2) { // ugly special test commands, remove on release
        if (!strcmp("selftest", argv[1]))
            return selftest();
        else if (!strcmp("patch_seggs", argv[1]))
            return patch_seggs();
    }

    if (argc < 4) {
        printf("\n----------------------\n> mkernie by skgleba <\n----------------------\n");
        printf("\nusage: %s [decrypt | encrypt | list | diff] [args]\n", argv[0]);
        printf("\n");
        printf(" decrypt [INPUT_FILE] [OUTPUT_FILE] <TEMPLATE_FILE>\n");
        printf("  ? decrypt an ernie update\n");
        printf("  | INPUT_FILE => encrypted syscon update file\n");
        printf("  | OUTPUT_FILE => decrypted syscon update output file\n");
        printf("  | TEMPLATE_FILE => optional template for syscon flash, copied to output\n");
        printf("  $ %s decrypt syscon_fw-00.bin syscon_fw_dec.bin\n", argv[0]);
        printf("    => decrypt syscon_fw-00.bin to syscon_fw_dec.bin\n");
        printf("\n");
        printf(" encrypt [INPUT_FILE] [OUTPUT_FILE] [LIST_FILE] [HEADER PARAMS]\n");
        printf("  ? create and encrypt an ernie update file\n");
        printf("  | INPUT_FILE => syscon flash-like blob input file\n");
        printf("  | OUTPUT_FILE => encrypted syscon update output file\n");
        printf("  | LIST_FILE => list of INPUT_FILE segments to add to the syscon update file\n");
        printf("  | HEADER PARAMS => one of:\n");
        printf("    | => [SC_TYPE] [HW_INFOu32] [SC_FW_VERu32]\n");
        printf("      | SC_TYPE => syscon type, one of 78k0r-L, 78k0r or rl78\n");
        printf("      | HW_INFOu32 => target hardware info, eg 0x00416000\n");
        printf("      | SC_FW_VERu32 => update syscon version, eg 0x0100060D\n");
        printf("    | => [HEADER_FILE]\n");
        printf("      | HEADER_FILE => encrypted syscon update, will use its params\n");
        printf("  $ %s encrypt syscon.fw sc_patch.bin list.bin 78k0r 0x00406000 0x0100060D\n", argv[0]);
        printf("    => extract segments listed in list.bin from syscon.fw and generate patch 'sc_patch.bin' using the set params\n");
        printf("\n");
        printf(" list [LIST_FILE] {SEGMENTS TO ADD}\n");
        printf("  ? create a list of segments for the encrypt command\n");
        printf("  | LIST_FILE => output list of segments to add to the syscon update file\n");
        printf("  $ %s list list.bin 0x0 0x3 0xB8\n", argv[0]);
        printf("    => create a list of segments to use for the encryptor [0x0, 0x3, 0xB8]\n");
        printf("\n");
        printf(" diff [LIST_FILE] [FILE_A] [FILE_B] [SC_TYPE]\n");
        printf("  ? create a list of segments for the encrypt command (diff based)\n");
        printf("  | LIST_FILE => output list of segments to add to the syscon update file\n");
        printf("  | FILE_A => syscon flash-like blob file 1\n");
        printf("  | FILE_B => syscon flash-like blob file 2\n");
        printf("  | SC_TYPE => syscon type, one of 78k0r-L, 78k0r or rl78\n");
        printf("  $ %s diff list.bin sc.bin sc_patched.bin rl78\n", argv[0]);
        printf("    => create a list of rl78 segments to use for the encryptor by comparing sc.bin and sc_patched.bin\n");
        printf("\n");
        return -1;
    }

    if (!strcmp("list", argv[1])) {
        FILE* fd = fopen(argv[2], "wb");
        if (!fd) {
            printf("error opening list for write\n");
            return -1;
        }
        uint16_t segment = 0;
        for (int i = 3; i < argc; i++) {
            segment = (uint16_t)strtoul((argv[i] + 2), NULL, 16);
            fwrite(&segment, 2, 1, fd);
            printf("added segment 0x%X\n", segment);
        }
        fclose(fd);
        return 0;
    } else if (!strcmp("diff", argv[1])) {
        int type;
        if (!strcmp("78k0r-L", argv[5]))
            type = 0;
        else if (!strcmp("78k0r", argv[5]))
            type = 1;
        else if (!strcmp("rl78", argv[5]))
            type = 2;
        else {
            printf("bad type!\n");
            return -1;
        }
        if (diff(argv[3], argv[4], type, argv[2]) < 0) {
            printf("diff error occured\n");
            return -1;
        }
        return 0;
    }

    int encrypt = 0;
    uint32_t fwv = 0, hwv = 0, type = 0;
    char* enc = NULL, * dec = NULL, *ref = NULL;

    if (!strcmp("encrypt", argv[1])) {
        if (argc != 6 && argc != 8) {
            printf("encrypt parse_args error: argc\n");
            return -1;
        }
        dec = argv[2];
        enc = argv[3];
        ref = argv[4];
        if (argc == 6) {
            char header[0x30];
            FILE* hp = fopen(argv[5], "rb");
            if (!hp) {
                printf("encrypt parse_args error: could not open header\n");
                return -1;
            }
            fread(header, 0x30, 1, hp);
            fclose(hp);
            fwv = *(uint32_t*)(header + 4);
            hwv = *(uint32_t*)(header + 8);
            type = *(uint32_t*)(header + 0x18);
        } else {
            if (!strcmp("78k0r-L", argv[5]))
                type = 0;
            else if (!strcmp("78k0r", argv[5]))
                type = 1;
            else if (!strcmp("rl78", argv[5]))
                type = 2;
            else {
                printf("encrypt parse_args error: bad type\n");
                return 0;
            }
            hwv = (uint32_t)strtoul((argv[6] + 2), NULL, 16);
            fwv = (uint32_t)strtoul((argv[7] + 2), NULL, 16);
        }
        encrypt = 1;
    } else {
        if (argc != 4 && argc != 5) {
            printf("decrypt parse_args error: argc\n");
            return -1;
        }
        enc = argv[2];
        dec = argv[3];
        if (argc == 5)
            ref = argv[4];
    }

    if (!enc || !dec) {
        printf("please set both input and output!\n");
        return -1;
    }

    if (encrypt && !ref) {
        printf("please set the seggs list!\n");
        return -1;
    }

    mem_buffers* buf = calloc(5, 0x100000);

    if (!buf) {
        printf("calloc error!\n");
        return -1;
    }

    FILE* fp = NULL;
    void* output_buf = NULL;
    char* output_file = NULL;
    int ret = 0, output_size = 0;

    if (encrypt) {
        fp = fopen(ref, "rb");
        if (!fp) {
            printf("encrypt prep_mem error: could not open list\n");
            return -1;
        }
        fread(buf->ref, getSz(ref), 1, fp);
        fclose(fp);
        fp = fopen(dec, "rb");
        if (!fp) {
            printf("encrypt prep_mem error: could not open source\n");
            return -1;
        }
        fread(buf->dec, 0x100000, 1, fp);
        fclose(fp);
        ret = encrypt_patch(buf->work, buf->dec, buf->enc, (uint16_t*)buf->ref, getSz(ref) / 2, fwv, hwv, type);
        output_buf = buf->enc;
        output_size = ret;
        output_file = enc;
    } else {
        fp = fopen(enc, "rb");
        if (!fp) {
            printf("decrypt prep_mem error: could not open source\n");
            return -1;
        }
        fread(buf->enc, getSz(enc), 1, fp);
        fclose(fp);
        if (ref) {
            fp = fopen(ref, "rb");
            if (!fp) {
                printf("decrypt prep_mem error: could not open template\n");
                return -1;
            }
            fread(buf->dec, getSz(ref), 1, fp);
            fclose(fp);
        }
        ret = decrypt_patch(buf->work, buf->enc, buf->dec, getSz(enc));
        output_buf = buf->dec;
        output_size = 0x100000;
        output_file = dec;
    }

    if (ret < 0) {
        printf("work error!\n");
        free(buf);
        return -1;
    }
    
    FILE* fd = fopen(output_file, "wb");
    if (!fd) {
        printf("write output error\n");
        return -1;
    }
    fwrite(output_buf, output_size, 1, fd);
    fclose(fd);

    printf("all done: %s[0x%X]\n", output_file, output_size);
    free(buf);
    return 0;
}


// test functions, remove from final
int selftest(void) {
    printf("self test requested\n");
    void* test_buf = malloc(0x400);
    char sha[2][0x14], shastr[0x29];
    if (!test_buf)
        return -1;
    for (int i = 0; i < 4; i -= -1) {
        memset(test_buf, 0, 0x400);
        memset(sha[0], 0, sizeof(sha));
        sha1digest(sha[0], NULL, test_buf, 0x400);
        aes_cbc(key_pairs[i][0], key_pairs[i][1], test_buf, 0x400, 1);
        aes_cbc(key_pairs[i][0], key_pairs[i][1], test_buf, 0x400, 0);
        sha1digest(sha[1], NULL, test_buf, 0x400);
        if (memcmp(sha[0], sha[1], 0x14)) {
            printf("aes enc/dec error\n");
            return -1;
        }
        memset(test_buf, 0xF4, 0x400);
        memcpy(test_buf, sha, 0x28);
        aes_cbc(key_pairs[i][0], key_pairs[i][1], test_buf, 0x400, 1);
        memset(shastr, 0, sizeof(shastr));
        sha1digest(NULL, shastr, test_buf, 0x400);
        if (memcmp(shastr, selftest_hashes[i], 0x28)) {
            printf("sha error or bad keypair %d\n", i);
            return -1;
        }
    }
    printf("selftest passed\n");
    free(test_buf);
    return 0;
}

int patch_seggs(void) {
    int ps_argc = 4;
    char* ps_argv[] = { "mkernie", "decrypt", "ps_scup.bin", "ps_scup_dec.bin", "NUL", "NUL" };
    printf("ps$ ");
    for (int i = 0; i < ps_argc; i++)
        printf("%s ", ps_argv[i]);
    printf("\n");
    if (main(ps_argc, ps_argv) >= 0) {
        ps_argc = 6;
        ps_argv[1] = "diff";
        ps_argv[2] = "ps_list.bin";
        ps_argv[3] = "ps_scup_dec.bin";
        ps_argv[4] = "ps_cpatch.bin";
        ps_argv[5] = "78k0r";
        char ps_header[0x30];
        FILE* pshp = fopen("ps_scup.bin", "rb");
        if (!pshp) {
            printf("encrypt parse_args error: could not open header\n");
            return -1;
        }
        fread(ps_header, 0x30, 1, pshp);
        fclose(pshp);
        int ps_type = *(uint32_t*)(ps_header + 0x18);
        if (!ps_type)
            ps_argv[5] = "78k0r-L";
        else if (ps_type == 2)
            ps_argv[5] = "rl78";
        printf("ps$ ");
        for (int i = 0; i < ps_argc; i++)
            printf("%s ", ps_argv[i]);
        printf("\n");
        if (main(ps_argc, ps_argv) >= 0) {
            ps_argc = 6;
            ps_argv[1] = "encrypt";
            ps_argv[2] = "ps_cpatch.bin";
            ps_argv[3] = "ps_cpatch_enc.bin";
            ps_argv[4] = "ps_list.bin";
            ps_argv[5] = "ps_scup.bin";
            printf("ps$ ");
            for (int i = 0; i < ps_argc; i++)
                printf("%s ", ps_argv[i]);
            printf("\n");
            if (main(ps_argc, ps_argv) >= 0) {
                printf("patch_seggs done\n");
                return 0;
            }
        }
    }
    return -1;
}


// SHA1 & AES-128-CBC Based on tiny-sha1.c and tiny-aescbc128.c

#define Nb 4
#define Nk 4
#define Nr 10

#ifndef MULTIPLY_AS_A_FUNCTION
#define MULTIPLY_AS_A_FUNCTION 0
#endif

typedef uint8_t state_t[4][4];

static const uint8_t sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16 };

static const uint8_t rsbox[256] = {
  0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
  0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
  0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
  0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
  0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
  0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
  0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
  0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
  0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
  0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
  0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
  0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
  0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
  0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
  0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
  0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d };

static const uint8_t Rcon[11] = {
  0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36 };

#define getSBoxValue(num) (sbox[(num)])

static void KeyExpansion(uint8_t* RoundKey, const uint8_t* Key) {
    unsigned i, j, k;
    uint8_t tempa[4];

    for (i = 0; i < Nk; ++i) {
        RoundKey[(i * 4) + 0] = Key[(i * 4) + 0];
        RoundKey[(i * 4) + 1] = Key[(i * 4) + 1];
        RoundKey[(i * 4) + 2] = Key[(i * 4) + 2];
        RoundKey[(i * 4) + 3] = Key[(i * 4) + 3];
    }

    for (i = Nk; i < Nb * (Nr + 1); ++i) {
        {
            k = (i - 1) * 4;
            tempa[0] = RoundKey[k + 0];
            tempa[1] = RoundKey[k + 1];
            tempa[2] = RoundKey[k + 2];
            tempa[3] = RoundKey[k + 3];

        }

        if (i % Nk == 0) {
            {
                const uint8_t u8tmp = tempa[0];
                tempa[0] = tempa[1];
                tempa[1] = tempa[2];
                tempa[2] = tempa[3];
                tempa[3] = u8tmp;
            }

            {
                tempa[0] = getSBoxValue(tempa[0]);
                tempa[1] = getSBoxValue(tempa[1]);
                tempa[2] = getSBoxValue(tempa[2]);
                tempa[3] = getSBoxValue(tempa[3]);
            }

            tempa[0] = tempa[0] ^ Rcon[i / Nk];
        }
        j = i * 4; k = (i - Nk) * 4;
        RoundKey[j + 0] = RoundKey[k + 0] ^ tempa[0];
        RoundKey[j + 1] = RoundKey[k + 1] ^ tempa[1];
        RoundKey[j + 2] = RoundKey[k + 2] ^ tempa[2];
        RoundKey[j + 3] = RoundKey[k + 3] ^ tempa[3];
    }
}

void AES_init_ctx(struct AES_ctx* ctx, const uint8_t* key) {
    KeyExpansion(ctx->RoundKey, key);
}
void AES_init_ctx_iv(struct AES_ctx* ctx, const uint8_t* key, const uint8_t* iv) {
    KeyExpansion(ctx->RoundKey, key);
    memcpy(ctx->Iv, iv, AES_BLOCKLEN);
}
void AES_ctx_set_iv(struct AES_ctx* ctx, const uint8_t* iv) {
    memcpy(ctx->Iv, iv, AES_BLOCKLEN);
}

static void AddRoundKey(uint8_t round, state_t* state, const uint8_t* RoundKey) {
    uint8_t i, j;
    for (i = 0; i < 4; ++i) {
        for (j = 0; j < 4; ++j) {
            (*state)[i][j] ^= RoundKey[(round * Nb * 4) + (i * Nb) + j];
        }
    }
}

static void SubBytes(state_t* state) {
    uint8_t i, j;
    for (i = 0; i < 4; ++i) {
        for (j = 0; j < 4; ++j) {
            (*state)[j][i] = getSBoxValue((*state)[j][i]);
        }
    }
}

static void ShiftRows(state_t* state) {
    uint8_t temp;

    temp = (*state)[0][1];
    (*state)[0][1] = (*state)[1][1];
    (*state)[1][1] = (*state)[2][1];
    (*state)[2][1] = (*state)[3][1];
    (*state)[3][1] = temp;

    temp = (*state)[0][2];
    (*state)[0][2] = (*state)[2][2];
    (*state)[2][2] = temp;

    temp = (*state)[1][2];
    (*state)[1][2] = (*state)[3][2];
    (*state)[3][2] = temp;

    temp = (*state)[0][3];
    (*state)[0][3] = (*state)[3][3];
    (*state)[3][3] = (*state)[2][3];
    (*state)[2][3] = (*state)[1][3];
    (*state)[1][3] = temp;
}

static uint8_t xtime(uint8_t x) {
    return ((x << 1) ^ (((x >> 7) & 1) * 0x1b));
}

static void MixColumns(state_t* state) {
    uint8_t i;
    uint8_t Tmp, Tm, t;
    for (i = 0; i < 4; ++i) {
        t = (*state)[i][0];
        Tmp = (*state)[i][0] ^ (*state)[i][1] ^ (*state)[i][2] ^ (*state)[i][3];
        Tm = (*state)[i][0] ^ (*state)[i][1]; Tm = xtime(Tm);  (*state)[i][0] ^= Tm ^ Tmp;
        Tm = (*state)[i][1] ^ (*state)[i][2]; Tm = xtime(Tm);  (*state)[i][1] ^= Tm ^ Tmp;
        Tm = (*state)[i][2] ^ (*state)[i][3]; Tm = xtime(Tm);  (*state)[i][2] ^= Tm ^ Tmp;
        Tm = (*state)[i][3] ^ t;              Tm = xtime(Tm);  (*state)[i][3] ^= Tm ^ Tmp;
    }
}

#if MULTIPLY_AS_A_FUNCTION
static uint8_t Multiply(uint8_t x, uint8_t y) {
    return (((y & 1) * x) ^
        ((y >> 1 & 1) * xtime(x)) ^
        ((y >> 2 & 1) * xtime(xtime(x))) ^
        ((y >> 3 & 1) * xtime(xtime(xtime(x)))) ^
        ((y >> 4 & 1) * xtime(xtime(xtime(xtime(x))))));
}
#else
#define Multiply(x, y)                                \
      (  ((y & 1) * x) ^                              \
      ((y>>1 & 1) * xtime(x)) ^                       \
      ((y>>2 & 1) * xtime(xtime(x))) ^                \
      ((y>>3 & 1) * xtime(xtime(xtime(x)))) ^         \
      ((y>>4 & 1) * xtime(xtime(xtime(xtime(x))))))   \

#endif

#define getSBoxInvert(num) (rsbox[(num)])

static void InvMixColumns(state_t* state) {
    int i;
    uint8_t a, b, c, d;
    for (i = 0; i < 4; ++i) {
        a = (*state)[i][0];
        b = (*state)[i][1];
        c = (*state)[i][2];
        d = (*state)[i][3];

        (*state)[i][0] = Multiply(a, 0x0e) ^ Multiply(b, 0x0b) ^ Multiply(c, 0x0d) ^ Multiply(d, 0x09);
        (*state)[i][1] = Multiply(a, 0x09) ^ Multiply(b, 0x0e) ^ Multiply(c, 0x0b) ^ Multiply(d, 0x0d);
        (*state)[i][2] = Multiply(a, 0x0d) ^ Multiply(b, 0x09) ^ Multiply(c, 0x0e) ^ Multiply(d, 0x0b);
        (*state)[i][3] = Multiply(a, 0x0b) ^ Multiply(b, 0x0d) ^ Multiply(c, 0x09) ^ Multiply(d, 0x0e);
    }
}

static void InvSubBytes(state_t* state) {
    uint8_t i, j;
    for (i = 0; i < 4; ++i) {
        for (j = 0; j < 4; ++j) {
            (*state)[j][i] = getSBoxInvert((*state)[j][i]);
        }
    }
}

static void InvShiftRows(state_t* state) {
    uint8_t temp;

    temp = (*state)[3][1];
    (*state)[3][1] = (*state)[2][1];
    (*state)[2][1] = (*state)[1][1];
    (*state)[1][1] = (*state)[0][1];
    (*state)[0][1] = temp;

    temp = (*state)[0][2];
    (*state)[0][2] = (*state)[2][2];
    (*state)[2][2] = temp;

    temp = (*state)[1][2];
    (*state)[1][2] = (*state)[3][2];
    (*state)[3][2] = temp;

    temp = (*state)[0][3];
    (*state)[0][3] = (*state)[1][3];
    (*state)[1][3] = (*state)[2][3];
    (*state)[2][3] = (*state)[3][3];
    (*state)[3][3] = temp;
}

static void Cipher(state_t* state, const uint8_t* RoundKey) {
    uint8_t round = 0;

    AddRoundKey(0, state, RoundKey);

    for (round = 1; ; ++round) {
        SubBytes(state);
        ShiftRows(state);
        if (round == Nr) {
            break;
        }
        MixColumns(state);
        AddRoundKey(round, state, RoundKey);
    }

    AddRoundKey(Nr, state, RoundKey);
}

static void InvCipher(state_t* state, const uint8_t* RoundKey) {
    uint8_t round = 0;

    AddRoundKey(Nr, state, RoundKey);

    for (round = (Nr - 1); ; --round) {
        InvShiftRows(state);
        InvSubBytes(state);
        AddRoundKey(round, state, RoundKey);
        if (round == 0) {
            break;
        }
        InvMixColumns(state);
    }

}

static void XorWithIv(uint8_t* buf, const uint8_t* Iv) {
    uint8_t i;
    for (i = 0; i < AES_BLOCKLEN; ++i)     {
        buf[i] ^= Iv[i];
    }
}

void AES_CBC_encrypt_buffer(struct AES_ctx* ctx, uint8_t* buf, size_t length) {
    size_t i;
    uint8_t* Iv = ctx->Iv;
    for (i = 0; i < length; i += AES_BLOCKLEN) {
        XorWithIv(buf, Iv);
        Cipher((state_t*)buf, ctx->RoundKey);
        Iv = buf;
        buf += AES_BLOCKLEN;
    }
    memcpy(ctx->Iv, Iv, AES_BLOCKLEN);
}

void AES_CBC_decrypt_buffer(struct AES_ctx* ctx, uint8_t* buf, size_t length) {
    size_t i;
    uint8_t storeNextIv[AES_BLOCKLEN];
    for (i = 0; i < length; i += AES_BLOCKLEN) {
        memcpy(storeNextIv, buf, AES_BLOCKLEN);
        InvCipher((state_t*)buf, ctx->RoundKey);
        XorWithIv(buf, ctx->Iv);
        memcpy(ctx->Iv, storeNextIv, AES_BLOCKLEN);
        buf += AES_BLOCKLEN;
    }

}

int sha1digest(uint8_t* digest, char* hexdigest, const uint8_t* data, size_t databytes) {
#define SHA1ROTATELEFT(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

    uint32_t W[80];
    uint32_t H[] = { 0x67452301,
                    0xEFCDAB89,
                    0x98BADCFE,
                    0x10325476,
                    0xC3D2E1F0 };
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f = 0;
    uint32_t k = 0;

    uint32_t idx;
    uint32_t lidx;
    uint32_t widx;
    uint32_t didx = 0;

    int32_t wcount;
    uint32_t temp;
    uint64_t databits = ((uint64_t)databytes) * 8;
    uint32_t loopcount = (databytes + 8) / 64 + 1;
    uint32_t tailbytes = 64 * loopcount - databytes;
    uint8_t datatail[128] = { 0 };

    if (!digest && !hexdigest)
        return -1;

    if (!data)
        return -1;

    datatail[0] = 0x80;
    datatail[tailbytes - 8] = (uint8_t)(databits >> 56 & 0xFF);
    datatail[tailbytes - 7] = (uint8_t)(databits >> 48 & 0xFF);
    datatail[tailbytes - 6] = (uint8_t)(databits >> 40 & 0xFF);
    datatail[tailbytes - 5] = (uint8_t)(databits >> 32 & 0xFF);
    datatail[tailbytes - 4] = (uint8_t)(databits >> 24 & 0xFF);
    datatail[tailbytes - 3] = (uint8_t)(databits >> 16 & 0xFF);
    datatail[tailbytes - 2] = (uint8_t)(databits >> 8 & 0xFF);
    datatail[tailbytes - 1] = (uint8_t)(databits >> 0 & 0xFF);

    for (lidx = 0; lidx < loopcount; lidx++) {

        memset(W, 0, 80 * sizeof(uint32_t));

        for (widx = 0; widx <= 15; widx++) {
            wcount = 24;

            while (didx < databytes && wcount >= 0) {
                W[widx] += (((uint32_t)data[didx]) << wcount);
                didx++;
                wcount -= 8;
            }

            while (wcount >= 0) {
                W[widx] += (((uint32_t)datatail[didx - databytes]) << wcount);
                didx++;
                wcount -= 8;
            }
        }

        for (widx = 16; widx <= 31; widx++) {
            W[widx] = SHA1ROTATELEFT((W[widx - 3] ^ W[widx - 8] ^ W[widx - 14] ^ W[widx - 16]), 1);
        }
        for (widx = 32; widx <= 79; widx++) {
            W[widx] = SHA1ROTATELEFT((W[widx - 6] ^ W[widx - 16] ^ W[widx - 28] ^ W[widx - 32]), 2);
        }

        a = H[0];
        b = H[1];
        c = H[2];
        d = H[3];
        e = H[4];

        for (idx = 0; idx <= 79; idx++) {
            if (idx <= 19) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (idx >= 20 && idx <= 39) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (idx >= 40 && idx <= 59) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else if (idx >= 60 && idx <= 79) {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            temp = SHA1ROTATELEFT(a, 5) + f + e + k + W[idx];
            e = d;
            d = c;
            c = SHA1ROTATELEFT(b, 30);
            b = a;
            a = temp;
        }

        H[0] += a;
        H[1] += b;
        H[2] += c;
        H[3] += d;
        H[4] += e;
    }

    if (digest) {
        for (idx = 0; idx < 5; idx++) {
            digest[idx * 4 + 0] = (uint8_t)(H[idx] >> 24);
            digest[idx * 4 + 1] = (uint8_t)(H[idx] >> 16);
            digest[idx * 4 + 2] = (uint8_t)(H[idx] >> 8);
            digest[idx * 4 + 3] = (uint8_t)(H[idx]);
        }
    }

    if (hexdigest) {
        snprintf(hexdigest, 41, "%08x%08x%08x%08x%08x",
            H[0], H[1], H[2], H[3], H[4]);
    }

    return 0;
}