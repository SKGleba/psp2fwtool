# psp2fwtool
Firmware manager for Playstation Vita/TV

This tool sets version to 0xDEADBEEF which skips bootloader checks.
  - The stage2loader sets its version in keyslots.

## WARNING 1: DO NOT USE THE BL-PM-SKIP
It will set a flag in snvs that CANNOT be unset.

## WARNING 2: AFTER FIRMWARE UPDATE THE BL-SKIP FLAG IS RESET
So you need to rerun the tool.

## WARNING 3: THIS TOOL MAY BRICK YOUR VITA, USE WITH HARDMOD
Or just the syscon thingy, i need to implement proper IMG checks.

### Waiting for 3.74 be like...
