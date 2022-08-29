(
mkdir create;
cd tools/;
rm ../create/mkcfw-wingui.ps1 && cp mkcfw-wingui.ps1 ../create/mkcfw-wingui.ps1;
gcc mkcfw.c -Wno-multichar -o ../create/mkcfw;
gcc mksbls.c -o ../create/mksbls;
gcc mkernie.c -o ../create/mkernie;
gcc mkmbr.c -o ../create/mkmbr;
gcc fstool.c -o ../create/fstool;
cd ../;
cmake ./ && make;
cp compile_psp2swu.self psp2swu.self
cp compile_psp2swu.self create/psp2swu.self
rm -rf CMakeFiles && rm cmake_install.cmake && rm CMakeCache.txt && rm Makefile;
rm -rf fwtoolkernel_stubs && rm fwtoolkernel_stubs.yml && rm compile_*;
echo "";
echo "VPK: FWTOOL.vpk";
echo "SWU: psp2swu.self";
echo "BIN: /create/";
echo "";
echo "DONE! [ VPK | SWU | BIN ]";
echo "";
)