# Requires the existing browser bot library (ninja -C build_wasm etl).
param([string]$Compiler = 'em++', [string]$Node = 'node')
$ErrorActionPreference = 'Stop'
$botRepo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Push-Location -LiteralPath $botRepo
try {
    $gmRoot = 'vendor/omni-bot-runtime/0.83/Omnibot/dependencies/gmscriptex/gmsrc_ex/src'
    $botCheckArgs = @('misc/web/test_bot_classes.cpp', 'build_wasm/libomnibot-wasm.a',
        "-I$gmRoot/gm", "-I$gmRoot/platform/win32gcc", "-I$gmRoot/binds", "-I$gmRoot/3rdParty",
        '-O1', '-fexceptions', '-sASSERTIONS=1', '-sENVIRONMENT=node', '-sWASM_ASYNC_COMPILATION=0',
        '--embed-file', 'vendor/omni-bot-browser/et/scripts/et_classmanager.gm@/et_classmanager.gm',
        '--embed-file', 'vendor/omni-bot-browser/et/scripts/goals/goal_difficulty.gm@/goal_difficulty.gm')
    foreach ($botGoal in @('selectweapons', 'rolemanager')) {
        $botCheckArgs += @('--embed-file', "vendor/omni-bot-browser/et/scripts/goals/goal_$botGoal.gm@/goal_$botGoal.gm")
    }
    $botCheckArgs += @('--embed-file', 'vendor/omni-bot-browser/global_scripts/server_manager.gm@/server_manager.gm')
    foreach ($botMap in @('oasis', 'goldrush', 'battery', 'fueldump', 'radar', 'railgun', 'etx_railgun')) {
        $botCheckArgs += @('--embed-file', "vendor/omni-bot-browser/et/nav/$botMap.gm@/nav/$botMap.gm")
    }
    $botCheckArgs += @('-o', 'build_wasm/test_bot_classes.cjs')
    & $Compiler @botCheckArgs
    if ($LASTEXITCODE -ne 0) { throw 'Bot regression-check compilation failed.' }
    & $Node 'build_wasm/test_bot_classes.cjs'
    if ($LASTEXITCODE -ne 0) { throw 'Bot regression checks failed.' }
} finally {
    Pop-Location
}
