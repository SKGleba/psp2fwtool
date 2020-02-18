#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>

#include "../plugin/fwtool.h"
#include "../plugin/crc32.c"

static char tocblk[0x8];
static uint8_t target_type = 6, emmc_target = 0;

uint32_t getSz(const char *src) {
	FILE *fp = fopen(src, "rb");
	if (fp == NULL)
		return 0;
	fseek(fp, 0L, SEEK_END);
	uint32_t sz = ftell(fp);
	fclose(fp);
	return sz;
}

void read_image() {
	printf("\nImage info:\n size: %dB\n", getSz("fwimage.bin"));
	FILE *fp = fopen("fwimage.bin", "rb");
	if (fp == NULL)
		return;
	fread(tocblk,8,1,fp);
	pkg_toc *totoc = (pkg_toc *)tocblk;
	printf(" magic: 0x%lX\n version: %d\n target: %s\n flash-able: %s\n blobs count: %d\n\nUpdate blobs:\n", totoc->magic, totoc->version, target_dev[totoc->target], (totoc->fmode) ? "YES" : "NO", totoc->fs_count);
	uint32_t off = sizeof(pkg_toc), ret = 1;
	uint8_t ecount = 0;
	pkg_fs_etr fsa;
	while (ecount < totoc->fs_count) {
		fseek(fp, off, SEEK_SET);
		fread((void *)&fsa,sizeof(pkg_fs_etr),1,fp);
		printf(" magic: 0x%02X\n fwimage offset: %d\n compressed size: %d\n target device: sdstor0:%s-lp-%s-%s\n device offset: %d\n final size: %d\n crc32: 0x%lX\n\n", fsa.magic, fsa.pkg_off, fsa.pkg_sz, stor_st[fsa.dst_etr[0]], stor_rd[fsa.dst_etr[1]], stor_th[fsa.dst_etr[2]], fsa.dst_off, fsa.dst_sz, fsa.crc32);
		ecount = ecount + 1;
		off = fsa.pkg_off + fsa.pkg_sz;
	}
	
}

void info(int new) {
	FILE *fp = fopen("fwimage.bin", (new) ? "wb" : "rb+");
	if (!new) {
		fread(tocblk,8,1,fp);
		fseek(fp, 0L, SEEK_SET);
	} else
		memset(tocblk, 0, 8);
	pkg_toc *totoc = (pkg_toc *)tocblk;
	totoc->magic = 0xDEAFBABE;
	totoc->version = 1;
	totoc->target = target_type;
	totoc->fmode = emmc_target;
	totoc->fs_count = (new) ? 0 : totoc->fs_count + 1;
	fwrite(tocblk,8,1,fp);
	fclose(fp);
	if (new)
		printf("\nNEW fwimage:\n magic: 0x%lX\n version: %d\n target: %s\n flash-able: %s\n\n", totoc->magic, totoc->version, target_dev[totoc->target], (totoc->fmode) ? "YES" : "NO");
}

static uint32_t get_block_crc32_file(char *inp) {
	char crcbuf[0x200];
	FILE *fp = fopen(inp, "rb");
	fread(&crcbuf,0x200,1,fp);
	fclose(fp);
	return crc32(0, &crcbuf, 0x200);;
}

int write_entry(uint32_t dst_sz, uint8_t master, uint8_t active, uint8_t partition, uint32_t dst_off) {
	if (dst_sz == 0)
		return -1;
	pkg_fs_etr fs_etr;
	fs_etr.magic = 0x69;
	fs_etr.pkg_off = getSz("fwimage.bin") + sizeof(pkg_fs_etr);
	fs_etr.pkg_sz = getSz("rawfs.gz");
	fs_etr.dst_etr[0] = master;
	fs_etr.dst_etr[1] = active;
	fs_etr.dst_etr[2] = partition;
	fs_etr.dst_off = dst_off;
	fs_etr.dst_sz = dst_sz;
	fs_etr.crc32 = get_block_crc32_file("rawfs.gz");
	FILE *fp = fopen("fwimage.bin", "rb+");
	fseek(fp, 0L, SEEK_END);
	fwrite((void *)&fs_etr, sizeof(pkg_fs_etr), 1, fp);
	fclose(fp);
	system("cat rawfs.gz >> fwimage.bin");;
	return 0;
}

