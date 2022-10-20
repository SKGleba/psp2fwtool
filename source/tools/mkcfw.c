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
#include <stdbool.h>

#include "../fwtool.h"
#include "../kernel/crc32.c"

//misc--------------------
#define ALIGN_SECTOR(s) ((s + (BLOCK_SIZE - 1)) & -BLOCK_SIZE) // align (arg) to BLOCK_SIZE
#define ARRAYSIZE(x) ((sizeof(x) / sizeof(0 [x])) / ((size_t)(!(sizeof(x) % sizeof(0 [x])))))

#ifdef WINDOWS
#include <windows.h>
#include <io.h>
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
#endif

uint32_t getSz(const char* src) {
	FILE* fp = fopen(src, "rb");
	if (!fp)
		return 0;
	fseek(fp, 0L, SEEK_END);
	uint32_t sz = ftell(fp);
	fclose(fp);
	return sz;
}

int split(const char* src, const char* dst_format, uint32_t part_size) {
	uint32_t full_size = getSz(src);
	if (!full_size)
		return -1;

	void* full_file = calloc(1, full_size);
	if (!full_file)
		return -2;

	printf("Splitting %s into 0x%08X-byte chunks\nReading... ", src, part_size);
	
	FILE* fp = fopen(src, "rb");
	if (!fp) {
		free(full_file);
		printf("FAILED\n");
		return -3;
	}
	fread(full_file, full_size, 1, fp);
	fclose(fp);
	printf("OK\nWriting..\n");

	char cur_path[32];
	uint8_t cur_dst = 0;
	for (uint32_t copied = 0; copied < full_size; copied += part_size) {
		memset(cur_path, 0, 32);
		snprintf(cur_path, 32, "%s%s%02d", src, dst_format, cur_dst);
		fp = fopen(cur_path, "wb");
		if (!fp) {
			free(full_file);
			printf("write open %s failed\n", cur_path);
			return -4;
		}
		if (copied + part_size < full_size) {
			printf("0x%08X @ %s[0x%08X] -> %s\n", part_size, src, copied, cur_path);
			fwrite(full_file + copied, part_size, 1, fp);
		} else {
			printf("0x%08X @ %s[0x%08X] -> %s\n", full_size - copied, src, copied, cur_path);
			fwrite(full_file + copied, full_size - copied, 1, fp);
		}
		fclose(fp);
		cur_dst++;
	}

	free(full_file);

	printf("Split %s into %d chunks\n", src, cur_dst);

	return 0;
}

int append(const char* source, const char* dest) {
	uint32_t ap_size = getSz(source);
	if (!ap_size)
		return -1;

	void* buf = calloc(1, ap_size);
	if (!buf)
		return -2;

	FILE* fp = fopen(source, "rb");
	if (!fp) {
		free(buf);
		return -3;
	}
	fread(buf, ap_size, 1, fp);
	fclose(fp);

	fp = fopen(dest, "ab");
	if (!fp) {
		free(buf);
		return -4;
	}
	fwrite(buf, ap_size, 1, fp);
	fclose(fp);

	free(buf);

	return 0;
}

static uint32_t get_block_crc32_file(char* inp, uint32_t belowblock) {
	uint8_t crcbuf[BLOCK_SIZE];
	FILE* fp = fopen(inp, "rb");
	if (belowblock)
		fread(crcbuf, belowblock, 1, fp);
	else
		fread(crcbuf, BLOCK_SIZE, 1, fp);
	fclose(fp);
	if (belowblock)
		return crc32(0, crcbuf, belowblock);
	return crc32(0, crcbuf, BLOCK_SIZE);
}

int fat2e2x() {
	unlink("slim.e2x");
	printf("Creating e2x image from enso.bin\n");
	uint32_t fatsize = getSz("enso.bin");
	if (!fatsize || fatsize != E2X_SIZE_BYTES + 0x400)
		return -1;
	void* fatbuf = calloc(1, E2X_SIZE_BYTES + 0x400);
	if (!fatbuf)
		return -1;
	FILE* fp = fopen("enso.bin", "rb");
	fread(fatbuf, E2X_SIZE_BYTES + 0x400, 1, fp);
	fclose(fp);

	// TODO: some checks

	fp = fopen("slim.e2x", "wb");
	fwrite(fatbuf + 0x400, E2X_SIZE_BYTES, 1, fp);
	fclose(fp);

	if (getSz("slim.e2x") != E2X_SIZE_BYTES) {
		printf("Unknown e2x size!\n");
		return -1;
	}

	free(fatbuf);
	return 0;
}

