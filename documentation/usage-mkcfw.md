# usage
### mkcfw [fwimage] [-options] <+images> <+fwupgrades>

## fwimage options
 - `-info` : display fwimage info
 - `-fw VERSION` : u32h version displayed in fwtool
   - e.g. `-fw 0x03650000`
   - default : 0x00000000
 - `-msg INFO` : build info displayed in fwtool
   - e.g. `-msg "my first 3.65 cex repack"`
   - default `n/a`
 - `-hw HWINFO MASK` : required target u32h HWINFO, checked against target hwinfo & MASK
   - e.g. `-hw 0x00703030 0x00FFFFFF` : pstvs version 1
   - default 0 0 : all units/not checked
 - `-target TARGET` : required target unit type
   - `TARGET` being one of:
     - `TEST` : Emulator
     - `DEVTOOL` : Development Kit
     - `DEX` : Testing Kit
     - `CEX` : Retail unit
     - `UNKQA` : Unknown/FWTOOLQA
     - `ALL` : All units (use with caution!)
     - `NOCHK` : All units, 'safe' - no component/criticalfs updates
   - e.g. `-target CEX` : only retail units can install this image
   - default `NOCHK` : contains non-critical fs, installable anywhere
 - `-min_fw VERSION` : minimum u32h firmware version the image can be installed on
   - e.g. `-min_fw 0x03600000` : only consoles running fw 3.60 or higher can install this fwimage
   - default 0x00000000 : no lower firmware bound
 - `-max_fw VERSION` : maximum u32h firmware version the image can be installed on
   - e.g. `-min_fw 0x03740000` : only consoles running fw 3.74 or lower can install this fwimage
   - default 0x00000000 : no upper firmware bound
 - `-force_component_update` : ignores the version checks on component upgrades
 - `-require_enso` : only units with an active enso hack can install this fwimage
 - `-use_file_logging` : log kernel stdout to ux0:data/fwtool.log
 - `-pup` : create a (N)PUP from the resulting fwimage
   - additional PUP components:
     - *pupinfo.txt* : disclaimer displayed before PUP install, optional
     - *patches_all.zip* : partition patches for all units, optional
     - *patches_vita.zip* : partition patches for vita units, optional
     - *patches_dolce.zip* : partition patches for pstv units, optional
     - *psp2swu.self* : PUP installer, provided by fwtool, mandatory
     - *cui_setupper.self* : PUP installer (nbh), provided by fwtool, optional

<br/>

## adding partition images to fwimage:
 - `+enso` : will use enso image @ enso.bin
 - `+slb2` : will use slb2 image @ slb2.img
 - `+os0` : will use os0 image @ os0.img
 - `+vs0` : will use vs0 image @ vs0.img
 - `+vd0` : will use vd0 image @ vd0.img
 - `+tm0` : will use tm0 image @ tm0.img
 - `+ur0` : will use ur0 image @ ur0.img
 - `+ux0` : will use ux0 image @ ux0.img
 - `+gro0` : will use gro0 image @ gro0.img
 - `+grw0` : will use grw0 image @ grw0.img
 - `+ud0` : will use ud0 image @ ud0.img
 - `+sa0` : will use sa0 image @ sa0.img
 - `+mediaid` : will use mediaid image @ mediaid.img
 - `+pd0` : will use pd0 image @ pd0.img

### notes:
 - make sure that enso version matches the slb2's version

<br/>

## adding component upgrades to fwimage (XX is a 2-digit ID starting at 00):
 - `+syscon_fw` : will use raw syscon_fw upgrades @ syscon_fw-XX.bin and spkgs @ syscon_fw-XX.pkg
 - `+syscon_cpmgr` : will use raw syscon_cpmgr upgrades @ syscon_cpmgr-XX.bin and spkgs @ syscon_cpmgr-XX.pkg
 - `+syscon_dl` : will use raw syscon_dl upgrades @ syscon_dl-XX.bin and spkgs @ syscon_dl-XX.pkg
 - `+motion0` : will use raw motion0 upgrades @ motion0-XX.bin and spkgs @ motion0-XX.pkg
 - `+motion1` : will use raw motion1 upgrades @ motion1-XX.bin and spkgs @ motion1-XX.pkg
 - `+cp` : will use raw cp upgrades @ cp-XX.bin and spkgs @ cp-XX.pkg
 - `+bic_fw` : will use raw bic_fw upgrades @ bic_fw-XX.bin and spkgs @ bic_fw-XX.pkg
 - `+bic_df` : will use raw bic_df upgrades @ bic_df-XX.bin and spkgs @ bic_df-XX.pkg
 - `+touch_fw` : will use raw touch_fw upgrades @ touch_fw-XX.bin and spkgs @ touch_fw-XX.pkg
 - `+touch_cfg` : will use raw touch_cfg upgrades @ touch_cfg-XX.bin and spkgs @ touch_cfg-XX.pkg
 - `+com` : will use raw com upgrades @ com-XX.bin and spkgs @ com-XX.pkg

<br/>

## example case 1 - All-in-one 3.65 CFW NPUP
### setup
 - enso_ex v5 for 3.65 as enso.bin
 - slb2 extracted from a 3.65 PUP as slb2.img
 - os0 extracted from a 3.65 PUP as os0.img
 - vs0 extracted from a 3.65 PUP as vs0.img
 - psp2swu.self provided by fwtool
 - pupinfo.txt containing some disclaimer and author info
 - patches_all.zip containing:
   - os0-patch/ with the enso_ex os0 modules
   - ur0-patch/ with taihen and henkaku
 - patches_vita.zip containing:
   - vs0-patch/ with the extracted vs0 vita tarpatch
   - ur0-patch/ with vita-specific taihen config and plugins
 - patches_dolce.zip containing:
   - vs0-patch/ with the extracted vs0 pstv tarpatch
   - ur0-patch/ with pstv-specific taihen config and plugins
### command
 - mkcfw psp2cfw -fw 0x03650000 -msg "AIO 3.65 CFW setup sample" -target CEX -pup +enso +slb2 +os0 +vs0
### result
 - a PUP that can be installed with any modoru version on retail PS Vita/TV units.

<br/>

## example case 2 - Syscon CFW collection NPUP
### setup
 - slim syscon cfw as syscon_fw-00.bin and its PUP spkg as syscon_fw-00.pkg
 - phat syscon cfw as syscon_fw-01.bin and its PUP spkg as syscon_fw-01.pkg
 - pstv syscon cfw as syscon_fw-02.bin and its PUP spkg as syscon_fw-02.pkg
 - devkit syscon cfw as syscon_fw-03.bin and its PUP spkg as syscon_fw-03.pkg
 - psp2swu.self and cui_setupper.self provided by fwtool
 - pupinfo.txt containing some disclaimer and author info
### command
 - mkcfw psp2cfw -fw 0x03650000 -msg "ernie cfw sample" -target ALL -pup -force_component_update -require_enso -min_fw 0x03600000 +syscon_fw
### result
 - a 3.60+ PUP requiring enso that can be installed with any modoru version as well as neighbourhood.

<br/>

## example case 3 - DevKit 3.65 full->cexfortool via fwimage
### setup
 - vs0 extracted from a 3.65 CFT PUP as vs0.img
### command
 - mkcfw psp2cfw -fw 0x03650000 -msg "3.65 cexfortool vs0" -target NOCHK -hw 0x00416000 0x00FFFF00 -min_fw 0x03650000 -max_fw 0x03650011 +vs0
### result 
 - a 3.65-only fwimage requiring devkit hwinfo, installable with the fwtool app
