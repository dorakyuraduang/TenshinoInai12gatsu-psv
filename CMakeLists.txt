cmake_minimum_required(VERSION 3.16)
option(TENSHI_VITA "Build the PlayStation Vita package" ON)
if(TENSHI_VITA AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(NOT DEFINED ENV{VITASDK})
    message(FATAL_ERROR "Set VITASDK to your VitaSDK directory")
  endif()
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Vita toolchain")
endif()
project(tenshi_vita LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(tenshi src/main.cpp)
target_compile_options(tenshi PRIVATE -O2 -Wall -Wextra -Wno-psabi)
if(TENSHI_VITA)
  include("${VITASDK}/share/vita.cmake" REQUIRED)
  target_include_directories(tenshi PRIVATE "${VITASDK}/arm-vita-eabi/include/SDL2")
  target_link_libraries(tenshi PRIVATE -Wl,--start-group SDL2_ttf SDL2_image SDL2_mixer SDL2
    freetype harfbuzz bz2 png jpeg webp webpdemux z vorbisfile vorbis ogg opusfile opus modplug xmp-lite
    stdc++ pthread SceVideodec_stub SceCodecEngine_stub SceAudiodec_stub SceGxm_stub SceDisplay_stub SceCtrl_stub SceAppMgr_stub SceAppUtil_stub
    SceAudio_stub SceAudioIn_stub SceSysmodule_stub SceIofilemgr_stub SceCommonDialog_stub
    SceTouch_stub SceHid_stub SceMotion_stub ScePower_stub SceProcessmgr_stub m -Wl,--end-group)
  vita_create_self(eboot.bin tenshi)
  file(GLOB notice_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/licenses/*")
  set(notice_args)
  foreach(notice IN LISTS notice_files)
    get_filename_component(notice_name "${notice}" NAME)
    list(APPEND notice_args FILE "${notice}" "licenses/${notice_name}")
  endforeach()
  vita_create_vpk(tenshi-vita.vpk "TNSH00001" eboot.bin VERSION "00.27" NAME "Tenshi Vita"
    ${notice_args}
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/SourceHanSansCN-Regular.otf" "assets/SourceHanSansCN-Regular.otf"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/source-han-sans.json" "assets/source-han-sans.json"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/opening.mp4" "assets/opening.mp4"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/opening.json" "assets/opening.json"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/opening.idx" "assets/opening.idx"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/FONT_LICENSE.md" "FONT_LICENSE.md"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/icon0.png" "sce_sys/icon0.png"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "LICENSE"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "THIRD_PARTY_NOTICES.md")
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL REQUIRED IMPORTED_TARGET sdl2 SDL2_image SDL2_ttf SDL2_mixer vorbisfile)
  target_link_libraries(tenshi PRIVATE PkgConfig::SDL)
  add_custom_command(TARGET tenshi POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:tenshi>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/SourceHanSansCN-Regular.otf"
      "$<TARGET_FILE_DIR:tenshi>/assets/SourceHanSansCN-Regular.otf")
  foreach(test IN ITEMS runtime_test coverage_test media_test archive_audio_test voice_decoder_test prefetched_audio_test audio_loading_test presentation_test replay_test snapshot_test startup_test message_bytes_test logo_test movie_vita_test movie_index_test)
    add_executable(${test} tests/${test}.cpp)
    target_include_directories(${test} PRIVATE src)
  endforeach()
  target_link_libraries(archive_audio_test PRIVATE PkgConfig::SDL)
  target_link_libraries(voice_decoder_test PRIVATE PkgConfig::SDL)
  target_link_libraries(prefetched_audio_test PRIVATE PkgConfig::SDL)
  target_link_libraries(audio_loading_test PRIVATE PkgConfig::SDL)
  target_link_libraries(snapshot_test PRIVATE PkgConfig::SDL)
  target_link_libraries(logo_test PRIVATE PkgConfig::SDL)
  target_link_libraries(movie_vita_test PRIVATE PkgConfig::SDL)
  set(vita_test_headers "$ENV{VITASDK}/arm-vita-eabi/include")
  if(NOT EXISTS "${vita_test_headers}/psp2/videodec.h")
    set(vita_test_headers "${CMAKE_CURRENT_SOURCE_DIR}/.tools/vitasdk/arm-vita-eabi/include")
  endif()
  target_compile_options(movie_vita_test PRIVATE -idirafter "${vita_test_headers}")
endif()