void add_entry(FILE* fd, const char* src, uint8_t pid, pkg_toc* fwimg_toc, uint32_t dstoff, uint32_t hdr2, uint32_t hdr3) {
	uint32_t gcrc = 0, gsize = 0;
	uint32_t fsize = getSz(src);
	if (!fsize)
		return;
	if (pid == SCEMBR_PART_EMPTY && dstoff != 0x400) {
		for (int i = 0; i < E2X_MISC_NOTYPE; i++) {
			if (dstoff == e2x_misc_type_offsets[i])
				fsize = e2x_misc_type_sizes[i];
		}
	}
	char cmdbuf[128];
	memset(cmdbuf, 0, 128);
	sprintf(cmdbuf, "gzip -9 -k %s", src);
	system(cmdbuf);
#ifdef WINDOWS
	system("ren *.gz rawfs.gz");
#else
	system("mv *.gz rawfs.gz");
#endif
	gsize = getSz("rawfs.gz");
	if (!gsize)
		return;
	gcrc = get_block_crc32_file("rawfs.gz", (gsize > BLOCK_SIZE) ? BLOCK_SIZE : gsize);
	append("rawfs.gz", "fwimage.bin_part");
	unlink("rawfs.gz");
	int target_component = 0;
incr_comp:
	if (pid > SCEMBR_PART_UNUSED) {
		pid = pid - 0x10;
		target_component -= -1;
		goto incr_comp;
	}
	pkg_fs_etr fs_entry;
	memset(&fs_entry, 0, sizeof(pkg_fs_etr));
	fs_entry.magic = FSPART_MAGIC;
	fs_entry.part_id = pid;
	fs_entry.pkg_off = fwimg_toc->fs_count;
	fs_entry.pkg_sz = gsize;
	fs_entry.dst_off = dstoff;
	fs_entry.dst_sz = fsize;
	fs_entry.crc32 = gcrc;
	fs_entry.hdr2 = hdr2;
	fs_entry.hdr3 = hdr3;
	if (target_component)
		fs_entry.type = FSPART_TYPE_DEV;
	else {
		if (pid == SCEMBR_PART_SBLS) {
			fs_entry.type = FSPART_TYPE_BL;
			fwimg_toc->bl_fs_no = fwimg_toc->fs_count;
		} else if (pid == SCEMBR_PART_EMPTY && dstoff == 0x400)
			fs_entry.type = FSPART_TYPE_E2X;
	}
	fwimg_toc->fs_count -= -1;
	fwrite(&fs_entry, sizeof(pkg_fs_etr), 1, fd);
	return;
}

void add_entry_proxy(FILE* fd, const char* src, uint8_t pid, pkg_toc* fwimg_toc, uint32_t hdr2, uint32_t hdr3, uint32_t hdr4) {
	printf("looking for %s\n", src);
	uint32_t fsize = getSz(src);
	if (!fsize)
		return;

	if (fsize <= FSP_BUF_SZ_BYTES) {
		printf("adding (small)... ");
		add_entry(fd, src, pid, fwimg_toc, hdr4, hdr2, hdr3);
		printf("done\n");
		return;
	}

	printf("found big\n");

	split(src, "_", 16777216);
	
	uint32_t coff = 0, csz = 0;
	uint8_t cur = 0;
	char cfname[32];
	while (1) {
		memset(cfname, 0, 32);
		sprintf(cfname, "%s_%02d", src, cur);
		printf("looking for %s\n", cfname);
		csz = getSz(cfname);
		if (csz == 0)
			break;
		printf("adding (big)... ", cfname);
		add_entry(fd, cfname, pid, fwimg_toc, coff, hdr2, hdr3);
		printf("done\n");
		unlink(cfname);
		coff -= -csz;
		cur -= -1;
	}

	printf("done\n");
	return;
}

