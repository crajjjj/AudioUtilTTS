# Fetches what the build and the mod need but the repo does not track:
#   extern\sherpa-onnx\lib      prebuilt static libraries (sherpa-onnx + onnxruntime)
#   extern\sherpa-onnx\include  the matching C API header, from the same tag
#   dist\SKSE\Plugins\AudioUtilTTS\models\<Model>   the default voice model
# Safe to re-run: anything already present is skipped.
param(
    [string]$SherpaVersion = "1.13.8",
    [string]$Model = "kokoro-en-v0_19",
    [switch]$SkipModel
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$extern = Join-Path $root "extern\sherpa-onnx"
$models = Join-Path $root "dist\SKSE\Plugins\AudioUtilTTS\models"
$work = Join-Path $root "extern\_download"

function Get-Archive([string]$url, [string]$name) {
    New-Item -ItemType Directory -Force $work | Out-Null
    $archive = Join-Path $work "$name.tar.bz2"
    Write-Host "downloading $url"
    curl.exe -fsSL -o $archive $url
    if ($LASTEXITCODE -ne 0) { throw "download failed: $url" }
    tar.exe -xjf $archive -C $work
    if ($LASTEXITCODE -ne 0) { throw "extract failed: $archive" }
    Remove-Item -LiteralPath $archive
    return (Join-Path $work $name)
}

# static libraries, release build against the static CRT (matches xmake.lua "MT")
$lib = Join-Path $extern "lib"
if (-not (Test-Path (Join-Path $lib "sherpa-onnx-c-api.lib"))) {
    $name = "sherpa-onnx-v$SherpaVersion-win-x64-static-MT-Release-lib"
    $unpacked = Get-Archive "https://github.com/k2-fsa/sherpa-onnx/releases/download/v$SherpaVersion/$name.tar.bz2" $name
    New-Item -ItemType Directory -Force $extern | Out-Null
    if (Test-Path $lib) { Remove-Item -LiteralPath $lib -Recurse }
    Move-Item -LiteralPath (Join-Path $unpacked "lib") -Destination $lib
} else {
    Write-Host "sherpa-onnx libraries present"
}

# header from the same tag, so the struct layouts always match the libraries
$header = Join-Path $extern "include\sherpa-onnx\c-api\c-api.h"
if (-not (Test-Path $header)) {
    New-Item -ItemType Directory -Force (Split-Path $header -Parent) | Out-Null
    $url = "https://raw.githubusercontent.com/k2-fsa/sherpa-onnx/v$SherpaVersion/sherpa-onnx/c-api/c-api.h"
    Write-Host "downloading $url"
    curl.exe -fsSL -o $header $url
    if ($LASTEXITCODE -ne 0) { throw "download failed: $url" }
} else {
    Write-Host "sherpa-onnx header present"
}

if (-not $SkipModel) {
    $target = Join-Path $models $Model
    if (-not (Test-Path $target)) {
        $unpacked = Get-Archive "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/$Model.tar.bz2" $Model
        New-Item -ItemType Directory -Force $models | Out-Null
        Move-Item -LiteralPath $unpacked -Destination $target
    } else {
        Write-Host "model $Model present"
    }
}

if (Test-Path $work) { Remove-Item -LiteralPath $work -Recurse }
Write-Host "done"
