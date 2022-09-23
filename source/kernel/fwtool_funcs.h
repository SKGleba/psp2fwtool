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
extern int fwtool_update_dev(int id, uint32_t size, uint32_t u_hdr_data[3]);
extern int fwtool_check_rvk(int type, int id, uint32_t hdr2, uint32_t hdr3);

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
    CMD_REBOOT,
    CMD_GET_CURRENT_FWV,
    CMD_SET_TSMP_FWV
};