void sync_fwimage(const char* imagepath, pkg_toc* fwimg_toc) {
	printf("Syncing fwimage...\n");
	append("fwimage.bin_part", imagepath);
	unlink("fwimage.bin_part");

	int fp = open(imagepath, O_RDWR);
	if (fp < 0)
		return;
	pwrite(fp, fwimg_toc, sizeof(pkg_toc), 0);

	uint32_t act_toc_crc = crc32(0, fwimg_toc, sizeof(pkg_toc));
	printf("TOTOC crc 0x%08X\n", act_toc_crc);

	uint8_t ecount = 0;
	pkg_fs_etr fs_entry;
	uint32_t soff = sizeof(pkg_toc), boff = (sizeof(pkg_toc) + (fwimg_toc->fs_count * sizeof(pkg_fs_etr)));
	while (ecount < fwimg_toc->fs_count) {
		memset(&fs_entry, 0, sizeof(pkg_fs_etr));
		pread(fp, &fs_entry, sizeof(pkg_fs_etr), soff);
		fs_entry.pkg_off = boff;
		pwrite(fp, &fs_entry, sizeof(pkg_fs_etr), soff);
		act_toc_crc = crc32(act_toc_crc, &fs_entry, sizeof(pkg_fs_etr));
		if (fs_entry.type < FSPART_TYPE_DEV) {
			printf("\nFS_PART[%d] - magic 0x%04X | type %d\n"
				" READ: size 0x%X | offset 0x%X | ungzip %d\n"
				" WRITE: size 0x%X | offset 0x%X @ id %d\n"
				" CHECK: hdr2 0x%X | hdr3 0x%X\n"
				" PART_CRC32: 0x%08X\n",
				ecount, fs_entry.magic, fs_entry.type,
				fs_entry.pkg_sz, fs_entry.pkg_off, (fs_entry.pkg_sz < fs_entry.dst_sz),
				fs_entry.dst_sz, fs_entry.dst_off, fs_entry.part_id,
				fs_entry.hdr2, fs_entry.hdr3,
				fs_entry.crc32);
		} else {
			printf("\nFS_PART[%d] - magic 0x%04X | type %d\n"
				" READ: size 0x%X | offset 0x%X | ungzip %d\n"
				" WRITE: size 0x%X | device %d\n"
				" CHECK: hdr2 0x%X | hdr3 0x%X\n"
				" FIRMWARE: 0x%08X\n"
				" PART_CRC32: 0x%08X\n",
				ecount, fs_entry.magic, fs_entry.type,
				fs_entry.pkg_sz, fs_entry.pkg_off, (fs_entry.pkg_sz < fs_entry.dst_sz),
				fs_entry.dst_sz, fs_entry.part_id,
				fs_entry.hdr2, fs_entry.hdr3,
				fs_entry.dst_off,
				fs_entry.crc32);
		}
		soff -= -sizeof(pkg_fs_etr);
		boff -= -fs_entry.pkg_sz;
		ecount -= -1;
	}

	fwimg_toc->toc_crc32 = act_toc_crc;
	pwrite(fp, fwimg_toc, sizeof(pkg_toc), 0);

	close(fp);
	return;
}

void read_image(const char* image) {
	printf("Opening %s\n", image);
	int fp = open(image, O_RDWR);
	if (fp < 0)
		return;
	pkg_toc fwimg_toc;
	pread(fp, &fwimg_toc, sizeof(pkg_toc), 0);
	printf("Image magic: 0x%X\nImage version: 0x%X\n"
		"Firmware version: 0x%08X\nTarget type: %s\n"
		"Minimum firmware: 0x%08X\nMaximum firmware: 0x%08X\n"
		"Hardware rev: 0x%08X with mask: 0x%08X\n"
		"FS_PART count: %d crc32d: 0x%08X\n"
		"Build info: %s\n\n",
		fwimg_toc.magic, fwimg_toc.version,
		fwimg_toc.fw_version, target_dev[fwimg_toc.target],
		fwimg_toc.target_min_fw, fwimg_toc.target_max_fw,
		fwimg_toc.target_hw_rev, fwimg_toc.target_hw_mask,
		fwimg_toc.fs_count, fwimg_toc.toc_crc32,
		fwimg_toc.build_info);
	if (fwimg_toc.force_component_update)
		printf(" - Force components update\n");
	if (fwimg_toc.target_require_enso)
		printf(" - Require a preset enso installation\n");
	if (fwimg_toc.use_file_logging)
		printf(" - Log kernel log output to file\n");
	uint8_t ecount = 0;
	pkg_fs_etr fs_entry;
	uint32_t soff = sizeof(pkg_toc), boff = (sizeof(pkg_toc) + (fwimg_toc.fs_count * sizeof(pkg_fs_etr)));
	while (ecount < fwimg_toc.fs_count) {
		memset(&fs_entry, 0, sizeof(pkg_fs_etr));
		pread(fp, &fs_entry, sizeof(pkg_fs_etr), soff);
		if (fs_entry.type < FSPART_TYPE_DEV) {
			printf("\nFS_PART[%d] - magic 0x%04X | type %d\n"
				" READ: size 0x%X | offset 0x%X | ungzip %d\n"
				" WRITE: size 0x%X | offset 0x%X @ id %d\n"
				" CHECK: hdr2 0x%X | hdr3 0x%X\n"
				" PART_CRC32: 0x%08X\n",
				ecount, fs_entry.magic, fs_entry.type,
				fs_entry.pkg_sz, fs_entry.pkg_off, (fs_entry.pkg_sz < fs_entry.dst_sz),
				fs_entry.dst_sz, fs_entry.dst_off, fs_entry.part_id,
				fs_entry.hdr2, fs_entry.hdr3,
				fs_entry.crc32);
		} else {
			printf("\nFS_PART[%d] - magic 0x%04X | type %d\n"
				" READ: size 0x%X | offset 0x%X | ungzip %d\n"
				" WRITE: size 0x%X | device %d\n"
				" CHECK: hdr2 0x%X | hdr3 0x%X\n"
				" FIRMWARE: 0x%08X\n"
				" PART_CRC32: 0x%08X\n",
				ecount, fs_entry.magic, fs_entry.type,
				fs_entry.pkg_sz, fs_entry.pkg_off, (fs_entry.pkg_sz < fs_entry.dst_sz),
				fs_entry.dst_sz, fs_entry.part_id,
				fs_entry.hdr2, fs_entry.hdr3,
				fs_entry.dst_off,
				fs_entry.crc32);
		}
		soff -= -sizeof(pkg_fs_etr);
		ecount -= -1;
	}
	close(fp);
	return;
}