int add_os0() {
	printf("Adding kernel image...\n");
	system("gzip -9 -k os0.bin");
	system("mv os0.bin.gz rawfs.gz");
	if (write_entry(getSz("os0.bin"), 0, (target_type == 6) ? 0 : 1, 4, 0) < 0)
		return 0;
	unlink("rawfs.gz");
	info(0);
	return 0;
}

int add_slb2() {
	printf("Adding bootloaders image...\n");
	system("gzip -9 -k slb2.bin");
	system("mv slb2.bin.gz rawfs.gz");
	if (write_entry(getSz("slb2.bin"), 0, (target_type == 6) ? 0 : 1, 3, 0) < 0)
		return 0;
	unlink("rawfs.gz");
	info(0);
	return 0;
}

int add_vs0() {
	printf("Adding system image...\n");
	uint32_t coff = 0;
	uint8_t cur = 0;
	char cfname[64], mvcmd[128];
	system("split -b 16777216 -d vs0.bin vs0.bin_");
	system("gzip -9 -k vs0.bin_*");
	while(1) {
		memset(cfname, 0, 16);
		memset(mvcmd, 0, 128);
		sprintf(cfname, "vs0.bin_%02d", cur);
		coff = getSz(cfname);
		if (coff == 0)
			break;
		sprintf(mvcmd, "mv %s.gz rawfs.gz", cfname);
		system(mvcmd);
		if (write_entry(coff, 0, 2, 5, cur * 0x1000000) < 0)
			break;
		unlink("rawfs.gz");
		unlink(cfname);
		info(0);
		cur = cur + 1;
	}
	return 0;
}

int add_pd0() {
	printf("Adding preload image...\n");
	uint32_t coff = 0;
	uint8_t cur = 0;
	char cfname[64], mvcmd[128];
	system("split -b 16777216 -d pd0.bin pd0.bin_");
	system("gzip -9 -k pd0.bin_*");
	while(1) {
		memset(cfname, 0, 16);
		memset(mvcmd, 0, 128);
		sprintf(cfname, "pd0.bin_%02d", cur);
		coff = getSz(cfname);
		if (coff == 0)
			break;
		sprintf(mvcmd, "mv %s.gz rawfs.gz", cfname);
		system(mvcmd);
		if (write_entry(coff, 0, 2, 15, cur * 0x1000000) < 0)
			break;
		unlink("rawfs.gz");
		unlink(cfname);
		info(0);
		cur = cur + 1;
	}
	return 0;
}

int add_sa0() {
	printf("Adding sysdata image...\n");
	uint32_t coff = 0;
	uint8_t cur = 0;
	char cfname[64], mvcmd[128];
	system("split -b 16777216 -d sa0.bin sa0.bin_");
	system("gzip -9 -k sa0.bin_*");
	while(1) {
		memset(cfname, 0, 16);
		memset(mvcmd, 0, 128);
		sprintf(cfname, "sa0.bin_%02d", cur);
		coff = getSz(cfname);
		if (coff == 0)
			break;
		sprintf(mvcmd, "mv %s.gz rawfs.gz", cfname);
		system(mvcmd);
		if (write_entry(coff, 0, 2, 13, cur * 0x1000000) < 0)
			break;
		unlink("rawfs.gz");
		unlink(cfname);
		info(0);
		cur = cur + 1;
	}
	return 0;
}

int main (int argc, char *argv[]) {
	
	if(argc < 2){
		printf ("\nusage: ./[binname] [devices]\n");
		return -1;
	}
	
	// used devices
	for (int i=1; i< argc; i++) {
     	if (strcmp("-target", argv[i]) == 0) {
       		i = i + 1;
        	target_type = (uint8_t)atoi(argv[i]);
    	} else if (strcmp("-flashable", argv[i]) == 0)
        	emmc_target = 1;
		else if (strcmp("-info", argv[i]) == 0) {
			read_image();
			return 0;
		}
 	}
	
	info(1);
  
	// used devices
	for (int i=1; i< argc; i++) {
     	if (strcmp("-kernel", argv[i]) == 0)
       		add_os0();
    	else if (strcmp("-bootloaders", argv[i]) == 0)
        	add_slb2();
		else if (strcmp("-system", argv[i]) == 0)
        	add_vs0();
		else if (strcmp("-sysdata", argv[i]) == 0)
        	add_sa0();
		else if (strcmp("-preload", argv[i]) == 0)
        	add_pd0();
 	}
	
	printf("\nfinished: fwimage.bin\n");
	
 	return 0;
}