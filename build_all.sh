(
cd fwimager/;
make fwtool
cd ../;
cmake ./ && make;
rm -rf CMakeFiles && rm cmake_install.cmake && rm CMakeCache.txt && rm Makefile;
rm -rf fwtoolkernel_stubs && rm fwtoolkernel_stubs.yml && rm compile_*;
echo "";
echo "DONE! [ FWTOOL.vpk | FWIMAGER ]";
echo "";
)