void fwimg2pup(const char* fwimage, const char* updater, const char *setupper, const char* addcont_all, const char* addcont_vita, const char* addcont_dolce, const char* disclaimer, const char* pup, uint32_t pup_fw) {
	printf("\nGPUP[0x%08X] %s + %s + %s + %s + %s + %s -> %s\n", pup_fw, updater, fwimage, addcont_all, addcont_vita, addcont_dolce, disclaimer, pup);
	
	npup_hdr head;
	memset(&head, 0, sizeof(head));
	head.magic = NPUP_MAGIC;
	head.package_version = NPUP_VERSION;
	head.image_version = 0xF000000F + (pup_fw & 0x0FFFFFF0);
	head.file_count = 8;
	head.header_length = 0x200;
	
	// VERSION STRING (unused)
	strcpy(head.fw_string_block, "FWTOOL-BASED CUSTOM UPDATE PACKAGE FOR PSP2 VITA AND DOLCE\n");
	head.fw_string_block[sizeof("FWTOOL-BASED CUSTOM UPDATE PACKAGE FOR PSP2 VITA AND DOLCE\n")] = 0xA;
	head.version_info.data_length = sizeof("FWTOOL-BASED CUSTOM UPDATE PACKAGE FOR PSP2 VITA AND DOLCE\n") + 1;
	head.version_info.data_offset = 0x200;
	head.version_info.entry_id = 0x100;
	head.version_info.unk_0x18 = 0x2;
	
	// DISCLAIMER INFO
	head.disclaimer_info.data_length = getSz(disclaimer);
	head.disclaimer_info.data_offset = sizeof(head);
	head.disclaimer_info.entry_id = NPUP_PREIMSG_ID;
	head.disclaimer_info.unk_0x18 = NPUP_NUNK;
	
	// UPDATER INFO
	head.updater_info.data_length = getSz(updater);
	head.updater_info.data_offset = sizeof(head) + ALIGN_SECTOR(head.disclaimer_info.data_length);
	head.updater_info.entry_id = 0x200; // psp2swu.self id
	head.updater_info.unk_0x18 = NPUP_NUNK;

	// SETUPPER INFO
	head.setupper_info.data_length = getSz(setupper);
	head.setupper_info.data_offset = head.updater_info.data_offset + ALIGN_SECTOR(head.updater_info.data_length);
	head.setupper_info.entry_id = 0x204; // cui_setupper.self id
	head.setupper_info.unk_0x18 = NPUP_NUNK;

	// FWIMAGE INFO
	head.fwimage_info.data_length = getSz(fwimage);
	head.fwimage_info.data_offset = head.setupper_info.data_offset + ALIGN_SECTOR(head.setupper_info.data_length);
	head.fwimage_info.entry_id = NPUP_FWIMAGE_ID;
	head.fwimage_info.unk_0x18 = NPUP_NUNK;
	
	// ADDCONT INFO (ALL)
	head.addcont_all_info.data_length = getSz(addcont_all);
	head.addcont_all_info.data_offset = head.fwimage_info.data_offset + ALIGN_SECTOR(head.fwimage_info.data_length);
	head.addcont_all_info.entry_id = NPUP_ADDCONT_ALL_ID;
	head.addcont_all_info.unk_0x18 = NPUP_NUNK;

	// ADDCONT INFO (VITA)
	head.addcont_vita_info.data_length = getSz(addcont_vita);
	head.addcont_vita_info.data_offset = head.addcont_all_info.data_offset + ALIGN_SECTOR(head.addcont_all_info.data_length);
	head.addcont_vita_info.entry_id = NPUP_ADDCONT_VITA_ID;
	head.addcont_vita_info.unk_0x18 = NPUP_NUNK;

	// ADDCONT INFO (DOLCE)
	head.addcont_dolce_info.data_length = getSz(addcont_dolce);
	head.addcont_dolce_info.data_offset = head.addcont_vita_info.data_offset + ALIGN_SECTOR(head.addcont_vita_info.data_length);
	head.addcont_dolce_info.entry_id = NPUP_ADDCONT_DOLCE_ID;
	head.addcont_dolce_info.unk_0x18 = NPUP_NUNK;

	head.package_length = head.addcont_dolce_info.data_offset + ALIGN_SECTOR(head.addcont_dolce_info.data_length);

	void* pup_b = malloc(head.package_length);
	memset(pup_b, 0, head.package_length);
	memcpy(pup_b, &head, sizeof(npup_hdr));

	// DISCLAIMER
	FILE* fp = fopen(disclaimer, "rb");
	if (fp) {
		fread(pup_b + head.disclaimer_info.data_offset, head.disclaimer_info.data_length, 1, fp);
		fclose(fp);
	} else
		printf("PUPG: no pupinfo\n");

	// UPDATER
	fp = fopen(updater, "rb");
	if (fp) {
		fread(pup_b + head.updater_info.data_offset, head.updater_info.data_length, 1, fp);
		fclose(fp);
	} else
		printf("PUPG: no updater\n");

	// SETUPPER
	fp = fopen(setupper, "rb");
	if (fp) {
		fread(pup_b + head.setupper_info.data_offset, head.setupper_info.data_length, 1, fp);
		fclose(fp);
	} else
		printf("PUPG: no setupper\n");

	// FWIMAGE
	fp = fopen(fwimage, "rb");
	if (fp) {
		fread(pup_b + head.fwimage_info.data_offset, head.fwimage_info.data_length, 1, fp);
		fclose(fp);
	} else
		printf("PUPG: no fwimage (?)\n");

	// ADDCONT (ALL)
	fp = fopen(addcont_all, "rb");
	if (fp) {
		fread(pup_b + head.addcont_all_info.data_offset, head.addcont_all_info.data_length, 1, fp);
		fclose(fp);
	} else
		printf("PUPG: no patches for all targets\n");

	// ADDCONT (VITA)
	fp = fopen(addcont_vita, "rb");
	if (fp) {
		fread(pup_b + head.addcont_vita_info.data_offset, head.addcont_vita_info.data_length, 1, fp);
		fclose(fp);
	} else
		printf("PUPG: no patches for vita\n");

	// ADDCONT (DOLCE)
	fp = fopen(addcont_dolce, "rb");
	if (fp) {
		fread(pup_b + head.addcont_dolce_info.data_offset, head.addcont_dolce_info.data_length, 1, fp);
		fclose(fp);
	} else
		printf("PUPG: no patches for dolce\n");

	// PUP-GEN
	unlink(pup);
	fp = fopen(pup, "wb");
	fwrite(pup_b, head.package_length, 1, fp);
	fclose(fp);

	printf("\nPUPGEN SZCHK: got 0x%X exp 0x%X\n", getSz(pup), (uint32_t)head.package_length);

	free(pup_b);
}

