# Minimal stub for the WebAssembly build.
#
# The full dependency bundles live in the etlegacy-libs submodule; on
# Emscripten they are replaced by the Emscripten ports and the stub
# targets defined in cmake/ETLSetupFeatures.cmake. Native builds must
# check out the real submodule.
if(NOT EMSCRIPTEN)
	message(FATAL_ERROR
		"Bundled libraries were not found!\n"
		"Clone the repository with submodules:\n"
		"    git submodule init && git submodule update\n"
		"(this stub only supports the Emscripten/WASM build)")
endif()
