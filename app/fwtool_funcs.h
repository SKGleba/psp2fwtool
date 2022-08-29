/* THIS FILE IS A PART OF PSP2FWTOOL
 *
 * Copyright (C) 2019-2021 skgleba
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

extern int fwtool_read_fwimage(uint32_t offset, uint32_t size, uint32_t crc32, uint32_t unzip);
extern int fwtool_write_partition(uint32_t offset, uint32_t size, uint8_t partition);
extern int fwtool_personalize_bl(int fup);
extern int fwtool_update_mbr(int use_e2x, int swap_bl, int swap_os);
extern int fwtool_flash_e2x(uint32_t size);
extern int fwtool_unlink(void);
extern int fwtool_talku(int cmd, int cmdbuf);
extern int fwtool_rw_emmcimg(int dump);
extern int fwtool_dualos_create(void);
extern int fwtool_dualos_swap(void);
extern int fwtool_update_dev(int id, uint32_t size, uint32_t hdr2, uint32_t hdr3, uint32_t hdr4);
extern int fwtool_check_rvk(int type, int id, uint32_t hdr2, uint32_t hdr3);