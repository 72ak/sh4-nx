cmake_minimum_required(VERSION 3.20)
project(sh4nx C)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

file(GLOB GEN CONFIGURE_DEPENDS src/recomp/gen/*.c)
file(GLOB RT CONFIGURE_DEPENDS src/runtime/*.c src/win32/*.c src/d3d8/*.c src/game/*.c)

add_executable(sh4 ${GEN} ${RT})
target_include_directories(sh4 PRIVATE src/recomp/gen)
# Lifted code relies on wrapping arithmetic and type punning through guest memory.
target_compile_options(sh4 PRIVATE -fwrapv -fno-strict-aliasing)
set_source_files_properties(${GEN} PROPERTIES COMPILE_OPTIONS "-w")

if(NINTENDO_SWITCH)
  # Built with the devkitPro toolchain (tools/build-switch.sh); OpenGL comes through EGL and mesa.
  target_include_directories(sh4 PRIVATE ${DEVKITPRO}/portlibs/switch/include)
  target_link_libraries(sh4 PRIVATE SDL2 EGL glapi drm_nouveau stdc++ pthread m)
  nx_generate_nacp(sh4.nacp NAME "Silent Hill 4" AUTHOR "sh4-nx" VERSION "0.01")
  # icon.jpg (256x256, not in the repository: make it from your own copy) becomes the NRO's icon, which a
  # home menu forwarder made from the NRO shows too.
  if(EXISTS ${CMAKE_SOURCE_DIR}/icon.jpg)
    nx_create_nro(sh4 OUTPUT sh4-nx.nro NACP sh4.nacp ICON ${CMAKE_SOURCE_DIR}/icon.jpg)
  else()
    nx_create_nro(sh4 OUTPUT sh4-nx.nro NACP sh4.nacp NOICON)
  endif()
else()
  # GL is loaded at run time through SDL (src/d3d8/gl.c), so nothing links libGL.
  find_package(SDL2 REQUIRED)
  target_link_libraries(sh4 PRIVATE SDL2::SDL2 m pthread)
endif()
