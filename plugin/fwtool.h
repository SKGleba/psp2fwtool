#define LOG_LOC "ux0:data/fwtool/log.txt"

#define ARRAYSIZE(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

static char *pcode_str[] = {
	"empty",
	"first_partition",
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
	"some_data",
	"pd0",
	"invalid"
};
	
static char *target_dev[] = {
	"TEST",
	"DEVTOOL",
	"DEX",
	"CEX",
	"QA",
	"ALL",
	"NOCHK"
};

typedef struct {
	uint16_t magic;
	uint8_t part_id;
	uint8_t type;
	uint32_t offset;
	uint32_t size;
	char sha256[0x20];
} __attribute__((packed)) rpoint_entry;

typedef struct{
	uint32_t magic;
	char target[0x10];
	rpoint_entry e2x;
	rpoint_entry ids;
	rpoint_entry cbl;
	rpoint_entry fir;
	rpoint_entry sof;
	rpoint_entry act;
	rpoint_entry reg;
	rpoint_entry iux;
	rpoint_entry iur;
} __attribute__((packed)) rpoint_super;

typedef struct {
	uint16_t magic;
	uint8_t part_id;
	uint8_t type;
	uint32_t pkg_off;
	uint32_t pkg_sz;
	uint32_t dst_off;
	uint32_t dst_sz;
	uint32_t crc32;
} __attribute__((packed)) pkg_fs_etr;

typedef struct{
	uint32_t magic;
	uint8_t version;
	uint8_t target;
	uint8_t fs_count;
	uint8_t bl_fs_no;
} __attribute__((packed)) pkg_toc;

typedef struct {
	uint32_t off;
	uint32_t sz;
	uint8_t code;
	uint8_t type;
	uint8_t active;
	uint32_t flags;
	uint16_t unk;
} __attribute__((packed)) partition_t;

typedef struct {
	char magic[0x20];
	uint32_t version;
	uint32_t device_size;
	char unk1[0x28];
	partition_t partitions[0x10];
	char unk2[0x5e];
	char unk3[0x10 * 4];
	uint16_t sig;
} __attribute__((packed)) master_block_t;

typedef struct {
	const char *dev;
	const char *dev2;
	const char *blkdev;
	const char *blkdev2;
	int id;
} SceIoDevice;

typedef struct {
	int id;
	const char *dev_unix;
	int unk;
	int dev_major;
	int dev_minor;
	const char *dev_filesystem;
	int unk2;
	SceIoDevice *dev;
	int unk3;
	SceIoDevice *dev2;
	int unk4;
	int unk5;
	int unk6;
	int unk7;
} SceIoMountPoint;

