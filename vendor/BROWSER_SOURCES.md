# Browser runtime source snapshots

These are ordinary source files, with upstream notices retained. Local embedded
repository metadata, compiled libraries, native DLLs, and executables are not
part of the snapshots.

* `omni-bot-runtime/0.83/Omnibot`: Common/ET runtime and required PhysicsFS,
  Wild Magic, and profiler sources from `https://github.com/jswigart/omni-bot`,
  commit `b3f10f334de625765100bbfeacc5eac95c098607`.
* Its `dependencies/gmscriptex`: extended GameMonkey source and license from
  `https://github.com/jswigart/gmscriptex`, commit
  `2d24f6e500c327aa2e99f3572e8e2f4b285e37f4`.
* `boost-filesystem`: source and headers from
  `https://github.com/boostorg/filesystem`, commit
  `e65ddb6ef21697970f7d1438f7a46c5233940059`.
* `boost-regex`: source and headers from `https://github.com/boostorg/regex`,
  commit `4cbcd3078e6ae10d05124379623a1bf03fcb9350`.

Boost uses `BOOST_LICENSE_1_0.txt`. PhysicsFS includes its license and the
GameMonkey snapshot includes its license PDF. Other upstream notices remain
in the source files. See `docs/web-wasm.md` for browser runtime changes; the
snapshot includes the local GameMonkey ABI, PhysicsFS executable-path,
interprocess-discovery, and game-interface ownership corrections.

The build consumes these files directly through `cmake/ETLOmnibotWasm.cmake`;
no submodule initialization or upstream download is required for these sources.
