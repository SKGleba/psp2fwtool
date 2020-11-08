#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>
#include <fcntl.h>

#include "../plugin/fwtool.h"
#include "../plugin/crc32.c"

uint32_t getSz(const char *src) {
	FILE *fp = fopen(src, "rb");
	if (fp == NULL)
		return 0;
	fseek(fp, 0L, SEEK_END);
	uint32_t sz = ftell(fp);
	fclose(fp);
	return sz;
}

static uint32_t get_block_crc32_file(char *inp) {
	char crcbuf[0x200];
	FILE *fp = fopen(inp, "rb");
	fread(&crcbuf,0x200,1,fp);
	fclose(fp);
	return crc32(0, &crcbuf, 0x200);;
}

int fat2e2x() {
	unlink("e2x.bin");
	printf("Creating e2x image from fat.bin\n");
	system("dd if=fat.bin of=e2x.bin ibs=1024 skip=1");
	if (getSz("e2x.bin") != (0x6000 - 0x400)) {
		printf("Unk size\n");
		return -1;
	}
	return 0;
}

void add_entry(FILE *fd, const char *src, uint8_t pid, pkg_toc *fwimg_toc, uint32_t dstoff) {
	uint32_t fsize = getSz(src), gsize = 0;
	if (fsize == 0)
		return;
	pkg_fs_etr fs_entry;
	char cmdbuf[128];
	memset(cmdbuf, 0, 128);
	memset(&fs_entry, 0, sizeof(pkg_fs_etr));
	sprintf(cmdbuf, "gzip -9 -k %s", src);
	system(cmdbuf);
	system("mv *.gz rawfs.gz");
	gsize = getSz("rawfs.gz");
	system("cat rawfs.gz >> fwimage.bin_part");
	fs_entry.magic = 0xAA12;
	fs_entry.part_id = pid;
	fs_entry.pkg_off = fwimg_toc->fs_count;
	fs_entry.pkg_sz = gsize;
	fs_entry.dst_off = dstoff;
	fs_entry.dst_sz = fsize;
	fs_entry.crc32 = get_block_crc32_file("rawfs.gz");
	if (pid == 2) {
		fs_entry.type = 1;
		fwimg_toc->bl_fs_no = fwimg_toc->fs_count;
	} else if (pid == 0)
		fs_entry.type = 2;
	fwimg_toc->fs_count-=-1;
	fwrite(&fs_entry, sizeof(pkg_fs_etr), 1, fd);
	unlink("rawfs.gz");
	return;
}

void add_entry_proxy(FILE *fd, const char *src, uint8_t pid, pkg_toc *fwimg_toc) {
	printf("looking for %s\n", src);
	uint32_t fsize = getSz(src);
	if (fsize == 0) {
		printf("does not exist\n");
		return;
	}
	
	if (fsize < 0x1000001) {
		printf("adding (small)... ");
		add_entry(fd, src, pid, fwimg_toc, 0);
		printf("done\n");
		return;
	}
	
	uint32_t coff = 0, csz = 0;
	uint8_t cur = 0;
	char cfname[16], cmdbuf[128];
	memset(cmdbuf, 0, 128);
	sprintf(cmdbuf, "split -b 16777216 -d %s %s_", src, src);
	system(cmdbuf);
	while(1) {
		memset(cfname, 0, 16);
		sprintf(cfname, "%s_%02d", src, cur);
		printf("looking for %s\n", cfname);
		csz = getSz(cfname);
		if (csz == 0)
			break;
		printf("adding (big)... ", cfname);
		add_entry(fd, cfname, pid, fwimg_toc, coff);
		printf("done\n");
		unlink(cfname);
		coff-=-csz;
		cur-=-1;
	}
	
	printf("done\n");
	return;
}

