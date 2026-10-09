#!/bin/bash
# Build ET: Legacy for WebAssembly (Emscripten).
# Usage: ./build_wasm.sh [configure|build]
set -e

SOURCE="$(cd "$(dirname "$0")" && pwd)"
# Use an activated Emscripten environment, or an explicitly supplied SDK path.
if [ -n "${EMSDK:-}" ]; then
	export PATH="$EMSDK/upstream/emscripten:$EMSDK/upstream/bin:$PATH"
fi
if ! command -v emcmake >/dev/null 2>&1; then
	echo "Activate emsdk_env.sh or set EMSDK before building." >&2
	exit 1
fi
BUILD_DIR="$SOURCE/build_wasm"
MODE="${1:-build}"
if [ "$MODE" = "configure" ] || [ "$MODE" = "build" ]; then
	if [ "$#" -gt 0 ]; then shift; fi
fi

cmake_args=(
	-G Ninja
	-DCMAKE_BUILD_TYPE=Release
	-DBUILD_CLIENT=ON
	-DBUILD_SERVER=OFF
	-DBUILD_MOD=ON
	-DBUILD_CLIENT_MOD=ON
	-DBUILD_SERVER_MOD=ON
	-DBUNDLED_LIBS=ON
	-DCROSS_COMPILE32=OFF
	-DENABLE_MULTI_BUILD=OFF
	-DRENDERER_DYNAMIC=OFF
	-DFEATURE_RENDERER1=ON
	-DFEATURE_RENDERER2=OFF
	-DFEATURE_RENDERER_GLES=OFF
	-DFEATURE_RENDERER_VULKAN=OFF
	-DFEATURE_CURL=OFF
	-DFEATURE_SSL=OFF
	-DFEATURE_AUTH=OFF
	-DFEATURE_OPENAL=OFF
	-DFEATURE_OGG_VORBIS=ON
	-DFEATURE_THEORA=OFF
	-DFEATURE_FREETYPE=ON
	-DFEATURE_PNG=ON
	-DFEATURE_IRC_CLIENT=OFF
	-DFEATURE_IRC_SERVER=OFF
	-DFEATURE_DBMS=OFF
	-DFEATURE_LUA=OFF
	-DFEATURE_LUASQL=OFF
	-DFEATURE_OMNIBOT=ON
	-DFEATURE_AUTOUPDATE=OFF
	-DFEATURE_TRACKER=OFF
	-DFEATURE_ANTICHEAT=OFF
	-DFEATURE_MULTIVIEW=ON
	-DFEATURE_EDV=ON
	-DFEATURE_PAKISOLATION=OFF
	-DFEATURE_IPV6=OFF
	-DINSTALL_EXTRA=OFF
)

if [ "$MODE" = "configure" ] || [ ! -f "$BUILD_DIR/build.ninja" ]; then
	mkdir -p "$BUILD_DIR"
	cd "$BUILD_DIR"
	emcmake cmake "${cmake_args[@]}" "$SOURCE"
	[ "$MODE" = "configure" ] && exit 0
fi

cd "$BUILD_DIR"
ninja "$@"
