cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
endif()
project(vitaos_pause C)
include("${VITASDK}/share/vita.cmake" REQUIRED)

# The VitaOS pause menu. ATTRIBUTE 0x0E + MEMSIZE 0x12800 make it a system-mode
# app (the shoui520/VitaShell-sysapp settings), so it opens beside a suspended
# game instead of closing it. That budget is ~74 MB for everything.
set(VITA_APP_NAME "VitaOS Menu")
set(VITA_TITLEID "VTOSPAUSE")
set(VITA_VERSION "01.00")
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d PARENTAL_LEVEL=1 -s CATEGORY=gdc -d ATTRIBUTE=0x1089088 -d ATTRIBUTE2=0")
# As VitaShell-sysapp: a system authority ID (without it the shell treated the
# menu as a normal app and asked to close the game, 2026-09-27), and -pm carves
# 26 MiB PHYCONT out of the 74 MiB budget (48 MiB USER_RW left).
set(VITA_MAKE_FSELF_FLAGS "${VITA_MAKE_FSELF_FLAGS} -a 0x2808000000000000")

add_executable(pause src/main.c)
target_compile_options(pause PRIVATE -O2 -Wall)
target_link_libraries(pause
  vita2d freetype png jpeg z m bz2
  SceDisplay_stub SceGxm_stub SceSysmodule_stub SceCtrl_stub SceTouch_stub SceAppMgr_stub SceAppUtil_stub
  SceCommonDialog_stub SceRtc_stub ScePower_stub ScePgf_stub ScePvf_stub SceIofilemgr_stub SceLibKernel_stub SceAVConfig_stub SceRegistryMgr_stub SceShellSvc_stub)

set_target_properties(pause PROPERTIES LINK_FLAGS "-Wl,--wrap=sceKernelAllocMemBlock")   # CDRAM -> PHYCONT, see main.c
vita_create_self(eboot.bin pause UNSAFE ATTRIBUTE 0x02 MEMSIZE 0x4000)
vita_create_vpk(VitaOSPause.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE assets/Inter-Regular.ttf assets/Inter-Regular.ttf
  FILE assets/Inter-Bold.ttf assets/Inter-Bold.ttf
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)

# Experiment 1b (REPLACEMENT-ROADMAP.md, 2026-09-27 evening): -pm 0x6800 asked for the whole
# 26 MiB PHYCONT partition, which a suspended game already uses (C1-2609-7 =
# CANNOT_ALLOC_PHYMEMLOW). Now the shape of apps proven to run beside games (ElevenMPV-A):
# attr 0x02, 16 MiB, no PHYCONT, SFO CATEGORY gdc / ATTRIBUTE 0x1088088 / ATTRIBUTE2 0.