void parse_list(char* list, char* out) {
	int len = strlen(list);
	for (int i = 0; i < len; i -= -1) {
		if (list[i] > '9') {
			if (list[i] > 'F')
				break;
			out[list[i] - '7'] = 1;
		} else if (list[i] >= '0')
			out[list[i] - '0'] = 1;
	}
}

void print_usage(char *me);

int main(int argc, char* argv[]) {

	if (argc < 3) {
		print_usage(argv[0]);
		return -1;
	}

	uint8_t target_type = FWTARGET_SAFE, force_scu = 0, req_enso = 0, log2file = 0;
	uint32_t fwimg_fw = 0, hw_type = 0, hw_mask = 0, pkg_mask = 0, min_fw = 0, max_fw = 0;
	char* build_info = NULL, * gui_image_list = NULL, * gui_devfw_list = NULL, used_list[SCEMBR_PART_UNUSED + DEV_NODEV + 2];
	int gui = 0, create_pup = 0, gui_use_enso = 0, use_e2x_rconfig = 0, use_e2x_rblob = 0, use_e2x_rmbr = 0;

	memset(used_list, 0, sizeof(used_list));

	// opt
	for (int i = 2; i < argc; i++) {
		printf("argv: %s\n", argv[i]);
		if (!strcmp("-target", argv[i])) {
			i = i + 1;
			if (gui)
				target_type = *(uint8_t *)(argv[i]) - 0x30;
			else {
				for (int t = FWTARGET_EMU; t < (FWTARGET_SAFE + 1); t++) {
					if (!strcmp(target_dev[t], argv[i]))
						target_type = t;
				}
			}
		} else if (!strcmp("-info", argv[i])) {
			read_image(argv[1]);
			if (!gui)
				return 0;
			while (1) {};
		} else if (!strcmp("-eo", argv[i])) {
			fat2e2x();
			if (!gui)
				return 0;
			while (1) {};
		} else if (!strcmp("-gp", argv[i])) {
			i = i + 1;
			if (getSz(argv[1])) {
				fwimg2pup(argv[1], "psp2swu.self", "cui_setupper.self", "patches_all.zip", "patches_vita.zip", "patches_dolce.zip", "pupinfo.txt", "PSP2UPDAT.PUP", (uint32_t)strtoul((argv[i] + 2), NULL, 16));
				while (1) {};
			}
		} else if (!strcmp("-pup", argv[i]))
			create_pup = 1;
		else if (!strcmp("-fw", argv[i])) {
			i = i + 1;
			fwimg_fw = (uint32_t)strtoul((argv[i] + 2), NULL, 16);
		} else if (!strcmp("-msg", argv[i])) {
			i = i + 1;
			build_info = argv[i];
		} else if (!strcmp("-hw", argv[i])) {
			i = i + 1;
			hw_type = (uint32_t)strtoul((argv[i] + 2), NULL, 16);
			i = i + 1;
			hw_mask = (uint32_t)strtoul((argv[i] + 2), NULL, 16);
		} else if (!strcmp("-pkg", argv[i])) {
			i = i + 1;
			pkg_mask = (uint32_t)strtoul((argv[i] + 2), NULL, 16);
		} else if (!strcmp("-gui", argv[i]))
			gui = 1;
		else if (!strcmp("-li", argv[i])) {
			i = i + 1;
			gui_image_list = argv[i];
		} else if (!strcmp("-ld", argv[i])) {
			i = i + 1;
			gui_devfw_list = argv[i];
		} else if (!strcmp("-min_fw", argv[i])) {
			i = i + 1;
			min_fw = (uint32_t)strtoul((argv[i] + 2), NULL, 16);
		} else if (!strcmp("-max_fw", argv[i])) {
			i = i + 1;
			max_fw = (uint32_t)strtoul((argv[i] + 2), NULL, 16);
		} else if (!strcmp("-force_component_update", argv[i]))
			force_scu = 1;
		else if (!strcmp("-require_enso", argv[i]))
			req_enso = 1;
		else if (!strcmp("-use_file_logging", argv[i]))
			log2file = 1;
		else if (!strcmp("-use_e2x_recovery_config", argv[i]))
			use_e2x_rconfig = 1;
		else if (!strcmp("-use_e2x_recovery_blob", argv[i]))
			use_e2x_rblob = 1;
		else if (!strcmp("-use_e2x_recovery_mbr", argv[i]))
			use_e2x_rmbr = 1;
		else if (*(char*)argv[i] == '+') {
			if (!strcmp(argv[i] + 1, "enso"))
				used_list[SCEMBR_PART_EMPTY] = 1;
			for (int p = SCEMBR_PART_SBLS; p < SCEMBR_PART_UNUSED; p++) {
				if (!strcmp(argv[i] + 1, pcode_str[p]))
					used_list[p] = 1;
			}
			for (int d = DEV_SYSCON_FW; d < DEV_NODEV; d++) {
				if (!strcmp(argv[i] + 1, dcode_str[d]))
					used_list[0x10 + d] = 1;
			}
		}
	}

	unlink(argv[1]);

	if (gui) {
		printf("parsing the image lists\n");
		if (!gui_image_list && !gui_devfw_list) {
			printf("no list!\n");
			while (1) {};
		}
		if (gui_image_list)
			parse_list(gui_image_list, used_list);
		if (gui_devfw_list)
			parse_list(gui_devfw_list, &used_list[0x10]);
	}

	printf("opening %s\n", argv[1]);
	FILE* fd = fopen(argv[1], "wb");
	if (!fd) {
		printf("error\n");
		if (!gui)
			return 0;
		while (1) {};
	}

	pkg_toc fwimg_toc;
	memset(&fwimg_toc, 0, sizeof(pkg_toc));
	fwimg_toc.magic = CFWIMG_MAGIC;
	fwimg_toc.version = CFWIMG_VERSION;
	fwimg_toc.target = target_type;
	fwimg_toc.fw_version = fwimg_fw;
	fwimg_toc.target_hw_rev = hw_type;
	fwimg_toc.target_hw_mask = hw_mask;
	fwimg_toc.force_component_update = force_scu;
	fwimg_toc.unused = 0;
	fwimg_toc.target_require_enso = req_enso;
	fwimg_toc.use_file_logging = log2file;
	fwimg_toc.target_min_fw = min_fw;
	fwimg_toc.target_max_fw = max_fw;
	if (build_info)
		snprintf(fwimg_toc.build_info, sizeof(fwimg_toc.build_info), "%s", build_info);
	fwrite(&fwimg_toc, sizeof(pkg_toc), 1, fd);

	char cfname[32], hfname[32];
	// add partitions
	printf("add_images [emmc]\n");
	for (int i = SCEMBR_PART_SBLS; i < SCEMBR_PART_UNUSED; i -= -1) {
		if (used_list[i]) {
			memset(cfname, 0, 32);
			sprintf(cfname, "%s.img", pcode_str[i]);
			add_entry_proxy(fd, cfname, i, &fwimg_toc, pkg_mask, 0, 0);
		}
	}

	// add enso_ex
	if (used_list[0]) {
		printf("add_images [enso_ex]\n");
		if (!fat2e2x())
			add_entry(fd, "slim.e2x", 0, &fwimg_toc, 0x400, fwimg_fw, 0);
	}

	// add enso_ex v5+ recovery configuration/bootstrap
	if (use_e2x_rconfig) {
		printf("add_images [enso_ex recovery configuration]\n");
		add_entry(fd, "rconfig.e2xp", 0, &fwimg_toc, e2x_misc_type_offsets[E2X_MISC_RECOVERY_CONFIG], fwimg_fw, 0);
	}

	// add enso_ex v5+ recovery blob
	if (use_e2x_rblob) {
		printf("add_images [enso_ex recovery blob]\n");
		add_entry(fd, "rblob.e2xp", 0, &fwimg_toc, e2x_misc_type_offsets[E2X_MISC_RECOVERY_BLOB], fwimg_fw, 0);
	}

	// add enso_ex v5+ recovery mbr
	if (use_e2x_rmbr) {
		printf("add_images [enso_ex recovery mbr]\n");
		add_entry(fd, "rmbr.bin", 0, &fwimg_toc, e2x_misc_type_offsets[E2X_MISC_RECOVERY_MBR], fwimg_fw, 0);
	}

	// add devices
	for (int i = DEV_SYSCON_FW; i < DEV_NODEV; i -= -1) {
		if (used_list[i + 0x10]) {
			printf("add_images [%s]\n", dcode_str[i]);
			for (uint8_t z = 0; z < 0xFF; z -= -1) {
				memset(cfname, 0, 32);
				memset(hfname, 0, 32);
				sprintf(cfname, "%s-%02X.bin", dcode_str[i], z);
				sprintf(hfname, "%s-%02X.pkg", dcode_str[i], z);
				if (!getSz(cfname) || getSz(hfname) < 0x480)
					break;
				uint32_t hdr2 = 0, hdr3 = 0, hdr4 = 0;
				int fz = open(hfname, O_RDONLY);
				pread(fz, &hdr2, 4, 0x408);
				pread(fz, &hdr3, 4, 0x40C);
				pread(fz, &hdr4, 4, 0x410);
				close(fz);
				add_entry_proxy(fd, cfname, i + 0x10, &fwimg_toc, hdr2, hdr3, hdr4);
			}
		}
	}

	fclose(fd);

	sync_fwimage(argv[1], &fwimg_toc);
	
	if (create_pup)
		fwimg2pup(argv[1], "psp2swu.self", "cui_setupper.self", "patches_all.zip", "patches_vita.zip", "patches_dolce.zip", "pupinfo.txt", "PSP2UPDAT.PUP", fwimg_fw);

	printf("\nfinished: %s\n", argv[1]);

	if (gui) {
		while (1) {};
	}

	return 0;
}

