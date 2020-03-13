#define LOG_LOC "ux0:data/fwtool.log"
#define LOGGING_ENABLED 1

#define ARRAYSIZE(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

static char *stor_st[] = {
	"int",
	"ext",
	"gcd",
	"mcd",
	"xmc",
	"uma"
};
static char *stor_rd[] = {
	"ina",
	"act",
	"ign"
};

static char *stor_th[] = {
	"a",
	"unused",
	"idstor",
	"sloader",
	"os",
	"vsh",
	"vshdata",
	"vtrm",
	"user",
	"userext",
	"gamero",
	"gamerw",
	"updater",
	"sysdata",
	"mediaid",
	"pidata",
	"entire"
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

typedef struct{
	char *inp;
	char *oup;
	uint8_t version;
	uint8_t target;
	uint8_t wmode;
	uint8_t fmode;
	uint16_t fw_minor;
} __attribute__((packed)) il_mode;

typedef struct {
	uint8_t magic;
	uint32_t pkg_off;
	uint32_t pkg_sz;
	uint8_t dst_etr[3];
	uint32_t dst_off;
	uint32_t dst_sz;
	uint32_t crc32;
} __attribute__((packed)) pkg_fs_etr;

typedef struct{
	uint32_t magic;
	uint8_t version;
	uint8_t target;
	uint8_t fmode;
	uint8_t fs_count;
	uint8_t has_e2x;
	uint16_t fw_minor;
	uint8_t changefw;
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
