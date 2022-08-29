/* THIS FILE IS A PART OF PSP2FWTOOL
 *
 * Copyright (C) 2019-2022 skgleba
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __FWTOOL_H__
#define __FWTOOL_H__

// apex-----------------------
#define BLOCK_SIZE 0x200
#define E2X_SIZE_BLOCKS 0x2E
#define BL_BUF_SZ_BLOCKS 0x2000
#define FSP_BUF_SZ_BLOCKS 0x8000
#define E2X_SIZE_BYTES (E2X_SIZE_BLOCKS * BLOCK_SIZE)
#define BL_BUF_SZ_BYTES (BL_BUF_SZ_BLOCKS * BLOCK_SIZE)
#define FSP_BUF_SZ_BYTES (FSP_BUF_SZ_BLOCKS * BLOCK_SIZE)

// fwtool------------------------
#define FWTOOL_VERSION_STR "FWTOOL v1.3.5 by SKGleba"
#define LOG_LOC "ux0:data/fwtool/log.txt"
#define FWTOOL_MINFW 0x03600000
#define FWTOOL_MAXFW 0x03740011

enum FWTOOL_MINI_COMMANDS {
	CMD_SET_FWIMG_PATH,
	CMD_GET_MBR,
	CMD_GET_BL,
	CMD_GET_GZ,
	CMD_GET_FSP,
	CMD_SET_FILE_LOGGING,
	CMD_CMP_TARGET,
	CMD_BL_TO_FSP,
	CMD_UMOUNT,
	CMD_WRITE_REDIRECT,
	CMD_GRW_MOUNT,
	CMD_SET_INACTIVE_BL_SHA256,
	CMD_GET_ENSO_STATUS,
	CMD_NO_BL_PERSONALIZE,
	CMD_SET_FWRP_PATH,
	CMD_GET_REAL_MBR,
	CMD_GET_LOCK_STATE,
	CMD_SKIP_CRC,
	CMD_VALIDATE_KBLFW,
	CMD_SET_PERF_MODE,
	CMD_GET_DUALOS_HEADER,
	CMD_WIPE_DUALOS,
	CMD_GET_HW_REV,
	CMD_FORCE_DEV_UPDATE,
	CMD_REBOOT
};

// fwimage---------------------
#define CFWIMG_NAME "psp2cfw"
#define CFWIMG_MAGIC 0xCAFEBABE
#define CFWIMG_VERSION 3
#define FSPART_MAGIC 0xAA12
enum FSPART_TYPES { FSPART_TYPE_FS, FSPART_TYPE_BL, FSPART_TYPE_E2X, FSPART_TYPE_DEV };
enum FWTARGETS { FWTARGET_SYSDBG, FWTARGET_DEVKIT, FWTARGET_TESTKIT, FWTARGET_RETAIL, FWTARGET_QASETTLE, FWTARGET_ALL, FWTARGET_SAFE };
static char* target_dev[] = { "TEST", "DEVTOOL", "DEX", "CEX", "QA", "ALL", "NOCHK" };
enum DEVICE_COMPONENTS {
	DEV_SYSCON_FW, // ernie firmware
	DEV_RESERVED, // --ignore--
	DEV_SYSCON_UNK, // unknown, devkit-related ernie component
	DEV_SYSCON_DL, // ernie downloader/updater
	DEV_MOTION0, // berkley type 0 firmware
	DEV_MOTION1, // berkley type 1 firmware
	DEV_CP, // grover
	DEV_BIC_FW, // abby firmware
	DEV_BIC_DF, // abby data flash
	DEV_TOUCH_FW, // touch firmware
	DEV_TOUCH_CFG, // touch config
	DEV_COM, // BBMC / 3g module; Zoe?
	DEV_NODEV
};
static char* dcode_str[] = { "syscon_fw", "reserved", "syscon_unk", "syscon_dl", "motion0", "motion1", "cp", "bic_fw", "bic_df", "touch_fw", "touch_cfg", "com", "invalid" };

struct _pkg_fs_etr {
	uint16_t magic;
	uint8_t part_id;
	uint8_t type;
	uint32_t pkg_off;
	uint32_t pkg_sz;
	uint32_t dst_off;
	uint32_t dst_sz;
	uint32_t crc32;
	uint32_t hdr2;
	uint32_t hdr3;
} __attribute__((packed));
typedef struct _pkg_fs_etr pkg_fs_etr;

struct _pkg_toc {
	uint32_t magic;
	uint8_t version;
	uint8_t target;
	uint8_t fs_count;
	uint8_t bl_fs_no;
	uint32_t target_hw_rev;
	uint32_t target_hw_mask;
	uint32_t fw_version;
	char build_info[0x28];
	uint32_t toc_crc32;
} __attribute__((packed));
typedef struct _pkg_toc pkg_toc;

// restore point-----------
#define RPOINT_NAME "psp2rpoint"
#define RPOINT_MAGIC 0xC00F2020

struct _emmcimg_super {
	uint32_t magic;
	uint32_t size;
	unsigned char target[0x10];
	uint32_t blk_crc[0xF7];
	uint32_t prev_crc;
} __attribute__((packed));
typedef struct _emmcimg_super emmcimg_super;


// dualOS-----------------
#define DUALOS_MAGIC 0x92384D05
#define DOS_SLAVE_START 0x400000
#define DOS_UR0_SIZE 0x100000
#define DOS_UX0_OFFSET 0x300000
#define DOS_UX0_SIZE 0x100000
#define DOS_RESERVED_SZ 0x22000
#define DOS_BKP_BLOCK_SZ 0x8000
#define DOS_MASTER_BKP_START 0x2000
#define DOS_MASTER_OS0_START (DOS_MASTER_BKP_START + DOS_BKP_BLOCK_SZ)
#define DOS_SLAVE_BKP_START (DOS_MASTER_BKP_START + (2 * DOS_BKP_BLOCK_SZ))
#define DOS_SLAVE_OS0_START (DOS_SLAVE_BKP_START + DOS_BKP_BLOCK_SZ)

struct _dualos_super_t {
	uint32_t magic;
	uint32_t version;
	uint32_t device_size;
	uint32_t master_mode;
	uint32_t master_crc[2];
	uint32_t slave_crc[2];
} __attribute__((packed));
typedef struct _dualos_super_t dualos_super_t;

// npup--------------------
#define NPUP_NUNK 'CFW'
#define NPUP_FWIMAGE_ID 'IMG'
#define NPUP_ADDCONT_ID 'ADD'
#define NPUP_PREIMSG_ID 'MSG'
#define NPUP_VERSION 0xDEADBABE
#define NPUP_MAGIC 0x0100004655454353

struct _ScePupSegmentInfo { // size is 0x20
	uint64_t entry_id;
	uint64_t data_offset;
	uint64_t data_length;
	uint64_t unk_0x18;		// ex:2
} __attribute__((packed));
typedef struct _ScePupSegmentInfo ScePupSegmentInfo;

struct _npup_hdr { // size is 0x400
	uint64_t magic;
	uint32_t package_version;
	uint32_t unk_0x0C;
	uint32_t image_version;
	uint32_t unk_0x14;
	uint32_t file_count;
	uint32_t unk_0x1C;
	uint64_t header_length;
	uint64_t package_length;
	char bs_0[0x80 - 48];
	ScePupSegmentInfo version_info;
	ScePupSegmentInfo disclaimer_info;
	ScePupSegmentInfo updater_info;
	ScePupSegmentInfo fwimage_info;
	ScePupSegmentInfo addcont_info;
	char padding[0x200 - (0x80 + 0x20 * 5)];
	char fw_string_block[0x200];
} __attribute__((packed));
typedef struct _npup_hdr npup_hdr;

enum PUP_SPACKAGES {
	SPKG_INVALID_0,
	SPKG_KERNEL,
	SPKG_INVALID_2,
	SPKG_UNK_LIST,
	SPKG_SYSTEM_CHMOD,
	SPKG_INVALID_5,
	SPKG_INVALID_6,
	SPKG_INVALID_7,
	SPKG_SYSCON_FW,
	SPKG_SBLS,
	SPKG_SYSTEM,
	SPKG_CP,
	SPKG_MOTION0,
	SPKG_COM,
	SPKG_INVALID_E,
	SPKG_MOTION1,
	SPKG_TOUCH_FW,
	SPKG_TOUCH_CFG,
	SPKG_BIC_FW,
	SPKG_BIC_DF,
	SPKG_SYSCON_UNK,
	SPKG_KERNEL_UNK,
	SPKG_SYSTEM_PATCH,
	SPKG_SYSDATA,
	SPKG_PREINSTALL,
	SPKG_SYSCON_DL,
	SPKG_INVALID_1A,
	SPKG_PSP_EMULIST,
	SPKG_NVS_UNK
};


// SCE MBR--------------------
#define SCE_MAGIC_H 'ECS'
#define SBLS_MAGIC_H '2BLS'
#define SCEMBR_MAGIC "Sony Computer Entertainment Inc."

enum SCEMBR_FSTYPE {
	SCEMBR_FS_FAT16 = 0x6,
	SCEMBR_FS_EXFAT = 0x7,
	SCEMBR_FS_RAW = 0xDA
};

enum SCEMBR_PARTITIONS {
	SCEMBR_PART_EMPTY,
	SCEMBR_PART_IDSTORAGE,
	SCEMBR_PART_SBLS,
	SCEMBR_PART_KERNEL,
	SCEMBR_PART_SYSTEM,
	SCEMBR_PART_REGISTRY,
	SCEMBR_PART_ACTIVATION,
	SCEMBR_PART_USERDATA,
	SCEMBR_PART_USEREXT,
	SCEMBR_PART_GAMERO,
	SCEMBR_PART_GAMERW,
	SCEMBR_PART_UPDATER,
	SCEMBR_PART_SYSDATA,
	SCEMBR_PART_DRM,
	SCEMBR_PART_PREINSTALL,
	SCEMBR_PART_UNUSED
};

static char* pcode_str[] = {
	"empty",
	"idstorage",
	"slb2",
	"os0",
	"vs0",
	"vd0",
	"tm0",
	"ur0",
	"ux0",
	"gro0",
	"grw0",
	"ud0",
	"sa0",
	"mediaid",
	"pd0",
	"unused" // actual valid partition
};

struct _partition_t {
	uint32_t off;
	uint32_t sz;
	uint8_t code;
	uint8_t type;
	uint8_t active;
	uint32_t flags;
	uint16_t unk;
} __attribute__((packed));
typedef struct _partition_t partition_t;

struct _master_block_t {
	char magic[0x20];
	uint32_t version;
	uint32_t device_size;
	char unk1[0x28];
	partition_t partitions[0x10];
	char unk2[0x5e];
	char unk3[0x10 * 4];
	uint16_t sig;
} __attribute__((packed));
typedef struct _master_block_t master_block_t;

//misc--------------------
#define ALIGN_SECTOR(s) ((s + (BLOCK_SIZE - 1)) & -BLOCK_SIZE) // align (arg) to BLOCK_SIZE
#define ARRAYSIZE(x) ((sizeof(x) / sizeof(0 [x])) / ((size_t)(!(sizeof(x) % sizeof(0 [x])))))

#endif