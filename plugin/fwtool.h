#define LOG_LOC "ux0:data/fwtool.log"
#define LOGGING_ENABLED 1

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
} __attribute__((packed)) pkg_toc;
