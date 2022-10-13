cmake_minimum_required(VERSION 2.8)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(FWTOOL)
set(TITLE_ID "SKGFWT00L")
set(TITLE_NAME "FWTOOL@PSP2")

include("${VITASDK}/share/vita.cmake" REQUIRED)

enable_language(C ASM)

set(CMAKE_C_FLAGS "-Wno-multichar -Wl,-q -Wall -O3 -std=gnu99")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++11 -fno-rtti -fno-exceptions")


#-- MAIN KERNEL MODULE
link_directories(
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_BINARY_DIR}/imports
)

add_executable(compile_fwtool.elf
	kernel/fwtool.c
)

target_link_libraries(compile_fwtool.elf
	SceThreadmgrForDriver_custom
	SceSysconForDriver_custom
	SceIofilemgrForDriver_custom
	SceSdifForDriver_custom
	SceModulemgrForDriver_custom
	ScePowerForDriver_custom
	SceKernelUtilsForDriver_custom
	SceDebugForDriver_custom
	SceSblAIMgrForDriver_custom
	SceSysclibForDriver_custom
	SceSysmemForDriver_custom
	SceSysrootForKernel_custom
	SceSysrootForDriver_custom
)

set_target_properties(compile_fwtool.elf
	PROPERTIES LINK_FLAGS "-nostdlib"
	COMPILE_FLAGS "-D__VITA_KERNEL__"
)

add_custom_target(compile_fwtool.skprx ALL
	COMMAND vita-elf-create -e ${CMAKE_SOURCE_DIR}/kernel/fwtool.yml compile_fwtool.elf compile_fwtool.velf
	COMMAND vita-elf-export k compile_fwtool.elf ${CMAKE_SOURCE_DIR}/kernel/fwtool.yml ${CMAKE_CURRENT_BINARY_DIR}/fwtoolkernel_stubs.yml
	COMMAND vita-libs-gen ${CMAKE_CURRENT_BINARY_DIR}/fwtoolkernel_stubs.yml ${CMAKE_CURRENT_BINARY_DIR}/fwtoolkernel_stubs
	COMMAND make -C ${CMAKE_CURRENT_BINARY_DIR}/fwtoolkernel_stubs
	COMMAND vita-make-fself -c compile_fwtool.velf compile_fwtool.skprx
)

add_dependencies(compile_fwtool.skprx compile_fwtool.elf)


#-- MAIN KERNEL MODULE (incl)
add_custom_target(compile_fwtool.h ALL
	COMMAND xxd -i compile_fwtool.skprx > compile_fwtool.h
)
add_dependencies(compile_fwtool.h compile_fwtool.skprx)


#-- PSP2SWU REPLACEMENT
link_directories(
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_BINARY_DIR}/fwtoolkernel_stubs
)

add_executable(compile_swu.elf
	updater/npup_installer.c
	updater/Archives.c
	updater/debugScreenFont.c
)

target_link_libraries(compile_swu.elf
	taihen_stub
	SceDisplay_stub
	SceCtrl_stub
	fwtoolkernel_stub
	SceAppMgr_stub
	ScePower_stub
	SceVshBridge_stub
	z
)

add_custom_target(compile_swu.velf ALL
	COMMAND vita-elf-create compile_swu.elf compile_swu.velf
)

add_custom_target(compile_psp2swu.self ALL
	COMMAND vita-make-fself -c compile_swu.velf compile_psp2swu.self
)

add_dependencies(compile_swu.elf compile_fwtool.h)
add_dependencies(compile_swu.velf compile_swu.elf)
add_dependencies(compile_psp2swu.self compile_swu.velf)


#-- PSP2SWU LAUNCHER FOR CUI SETUPPER (kernel)
add_executable(compile_swu_launch.elf
	updater/swu_launch.c
)

target_link_libraries(compile_swu_launch.elf
  SceAppMgrForDriver_stub
  SceThreadmgrForDriver_stub
  SceSysclibForDriver_stub
)

set_target_properties(compile_swu_launch.elf
	PROPERTIES LINK_FLAGS "-nostdlib"
	COMPILE_FLAGS "-D__VITA_KERNEL__"
)

add_custom_target(compile_swu_launch.velf ALL
	COMMAND vita-elf-create -e ${CMAKE_SOURCE_DIR}/updater/swu_launch.yml compile_swu_launch.elf compile_swu_launch.velf
)

add_custom_target(compile_swu_launch.skprx ALL
	COMMAND vita-make-fself -c compile_swu_launch.velf compile_swu_launch.skprx
)

add_dependencies(compile_swu_launch.velf compile_swu_launch.elf)
add_dependencies(compile_swu_launch.skprx compile_swu_launch.velf)


#-- PSP2SWU LAUNCHER FOR CUI SETUPPER (kernel,incl)
add_custom_target(compile_swu_launch.h ALL
	COMMAND xxd -i compile_swu_launch.skprx > compile_swu_launch.h
)
add_dependencies(compile_swu_launch.h compile_swu_launch.skprx)


#-- CUI SETUPPER REPLACEMENT
add_executable(compile_sup.elf
	updater/npup_setupper.c
)

target_link_libraries(compile_sup.elf
	taihen_stub
	SceDisplay_stub
	SceCtrl_stub
	SceAppMgr_stub
	ScePower_stub
	SceVshBridge_stub
)

add_custom_target(compile_sup.velf ALL
	COMMAND vita-elf-create compile_sup.elf compile_sup.velf
)

add_custom_target(compile_cui_setupper.self ALL
	COMMAND vita-make-fself -c compile_sup.velf compile_cui_setupper.self
)

add_dependencies(compile_sup.elf compile_swu_launch.h)
add_dependencies(compile_sup.velf compile_sup.elf)
add_dependencies(compile_cui_setupper.self compile_sup.velf)


#-- MAIN APP EXECUTABLE
add_executable(compile_app.elf
	app/main.c
	app/debugScreenFont.c
)

target_link_libraries(compile_app.elf
	taihen_stub
	SceDisplay_stub
	SceCtrl_stub
	fwtoolkernel_stub
	SceAppMgr_stub
	ScePower_stub 
	SceVshBridge_stub
)

add_custom_target(compile_app.velf ALL
	COMMAND vita-elf-create compile_app.elf compile_app.velf
)

add_custom_target(compile_eboot.bin ALL
	COMMAND vita-make-fself -a 0x2808000000000000 -c compile_app.velf compile_eboot.bin
)

add_dependencies(compile_app.elf compile_fwtool.skprx)
add_dependencies(compile_app.velf compile_app.elf)
add_dependencies(compile_eboot.bin compile_app.velf)


#-- APP PACKAGE
add_custom_target(${PROJECT_NAME}.vpk ALL
	COMMAND vita-mksfoex -s TITLE_ID=${TITLE_ID} "${TITLE_NAME}" compile_param.sfo
	COMMAND vita-pack-vpk -s compile_param.sfo -b compile_eboot.bin
	-a compile_fwtool.skprx=fwtool.skprx
	-a res/icon0.png=sce_sys/icon0.png ${PROJECT_NAME}.vpk
	-a res/template.xml=sce_sys/livearea/contents/template.xml
	-a res/bg.png=sce_sys/livearea/contents/bg.png
	-a res/startup.png=sce_sys/livearea/contents/startup.png
)

add_dependencies(${PROJECT_NAME}.vpk compile_eboot.bin compile_fwtool.skprx)