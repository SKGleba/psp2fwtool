# psp2fwtool
Firmware manager for Playstation Vita/TV

## Usage
### Firmware image
"Firmware Image" is a container for EMMC partition images as well as the enso exploit.

### Creating a firmware image
0. Boot up your linux environment, make sure that you have gzip.
1. Clone this repository to your local computer and 'make fwtool' in 'psp2fwtool/create/'.
2. Copy partition images to the /create/ directory with name '[partition name].bin'.
 - enso should be called 'fat.bin' or 'e2x.bin' depending on the enso version you are using.
3. Run '. fwtool [path] -target [type]', the tool will create a firmware image in [path].
 - [type] is device target id in decimal:
  - 0-5: internal, devtool, testkit, retail, QA, all
  - 6: all targets but without SNVS/MBR update (used for "soft" images).
4. Run '. fwtool [path] -info' and make sure that everything is as expected.

### Installing the firmware image
0. Make sure that you have 'Unsafe homebrew' enabled and no game card inserted.
1. Put the firmware image in 'ux0:data/fwtool/fwimage.bin'.
2. The installer can update partitions with files found in 'ux0:data/fwtool/[part]-patch/'.
 - supported [part]itions are: os0, vs0, ur0; ex: 'os0-patch/'.
 - the installer will copy contents of the /[part]-patch/ after flashing the fwimage.
3. Open fwtool and select 'Flash a firmware image' then press [start] when asked.
4. Wait until the installer finishes with 'ALL DONE' or an error.
5. Press [circle] to reboot the device.

### Restore point
 - "Restore point" is a partial or full EMMC image, it is per-console.
 - The image can be restored at any time and on any supported firmware.
 - The image size is around 4GB for full and 1GB for partial (without ur0/ux0).
 - To create/restore it use the fwtool app:
  - 'Create a EMMC image' will create a restore point in 'ux0:data/fwtool/fwrpoint.bin'
  - 'Restore the EMMC image' will flash the restore point in 'ux0:data/fwtool/fwrpoint.bin'


