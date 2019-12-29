cmake_minimum_required(VERSION 2.8)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(BRICTOOL)
set(TITLE_ID "SKG8R1CC0")
set(TITLE_NAME "brictool")

include("${VITASDK}/share/vita.cmake" REQUIRED)

enable_language(C ASM)

set(CMAKE_C_FLAGS "-Wl,-q -Wall -O3 -std=gnu99")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++11 -fno-rtti -fno-exceptions")

include_directories(
)

link_directories(
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_BINARY_DIR}/brickernel_stubs
)

add_executable(app.elf
	app/main.c
	app/debugScreenFont.c
)

target_link_libraries(app.elf
	taihen_stub
	SceDisplay_stub
	SceCtrl_stub
	brickernel_stub
	SceAppMgr_stub
	ScePower_stub 
)

add_custom_target(app.velf ALL
	COMMAND vita-elf-create app.elf app.velf
)

add_custom_target(eboot.bin ALL
	COMMAND vita-make-fself -c app.velf eboot.bin
)

add_custom_target(${PROJECT_NAME}.vpk ALL
	COMMAND vita-mksfoex -s TITLE_ID=${TITLE_ID} "${TITLE_NAME}" param.sfo
	COMMAND vita-pack-vpk -s param.sfo -b eboot.bin -a bricc.skprx=bricc.skprx 
	-a res/icon0.png=sce_sys/icon0.png ${PROJECT_NAME}.vpk
)

add_dependencies(app.velf app.elf)
add_dependencies(eboot.bin app.velf)
add_dependencies(${PROJECT_NAME}.vpk eboot.bin)

vita_create_stubs(brickernel_stubs bricc.elf ${CMAKE_SOURCE_DIR}/plugin/bricc.yml KERNEL)

add_executable(bricc.elf
	plugin/bricc.c
)

target_link_libraries(bricc.elf
	SceSblSmCommForKernel_stub
	SceThreadmgrForDriver_stub
	SceSysconForDriver_stub
	SceIofilemgrForDriver_stub
	SceSdifForDriver_stub
	SceSysclibForDriver_stub
	SceCpuForDriver_stub
	taihenModuleUtils_stub
	taihenForKernel_stub
	ScePervasiveForDriver_stub
	SceSysmemForDriver_stub
	SceSblSsMgrForKernel_stub
	SceSysrootForDriver_stub
	SceDisplayForDriver_stub
	SceKernelUtilsForDriver_stub
	ScePowerForDriver_stub
	SceSysrootForKernel_stub
)

set_target_properties(bricc.elf
	PROPERTIES LINK_FLAGS "-nostdlib"
	COMPILE_FLAGS "-D__VITA_KERNEL__"
)

add_custom_target(bricc.skprx ALL
	COMMAND vita-elf-create -e ${CMAKE_SOURCE_DIR}/plugin/bricc.yml bricc.elf bricc.velf
	COMMAND vita-make-fself -c bricc.velf bricc.skprx
)
add_dependencies(bricc.skprx bricc.elf)