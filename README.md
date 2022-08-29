# psp2fwtool
Firmware manager for Playstation Vita/TV

## Firmware image
"Firmware Image" is a container for EMMC partition images, device firmware updates as well as the enso exploit.
### NPUP
"NPUP" is a PUP-compliant package containing the Firmware Image, a standalone Firmware Image installer (psp2swu.self) as well as additional partition patches.

### Creating a firmware image (Windows + WSL)
0. Make sure that you have gzip installed in your WSL setup.
1. Clone this repository to your local computer and run '. build_all.sh' in WSL.
2. Copy all update components the the /create/ directory in a proper format.
 - enso as enso.bin
 - [partition] as [partition].img
 - decrypted [device] update as [device]-XX.bin, encrypted as [device]-XX.pkg
3. Run the 'mkcfw_wingui.ps1' powershell script in the /create/ directory
4. Select all update components, set Firmware Image info and hit "CREATE"

### Installing the firmware image
0. Make sure that you have 'Unsafe homebrew' enabled and no game card inserted.
1. Put the firmware image in 'ux0:data/fwtool/' as 'psp2cfw'.
2. The installer can update partitions with files found in 'ux0:data/fwtool/[part]-patch/'.
 - supported [part]itions are: os0, vs0, ur0; ex: 'os0-patch/'.
 - the installer will copy contents of the /[part]-patch/ after flashing the fwimage.
3. Open fwtool and select 'Flash a firmware image' then press [start] when asked.
4. Wait until the installer finishes with 'ALL DONE' or an error.
5. Press [circle] to reboot the device.

### Installing a NPUP package
 - Install with any modoru version as you would a normal pup

## dualOS
 - "dualOS" splits the EMMC in half and adds another OS install - "slaveOS".
 - "masterOS" and "slaveOS" are entirely separate, they can be upgraded/downgraded at will
 - All partitions are separate, that includes ur0, idstorage, enso etc
 - Switching between masterOS and slaveOS is very quick and painless
 - To install dualOS use the "Install dualOS" option in fwtool installer
 - With dualOS installed there now should be an option to switch between masterOS and slaveOS
 - Uninstalling dualOS is not recommended but can be done from masterOS via the SELECT menu.

## Restore point
 - "Restore point" is a partial or full EMMC image, it is per-console.
 - The image can be restored at any time and on any supported firmware.
 - The image size is around 4GB for full and 1GB for partial (without ur0/ux0).
 - To create/restore it use the fwtool app:
   - 'Create a EMMC image' will create a restore point in 'ux0:data/fwtool/fwrpoint.bin'
   - 'Restore the EMMC image' will flash the restore point from 'ux0:data/fwtool/fwrpoint.bin'

## Additional PC Tools
 - fstool is a filesystem tool that supports various operations on SCE formatted devices/dumps
 - mkernie is a syscon update repacker/decryptor/encryptor/cfw-maker
 - mksbls is a sbls manager (read, edit, create)
 - mkmbr is a mbr manager (read, edit, create)

## Notes
 - This tool was written for firmwares 3.60-3.74, all device types.
 - A list of known prebuilt firmware images can be found in nfo.txt
 - fwtool has been extensively tested over the past three years, it should not cause any bricks
   - It is recommended to install images that include enso_ex v4+ for extra safety.