void sync_fwimage(const char *imagepath, pkg_toc *fwimg_toc) {
	char cmdbuf[128];
	memset(cmdbuf, 0, 128);
	sprintf(cmdbuf, "cat fwimage.bin_part >> %s", imagepath);
	system(cmdbuf);
	unlink("fwimage.bin_part");
	
	int fp = open(imagepath, O_RDWR);
	if (fp < 0)
		return;
	pwrite(fp, fwimg_toc, sizeof(pkg_toc), 0);
	
	uint8_t ecount = 0;
	pkg_fs_etr fs_entry;
	uint32_t soff = sizeof(pkg_toc), boff = (sizeof(pkg_toc) + (fwimg_toc->fs_count * sizeof(pkg_fs_etr)));
	while (ecount < fwimg_toc->fs_count) {
		memset(&fs_entry, 0, sizeof(pkg_fs_etr));
		pread(fp, &fs_entry, sizeof(pkg_fs_etr), soff);
		fs_entry.pkg_off = boff;
		pwrite(fp, &fs_entry, sizeof(pkg_fs_etr), soff);
		printf("\nFS_PART[%d] - magic 0x%04X | type %d\n"
			" READ: size 0x%X | offset 0x%X | ungzip %d\n"
			" WRITE: size 0x%X | offset 0x%X @ id %d\n"
			" PART_CRC32: 0x%08X\n",
			ecount, fs_entry.magic, fs_entry.type,
			fs_entry.pkg_sz, fs_entry.pkg_off, (fs_entry.pkg_sz < fs_entry.dst_sz),
			fs_entry.dst_sz, fs_entry.dst_off, fs_entry.part_id,
			fs_entry.crc32);
		soff-=-sizeof(pkg_fs_etr);
		boff-=-fs_entry.pkg_sz;
		ecount-=-1;
	}
	
	close(fp);
	return;
}

void read_image(const char *image) {
	int fp = open(image, O_RDWR);
	if (fp < 0)
		return;
	pkg_toc fwimg_toc;
	pread(fp, &fwimg_toc, sizeof(pkg_toc), 0);
	printf("Image magic: 0x%X\nImage version: 0x%X\nTarget type: %s\nFS_PART count: %d\n\n", fwimg_toc.magic, fwimg_toc.version, target_dev[fwimg_toc.target], fwimg_toc.fs_count);
	uint8_t ecount = 0;
	pkg_fs_etr fs_entry;
	uint32_t soff = sizeof(pkg_toc), boff = (sizeof(pkg_toc) + (fwimg_toc.fs_count * sizeof(pkg_fs_etr)));
	while (ecount < fwimg_toc.fs_count) {
		memset(&fs_entry, 0, sizeof(pkg_fs_etr));
		pread(fp, &fs_entry, sizeof(pkg_fs_etr), soff);
		printf("\nFS_PART[%d] - magic 0x%04X | type %d\n"
			" READ: size 0x%X | offset 0x%X | ungzip %d\n"
			" WRITE: size 0x%X | offset 0x%X @ id %d\n"
			" PART_CRC32: 0x%08X\n",
			ecount, fs_entry.magic, fs_entry.type,
			fs_entry.pkg_sz, fs_entry.pkg_off, (fs_entry.pkg_sz < fs_entry.dst_sz),
			fs_entry.dst_sz, fs_entry.dst_off, fs_entry.part_id,
			fs_entry.crc32);
		soff-=-sizeof(pkg_fs_etr);
		ecount-=-1;
	}
	close(fp);
	return;
}

int main (int argc, char *argv[]) {
	
	if(argc < 3){
		printf ("\nusage: ./[binname] [file] [opt]\n");
		return -1;
	}
	
	uint8_t target_type = 6;
	
	// used devices
	for (int i=2; i< argc; i++) {
     	if (strcmp("-target", argv[i]) == 0) {
       		i = i + 1;
        	target_type = (uint8_t)atoi(argv[i]);
    	} else if (strcmp("-info", argv[i]) == 0) {
			read_image(argv[1]);
			return 0;
		}
 	}
	
	unlink(argv[1]);
	
	printf("opening %s\n", argv[1]);
	FILE *fd = fopen(argv[1], "wb");
	if (fd == NULL) {
		printf("error\n");
		return 0;
	}
	
	pkg_toc fwimg_toc;
	memset(&fwimg_toc, 0, sizeof(pkg_toc));
	fwimg_toc.magic = 0xCAFEBABE;
	fwimg_toc.version = 2;
	fwimg_toc.target = target_type;
	fwrite(&fwimg_toc, sizeof(pkg_toc), 1, fd);
	
	add_entry_proxy(fd, "sa0.bin", 0xC, &fwimg_toc);
	add_entry_proxy(fd, "pd0.bin", 0xE, &fwimg_toc);
	add_entry_proxy(fd, "os0.bin", 0x3, &fwimg_toc);
	add_entry(fd, "slb2.bin", 0x2, &fwimg_toc, 0);
	add_entry_proxy(fd, "vs0.bin", 0x4, &fwimg_toc);
	if (fat2e2x() == 0)
		add_entry(fd, "e2x.bin", 0, &fwimg_toc, 0x400);
	
	fclose(fd);
	
	sync_fwimage(argv[1], &fwimg_toc);
	
	printf("\nfinished: %s\n", argv[1]);
	return 0;
}