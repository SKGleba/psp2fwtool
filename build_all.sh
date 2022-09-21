(
# PC tools
mkdir create;
cd tools/;
rm ../create/mkcfw-wingui.ps1 && cp mkcfw-wingui.ps1 ../create/mkcfw-wingui.ps1;
gcc mkcfw.c -Wno-multichar -o ../create/mkcfw;
gcc mksbls.c -o ../create/mksbls;
gcc mkernie.c -o ../create/mkernie;
gcc mkmbr.c -o ../create/mkmbr;
gcc fstool.c -o ../create/fstool;

# Custom imports because VitaSDK doesn't understand backward compat
cd ../;
vita-libs-gen imports.yml imports;
cd imports/;
make;
for lib in *_stub.a; do
    mv -- "$lib" "${lib%_stub.a}_custom.a"
done

# Compile main
cd ../;
cmake ./ && make;

# Copy updater
cp compile_psp2swu.self psp2swu.self;
cp compile_psp2swu.self create/psp2swu.self;
cp compile_cui_setupper.self cui_setupper.self;
cp compile_cui_setupper.self create/cui_setupper.self;

# Cleanup
rm -rf CMakeFiles && rm cmake_install.cmake && rm CMakeCache.txt && rm Makefile;
rm -rf fwtoolkernel_stubs && rm fwtoolkernel_stubs.yml && rm compile_*;
rm -rf imports;

# Info user
echo "";
echo "VPK: FWTOOL.vpk";
echo "SWU: psp2swu.self";
echo "SUP: cui_setupper.self";
echo "BIN: /create/";
echo "";
echo "DONE! [ VPK | SWU | SUP | BIN ]";
echo "";
)