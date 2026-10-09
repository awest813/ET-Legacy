#-----------------------------------------------------------------
# Build Client
#-----------------------------------------------------------------

set(ETL_OUTPUT_DIR "")

if(WIN32)
	add_executable(etl WIN32 ${COMMON_SRC} ${CLIENT_SRC} ${PLATFORM_SRC} ${PLATFORM_CLIENT_SRC})
elseif(APPLE)
	# These are vars used in the misc/Info.plist template file
	# See set_target_properties( ... MACOSX_BUNDLE_INFO_PLIST ...)
	set(MACOSX_BUNDLE_INFO_STRING            "ET: Legacy")
	set(MACOSX_BUNDLE_ICON_FILE              "etl.icns")
	set(MACOSX_BUNDLE_GUI_IDENTIFIER         "com.etlegacy.etl")
	set(MACOSX_BUNDLE_LONG_VERSION_STRING    "${ETL_CMAKE_VERSION}")
	set(MACOSX_BUNDLE_BUNDLE_NAME            "ET Legacy")
	set(MACOSX_BUNDLE_SHORT_VERSION_STRING   "${ETL_CMAKE_VERSION_SHORT}")
	set(MACOSX_BUNDLE_COPYRIGHT              "etlegacy.com")

	# Specify files to be copied into the .app's Resources folder
	set(RESOURCES_DIR "${CMAKE_SOURCE_DIR}/misc")
	set(MACOSX_RESOURCES "${RESOURCES_DIR}/${MACOSX_BUNDLE_ICON_FILE}")
	set_source_files_properties(${CMAKE_SOURCE_DIR}/misc/${MACOSX_BUNDLE_ICON_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

	# Create the .app bundle
	add_executable(etl MACOSX_BUNDLE ${COMMON_SRC} ${CLIENT_SRC} ${PLATFORM_SRC} ${PLATFORM_CLIENT_SRC} ${MACOSX_RESOURCES})
	set_target_properties(etl PROPERTIES
			OUTPUT_NAME "ET Legacy"
			XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME TRUE
			XCODE_ATTRIBUTE_EXECUTABLE_NAME "etl"
			MACOSX_BUNDLE_EXECUTABLE_NAME "etl"
	)
elseif(ANDROID)
	add_library(etl SHARED ${COMMON_SRC} ${CLIENT_SRC} ${PLATFORM_SRC} ${PLATFORM_CLIENT_SRC})
	set_target_properties(etl PROPERTIES PREFIX "lib")
	if(BUNDLED_SDL)
		# SDL's Android HID backend now builds C++ code, so the client shared
		# library has to link through the C++ driver to pull in the NDK runtime.
		set_target_properties(etl PROPERTIES LINKER_LANGUAGE CXX)
	endif()
	set(ETL_OUTPUT_DIR "legacy")
else()
	add_executable(etl ${COMMON_SRC} ${CLIENT_SRC} ${PLATFORM_SRC} ${PLATFORM_CLIENT_SRC})
endif()

target_link_libraries(etl
	client_libraries
	engine_libraries
	os_libraries # Has to go after cURL and SDL
)

if(FEATURE_WINDOWS_CONSOLE AND WIN32)
	set(ETL_COMPILE_DEF "USE_ICON;USE_WINDOWS_CONSOLE")
else()
	set(ETL_COMPILE_DEF "USE_ICON")
endif()

set_target_properties(etl PROPERTIES
	COMPILE_DEFINITIONS "${ETL_COMPILE_DEF}"
	RUNTIME_OUTPUT_DIRECTORY "${ETL_OUTPUT_DIR}"
	RUNTIME_OUTPUT_DIRECTORY_DEBUG "${ETL_OUTPUT_DIR}"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${ETL_OUTPUT_DIR}"
	MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/misc/Info.plist
)

if((UNIX OR ETL_ARM) AND NOT APPLE AND NOT ANDROID)
	set_target_properties(etl PROPERTIES SUFFIX "${BIN_SUFFIX}")
endif()

target_compile_definitions(etl PRIVATE ETL_CLIENT=1)

if(EMSCRIPTEN)
	# single self-contained WebAssembly build: renderer and mod modules are
	# linked statically, assets are supplied through the browser shell page
	set_target_properties(etl PROPERTIES SUFFIX ".html")
	set_property(TARGET etl APPEND PROPERTY LINK_DEPENDS "${CMAKE_SOURCE_DIR}/misc/web/etl_shell.html")
	set_property(TARGET etl APPEND PROPERTY LINK_DEPENDS "${CMAKE_SOURCE_DIR}/misc/web/network.js")
	set_property(TARGET etl APPEND PROPERTY LINK_DEPENDS "${CMAKE_SOURCE_DIR}/misc/web/touch.js")
	set_property(TARGET etl APPEND PROPERTY LINK_DEPENDS "${CMAKE_SOURCE_DIR}/misc/web/test_built_launcher.cjs" "${CMAKE_SOURCE_DIR}/misc/web/pwa/recovery.html")
	set_property(TARGET etl APPEND PROPERTY LINK_DEPENDS "${CMAKE_SOURCE_DIR}/misc/web/pwa.js" "${CMAKE_SOURCE_DIR}/misc/web/sw.js.in" "${CMAKE_SOURCE_DIR}/misc/web/package_pwa.cmake" "${CMAKE_SOURCE_DIR}/misc/web/pwa/manifest.webmanifest" "${CMAKE_SOURCE_DIR}/misc/web/pwa/icon-192.png" "${CMAKE_SOURCE_DIR}/misc/web/pwa/icon-512.png")
	set_property(TARGET etl APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/etmain/ui/version_generated.h")
	file(GLOB WEB_MENU_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/misc/web/ui/*.menu")
	set_property(TARGET etl APPEND PROPERTY LINK_DEPENDS ${WEB_MENU_FILES})
	if(FEATURE_OMNIBOT)
		file(GLOB_RECURSE OB_PRELOAD_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/vendor/omni-bot-browser/*")
		set_property(TARGET etl APPEND PROPERTY LINK_DEPENDS ${OB_PRELOAD_FILES})
	endif()
	target_link_libraries(etl cgame ui qagame)
	# Publish side modules from the same archives as the static client, and
	# compile their identities into that client for pure-server validation.
	find_package(Python3 REQUIRED COMPONENTS Interpreter)
	find_program(WEB_MODULE_NODE NAMES node nodejs REQUIRED)
	set(WEB_MODULE_DIR "${CMAKE_CURRENT_BINARY_DIR}/web-modules")
	add_custom_command(
		OUTPUT "${WEB_MODULE_DIR}/identity.c" "${WEB_MODULE_DIR}/identity.json"
		       "${WEB_MODULE_DIR}/cgame.mp.wasm32.so" "${WEB_MODULE_DIR}/ui.mp.wasm32.so"
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/misc/web/build_web_modules.py"
		        --compiler "${CMAKE_C_COMPILER}" --node "${WEB_MODULE_NODE}"
		        --cgame "$<TARGET_FILE:cgame>" --ui "$<TARGET_FILE:ui>"
		        --cjson "$<TARGET_FILE:bundled_cjson>" --output "${WEB_MODULE_DIR}"
		DEPENDS cgame ui bundled_cjson "${CMAKE_SOURCE_DIR}/misc/web/build_web_modules.py"
		VERBATIM
	)
	target_sources(etl PRIVATE "${WEB_MODULE_DIR}/identity.c")
	target_link_options(etl PRIVATE
		"--profiling-funcs"
		"-sALLOW_MEMORY_GROWTH=1"
		"-sMAX_WEBGL_VERSION=2"
		"-sMIN_WEBGL_VERSION=2"
		"-sEXIT_RUNTIME=0"
		"-sINVOKE_RUN=0"
		"-sSTACK_SIZE=8388608"
		"-sENVIRONMENT=web"
		"-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,FS,callMain"
		"-sWASM_BIGINT=0"
		"-lwebsocket.js"
		"-lidbfs.js"
		"--shell-file=${CMAKE_SOURCE_DIR}/misc/web/etl_shell.html"
		"--pre-js=${CMAKE_SOURCE_DIR}/misc/web/network.js"
		"--pre-js=${CMAKE_SOURCE_DIR}/misc/web/touch.js"
		"--pre-js=${CMAKE_SOURCE_DIR}/misc/web/pwa.js"
		"--preload-file=${CMAKE_CURRENT_BINARY_DIR}/etmain/ui/version_generated.h@/legacy/ui/version_generated.h"
		"--preload-file=${CMAKE_SOURCE_DIR}/misc/web/ui@/legacy/ui"
	)
	add_custom_command(TARGET etl POST_BUILD COMMAND "${CMAKE_COMMAND}" "-DSOURCE=${CMAKE_SOURCE_DIR}/misc/web" "-DBUILD=${CMAKE_CURRENT_BINARY_DIR}" -P "${CMAKE_SOURCE_DIR}/misc/web/package_pwa.cmake")
endif()

if(MSVC AND NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/etl.vcxproj.user)
	configure_file(${PROJECT_SOURCE_DIR}/cmake/vs2013.vcxproj.user.in ${CMAKE_CURRENT_BINARY_DIR}/etl.vcxproj.user @ONLY)
endif()

install(TARGETS etl
	BUNDLE  DESTINATION "${INSTALL_DEFAULT_BINDIR}"
	RUNTIME DESTINATION "${INSTALL_DEFAULT_BINDIR}"
)
