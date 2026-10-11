<#
.SYNOPSIS
Voice training for AudioUtilTTS, from Windows. Runs voice.sh inside WSL.

.DESCRIPTION
Fine-tunes Kokoro on recordings of one speaker and exports a model folder the
plugin loads. Needs WSL 2 with a Linux distribution (wsl --install -d Ubuntu)
and an NVIDIA card.

.EXAMPLE
.\voice.ps1 setup
Once: installs the tools, the training recipe and the base weights inside WSL.

.EXAMPLE
.\voice.ps1 make -Name MyVoice -Clips D:\clips\myvoice -Out D:\voices
Prepares, trains and exports in one go. The result is D:\voices\MyVoice.

.EXAMPLE
.\voice.ps1 export -Name MyVoice -Out D:\voices -VoiceTypes MaleNord,MaleGuard
Exports again, with a model.toml naming the voice types the voice speaks for.
#>
param(
    [Parameter(Position = 0)]
    [ValidateSet('setup', 'prepare', 'train', 'export', 'make', 'status', 'help')]
    [string]$Command = 'help',
    # the voice: letters, digits, - and _. It is also the name of the model folder.
    [string]$Name,
    # folder with the clips and their text (metadata.csv, or a .txt beside each clip)
    [string]$Clips,
    # folder the model folder is written into
    [string]$Out,
    # voice type editor IDs the voice speaks for, written into model.toml
    [string[]]$VoiceTypes,
    # WSL distribution to use; the default one when left out
    [string]$Distro,
    # training steps of stage one and two; 4400 and 2200 when left out
    [int]$Steps1,
    [int]$Steps2,
    # train stage one again even when it is done
    [switch]$Fresh,
    # further settings of voice.sh, as NAME=value (BATCH=2, GPU_FRACTION=0.8, ...)
    [string[]]$Set
)
$ErrorActionPreference = 'Stop'

function ConvertTo-LinuxPath([string]$Path) {
    $full = [System.IO.Path]::GetFullPath($Path)
    if ($full -notmatch '^([A-Za-z]):\\(.*)$') {
        throw "Not a path on a local drive: $Path"
    }
    return '/mnt/' + $Matches[1].ToLower() + '/' + ($Matches[2] -replace '\\', '/').TrimEnd('/')
}

function Assert-Given([string]$Value, [string]$Parameter) {
    if (-not $Value) {
        throw "'$Command' needs -$Parameter. See: Get-Help .\voice.ps1 -Examples"
    }
}

if ($Command -eq 'help') {
    @'
Voice training for AudioUtilTTS: fine-tune Kokoro on recordings of one speaker
and export a model folder the plugin loads.

  .\voice.ps1 setup                                           once
  .\voice.ps1 make    -Name MyVoice -Clips <folder> -Out <folder>   all three below
  .\voice.ps1 prepare -Name MyVoice -Clips <folder>           clips + text -> a training set
  .\voice.ps1 train   -Name MyVoice                           an hour or more
  .\voice.ps1 export  -Name MyVoice -Out <folder> [-VoiceTypes MaleNord,MaleGuard]
  .\voice.ps1 status  [-Name MyVoice]

The clips folder holds the audio files and their text: a metadata.csv with
lines "file name|text", or a .txt file beside each clip.
Options: -Steps1 4400  -Steps2 2200  -Fresh  -Distro <name>  -Set NAME=value
'@
    exit 0
}

if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw 'WSL is not installed. In an administrator PowerShell run: wsl --install -d Ubuntu  (then restart Windows)'
}

$run = @()
if ($Distro) { $run += @('-d', $Distro) }
# --exec hands the arguments over as they are, with no shell in between
$run += @('--exec', 'env', 'VOICE_FRONT=ps')
if ($Steps1) { $run += "STEPS1=$Steps1" }
if ($Steps2) { $run += "STEPS2=$Steps2" }
if ($Fresh) { $run += 'FRESH=1' }
foreach ($setting in $Set) {
    if ($setting -notmatch '^[A-Z_][A-Z0-9_]*=') { throw "-Set takes NAME=value, not '$setting'" }
    $run += $setting
}
$run += @('bash', (ConvertTo-LinuxPath (Join-Path $PSScriptRoot 'voice.sh')), $Command)

$types = ($VoiceTypes -join ',')
switch ($Command) {
    'prepare' {
        Assert-Given $Name 'Name'; Assert-Given $Clips 'Clips'
        $run += @($Name, (ConvertTo-LinuxPath $Clips))
    }
    'train' {
        Assert-Given $Name 'Name'
        $run += $Name
    }
    'export' {
        Assert-Given $Name 'Name'; Assert-Given $Out 'Out'
        New-Item -ItemType Directory -Force -Path $Out | Out-Null
        $run += @($Name, (ConvertTo-LinuxPath $Out))
        if ($types) { $run += $types }
    }
    'make' {
        Assert-Given $Name 'Name'; Assert-Given $Clips 'Clips'; Assert-Given $Out 'Out'
        New-Item -ItemType Directory -Force -Path $Out | Out-Null
        $run += @($Name, (ConvertTo-LinuxPath $Clips), (ConvertTo-LinuxPath $Out))
        if ($types) { $run += $types }
    }
    'status' {
        if ($Name) { $run += $Name }
    }
}

& wsl.exe @run
exit $LASTEXITCODE