void print_usage(char* me) {
	printf("\n--------------------------------------\n> mkcfw for %s <\n--------------------------------------\n", FWTOOL_VERSION_STR);
	printf("\nusage: %s [fwimage] [-options] <+images> <+fwupgrades>\n", me);
	printf("\nfwimage options:\n");
	printf("'-info' : display fwimage info\n");
	printf("\n");
	printf("'-fw VERSION' : u32h version displayed in fwtool\n e.g. '-fw 0x03650000'\n default 0x00000000\n");
	printf("\n");
	printf("'-msg INFO' : build info displayed in fwtool\n e.g. '-msg \"my first 3.65 cex repack\"'\n default n/a\n");
	printf("\n");
	printf("'-hw HWINFO MASK' : required target u32h HWINFO, checked against target hwinfo & MASK\n e.g. '-hw 0x00703030 0x00FFFFFF' : pstvs version 1\n default 0 0 : all units/not checked\n");
	printf("\n");
	printf("'-target TARGET' : required target unit type\n 'TARGET' being one of:\n");
	printf("   'TEST' : Emulator\n");
	printf("   'DEVTOOL' : Development Kit\n");
	printf("   'DEX' : Testing Kit\n");
	printf("   'CEX' : Retail unit\n");
	printf("   'UNKQA' : Unknown/FWTOOLQA\n");
	printf("   'ALL' : All units (use with caution!)\n");
	printf("   'NOCHK' : All units, 'safe' - no component/criticalfs updates\n");
	printf(" e.g. '-target CEX' : only retail units can install this image\n default NOCHK\n");
	printf("\n");
	printf("'-min_fw VERSION' : minimum u32h firmware version the image can be installed on\n e.g. '-min_fw 0x03600000' : only consoles running fw 3.60 or higher can install this fwimage\n default 0x00000000 : no lower firmware bound\n");
	printf("\n");
	printf("'-max_fw VERSION' : maximum u32h firmware version the image can be installed on\n e.g. '-min_fw 0x03740000' : only consoles running fw 3.74 or lower can install this fwimage\n default 0x00000000 : no upper firmware bound\n");
	printf("\n");
	printf("'-force_component_update' : ignores the version checks on component upgrades\n");
	printf("\n");
	printf("'-require_enso' : only units with an active enso hack can install this fwimage\n");
	printf("\n");
	printf("'-use_file_logging' : log kernel stdout to %s\n", LOG_LOC);
	printf("\n");
	printf("'-pup' : create a (N)PUP from the resulting fwimage\n additional PUP components:\n");
	printf("   'pupinfo.txt' : disclaimer displayed before PUP install, optional\n");
	printf("   'patches_all.zip' : partition patches for all units, optional\n");
	printf("   'patches_vita.zip' : partition patches for vita units, optional\n");
	printf("   'patches_dolce.zip' : partition patches for pstv units, optional\n");
	printf("   'psp2swu.self' : PUP installer, provided by fwtool, mandatory\n");
	printf("   'cui_setupper.self' : PUP installer (nbh), provided by fwtool, optional\n");
	printf("\n");
	printf("\nadding partition images to fwimage:\n");
	printf("'+enso' : will use enso image @ enso.bin\n");
	for (int i = SCEMBR_PART_SBLS; i < SCEMBR_PART_UNUSED; i++)
		printf("'+%s' : will use %s image @ %s.img\n", pcode_str[i], pcode_str[i], pcode_str[i]);
	printf("\n");
	printf("\nadding component upgrades to fwimage (XX is a 2-digit ID starting at 00):\n");
	for (int i = DEV_SYSCON_FW; i < DEV_NODEV; i++) {
		if (i == DEV_RESERVED)
			i++;
		printf("'+%s' : will use raw %s upgrades @ %s-XX.bin and spkgs @ %s-XX.pkg\n", dcode_str[i], dcode_str[i], dcode_str[i], dcode_str[i]);
	}
	printf("\n");
	printf("\nnotes:\n");
	printf(" - make sure that enso version matches the slb2's version\n");
	printf("\n");
	printf("\nexample case 1 - All-in-one 3.65 CFW NPUP:\n");
	printf("setup:\n");
	printf(" - enso_ex v5 for 3.65 as enso.bin\n");
	printf(" - slb2 extracted from a 3.65 PUP as slb2.img\n");
	printf(" - os0 extracted from a 3.65 PUP as os0.img\n");
	printf(" - vs0 extracted from a 3.65 PUP as vs0.img\n");
	printf(" - psp2swu.self provided by fwtool\n");
	printf(" - pupinfo.txt containing some disclaimer and author info\n");
	printf(" - patches_all.zip containing:\n   - os0-patch/ with the enso_ex os0 modules\n   - ur0-patch/ with taihen and henkaku\n");
	printf(" - patches_vita.zip containing:\n   - vs0-patch/ with the extracted vs0 vita tarpatch\n   - ur0-patch/ with vita-specific taihen config and plugins\n");
	printf(" - patches_dolce.zip containing:\n   - vs0-patch/ with the extracted vs0 pstv tarpatch\n   - ur0-patch/ with pstv-specific taihen config and plugins\n");
	printf("command:\n %s psp2cfw -fw 0x03650000 -msg \"AIO 3.65 CFW setup sample\" -target CEX -pup +enso +slb2 +os0 +vs0\n", me);
	printf("result: a PUP that can be installed with any modoru version on retail PS Vita/TV units.\n");
	printf("\nexample case 2 - Syscon CFW collection NPUP:\n");
	printf("setup:\n");
	printf(" - slim syscon cfw as syscon_fw-00.bin and its PUP spkg as syscon_fw-00.pkg\n");
	printf(" - phat syscon cfw as syscon_fw-01.bin and its PUP spkg as syscon_fw-01.pkg\n");
	printf(" - pstv syscon cfw as syscon_fw-02.bin and its PUP spkg as syscon_fw-02.pkg\n");
	printf(" - devkit syscon cfw as syscon_fw-03.bin and its PUP spkg as syscon_fw-03.pkg\n");
	printf(" - psp2swu.self and cui_setupper.self provided by fwtool\n");
	printf(" - pupinfo.txt containing some disclaimer and author info\n");
	printf("command:\n %s psp2cfw -fw 0x03650000 -msg \"ernie cfw sample\" -target ALL -pup -force_component_update -require_enso -min_fw 0x03600000 +syscon_fw\n", me);
	printf("result: a 3.60+ PUP requiring enso that can be installed with any modoru version as well as neighbourhood.\n");
	printf("\nexample case 3 - DevKit 3.65 full->cexfortool via fwimage:\n");
	printf("setup:\n");
	printf(" - vs0 extracted from a 3.65 CFT PUP as vs0.img\n");
	printf("command:\n %s psp2cfw -fw 0x03650000 -msg \"3.65 cexfortool vs0\" -target NOCHK -hw 0x00416000 0x00FFFF00 -min_fw 0x03650000 -max_fw 0x03650011 +vs0\n", me);
	printf("result: a 3.65-only fwimage requiring devkit hwinfo, installable with the fwtool app\n");
	printf("\n");
}