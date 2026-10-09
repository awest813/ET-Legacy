# Offline Omni-bot runtime: static libraries, without native dynamic loading.
set(OB_ROOT "${PROJECT_SOURCE_DIR}/vendor/omni-bot-runtime/0.83/Omnibot")
set(OB_DEPS "${OB_ROOT}/dependencies")
file(GLOB OB_SOURCES CONFIGURE_DEPENDS
    "${OB_DEPS}/gmscriptex/gmsrc_ex/src/gm/*.cpp"
    "${OB_DEPS}/gmscriptex/gmsrc_ex/src/platform/win32gcc/*.cpp"
    "${OB_DEPS}/gmscriptex/gmsrc_ex/src/binds/*.cpp"
    "${OB_DEPS}/gmscriptex/gmsrc_ex/src/3rdParty/mathlib/*.cpp"
    "${OB_DEPS}/gmscriptex/gmsrc_ex/src/3rdParty/gmbinder2/*.cpp"
    "${OB_DEPS}/wildmagic/*.cpp"
    "${OB_DEPS}/physfs/*.c" "${OB_DEPS}/physfs/archivers/*.c"
    "${OB_DEPS}/physfs/platform/*.c" "${OB_DEPS}/physfs/zlib123/*.c"
    "${OB_DEPS}/physfs/lzma/C/*.c"
    "${OB_DEPS}/physfs/lzma/C/Archive/7z/*.c"
    "${OB_DEPS}/physfs/lzma/C/Compress/Lzma/*.c"
    "${OB_DEPS}/physfs/lzma/C/Compress/Branch/*.c"
    "${PROJECT_SOURCE_DIR}/vendor/boost-filesystem/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/vendor/boost-regex/src/*.cpp")
list(FILTER OB_SOURCES EXCLUDE REGEX "gmSqliteLib\\.cpp$")
file(GLOB OB_BOOST_SOURCES "${PROJECT_SOURCE_DIR}/vendor/boost-filesystem/src/*.cpp" "${PROJECT_SOURCE_DIR}/vendor/boost-regex/src/*.cpp")
set_source_files_properties(${OB_BOOST_SOURCES} PROPERTIES COMPILE_OPTIONS "-U__linux__")
add_library(omnibot-wasm STATIC ${OB_SOURCES} "${OB_ROOT}/Common/BatchBuild.cpp" "${OB_ROOT}/ET/ET_BatchBuild.cpp")
set_target_properties(omnibot-wasm PROPERTIES CXX_STANDARD 11 C_STANDARD 99)
target_compile_options(omnibot-wasm PRIVATE "-sUSE_BOOST_HEADERS=1" -fexceptions -fno-strict-aliasing -Wno-error=implicit-function-declaration)
target_compile_definitions(omnibot-wasm PRIVATE __linux__ unix PHYSFS_NO_THREAD_SUPPORT PHYSFS_NO_CDROM_SUPPORT PHYSFS_SUPPORTS_ZIP=1 PHYSFS_SUPPORTS_7Z=1 BOOST_FILESYSTEM_STATIC_LINK BOOST_FILESYSTEM_NO_CXX20_ATOMIC_REF BOOST_REGEX_NO_LIB)
target_include_directories(omnibot-wasm PRIVATE
    "${OB_ROOT}/Common" "${OB_ROOT}/ET"
    "${OB_DEPS}/gmscriptex/gmsrc_ex/src/3rdParty"
    "${OB_DEPS}/gmscriptex/gmsrc_ex/src/gm"
    "${OB_DEPS}/gmscriptex/gmsrc_ex/src/binds"
    "${OB_DEPS}/gmscriptex/gmsrc_ex/src/3rdParty/gmbinder2"
    "${OB_DEPS}/gmscriptex/gmsrc_ex/src/platform/win32gcc"
    "${OB_DEPS}/wildmagic" "${OB_DEPS}/iprof" "${OB_DEPS}/physfs"
    "${OB_DEPS}/physfs/zlib123" "${OB_DEPS}/physfs/lzma/C"
    "${OB_DEPS}/physfs/lzma/C/Archive/7z"
    "${OB_DEPS}/physfs/lzma/C/Compress/Lzma"
    "${OB_DEPS}/physfs/lzma/C/Compress/Branch")
if(FEATURE_OMNIBOT)
    target_compile_definitions(qagame_libraries INTERFACE FEATURE_OMNIBOT)
    target_link_libraries(qagame_libraries INTERFACE omnibot-wasm)
    target_link_options(client_libraries INTERFACE -fexceptions)
    target_link_options(client_libraries INTERFACE "--preload-file=${PROJECT_SOURCE_DIR}/vendor/omni-bot-browser@/omni-bot")
    # Changes to preloaded scripts/navigation must regenerate etl.data too.
    file(GLOB_RECURSE OB_BROWSER_DATA CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/vendor/omni-bot-browser/*")
    set_property(TARGET client_libraries APPEND PROPERTY INTERFACE_LINK_DEPENDS ${OB_BROWSER_DATA})
endif()
