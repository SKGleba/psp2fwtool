(
rm -rf build;
mkdir build;

# PC tools
cd source/tools/;
cp mkcfw-wingui.ps1 ../../build/mkcfw-wingui.ps1;
gcc mkcfw.c -Wno-multichar -o ../../build/mkcfw;
x86_64-w64-mingw32-gcc-win32 -mno-ms-bitfields -Wno-multichar -DWINDOWS mkcfw.c -o ../../build/mkcfw.exe
gcc mksbls.c -o ../../build/mksbls;
x86_64-w64-mingw32-gcc-win32 -mno-ms-bitfields -DWINDOWS mksbls.c -o ../../build/mksbls.exe
gcc mkernie.c -o ../../build/mkernie;
x86_64-w64-mingw32-gcc-win32 -mno-ms-bitfields -DWINDOWS mkernie.c -o ../../build/mkernie.exe
gcc mkmbr.c -o ../../build/mkmbr;
x86_64-w64-mingw32-gcc-win32 -mno-ms-bitfields -DWINDOWS mkmbr.c -o ../../build/mkmbr.exe
gcc mkfs.c -o ../../build/mkfs;
x86_64-w64-mingw32-gcc-win32 -mno-ms-bitfields -DWINDOWS mkfs.c -o ../../build/mkfs.exe
cd ../../;

# Custom imports because VitaSDK doesn't understand backward compat
cd source/;
rm -rf imports;
vita-libs-gen kernel/imports.yml imports;
cd imports/;
make;
for lib in *_stub.a; do
    mv -- "$lib" "${lib%_stub.a}_custom.a"
done
cd ../../;

# Compile main
cd source/;
cmake ./ && make;
cd ../;

# Copyout builds
mv source/FWTOOL.vpk build/FWTOOL.vpk;
mv source/compile_psp2swu.self build/psp2swu.self;
mv source/compile_cui_setupper.self build/cui_setupper.self;

# Create a separate dir for repacks
mkdir create;
cp build/mk* create/;
cp build/psp2swu.self create/psp2swu.self;
cp build/cui_setupper.self create/cui_setupper.self;

# Cleanup
cd source/;
rm -rf CMakeFiles && rm cmake_install.cmake && rm CMakeCache.txt && rm Makefile;
rm -rf fwtoolkernel_stubs && rm fwtoolkernel_stubs.yml && rm compile_*;
rm -rf imports;
cd ../;

# Info user
echo "";
echo "VPK: /build/FWTOOL.vpk";
echo "SWU: /build/psp2swu.self";
echo "SUP: /build/cui_setupper.self";
echo "BIN: /build/ && /create/";
echo "";
echo "DONE! [ VPK | SWU | SUP | BIN ]";
echo "";
)