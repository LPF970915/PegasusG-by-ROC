param(
  [ValidatePattern('^[0-9A-Za-z._-]+$')]
  [string]$Version = "1.08",
  [ValidateSet("Stage", "Zip")]
  [string]$Output = "Stage",
  [ValidateSet("Sysroot", "Docker")]
  [string]$Builder = "Sysroot",
  [switch]$SkipBuild,
  [string]$Sysroot = "",
  [string]$MusicSource = "",
  [string]$FontSource = "",
  [string]$Distro = "Ubuntu"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $Sysroot) {
  $Sysroot = Join-Path $repoRoot "H700\sysroot"
}
if (-not $SkipBuild -and $Builder -eq "Sysroot" -and -not (Test-Path -LiteralPath $Sysroot -PathType Container)) {
  throw "Missing H700 sysroot: $Sysroot. Provide -Sysroot with a target sysroot or place one at H700\sysroot."
}
if (-not $MusicSource) {
  $MusicSource = Join-Path $repoRoot "assets\music\builtin"
}
if (-not (Test-Path -LiteralPath $MusicSource -PathType Container)) {
  throw "Missing music source: $MusicSource"
}

function WslPath([string]$Path) {
  $resolved = (Resolve-Path -LiteralPath $Path).Path -replace '\\', '/'
  $value = (wsl -d $Distro -- wslpath -a "$resolved").Trim()
  if ($LASTEXITCODE -ne 0 -or -not $value) { throw "Unable to convert path to WSL: $Path" }
  return $value
}

$rootWsl = WslPath $repoRoot
$musicWsl = WslPath $MusicSource
$buildEnvironment = @(
  "PEGASUSG_VERSION=$Version", "PEGASUSG_OUTPUT=$Output", "PEGASUSG_BUILDER=$Builder",
  "PEGASUSG_SKIP_BUILD=$([int]$SkipBuild.IsPresent)", "PEGASUSG_MUSIC_SOURCE=$musicWsl"
)
if (-not $SkipBuild -and $Builder -eq "Sysroot") {
  $buildEnvironment += "PEGASUSG_SYSROOT=$(WslPath $Sysroot)"
}
if ($env:PEGASUSG_BUILDER_IMAGE) {
  $buildEnvironment += "PEGASUSG_BUILDER_IMAGE=$env:PEGASUSG_BUILDER_IMAGE"
}
if ($FontSource) {
  if (-not (Test-Path -LiteralPath $FontSource -PathType Leaf)) {
    throw "Missing font source: $FontSource"
  }
  $buildEnvironment += "PEGASUSG_FONT_SOURCE=$(WslPath $FontSource)"
}
wsl -d $Distro -- env @buildEnvironment bash "$rootWsl/H700/build_app.sh"
if ($LASTEXITCODE -ne 0) { throw "H700 app build failed with exit code $LASTEXITCODE" }

if ($Output -eq "Zip") {
  $archive = Join-Path $PSScriptRoot "Downloads\PegasusG by ROC ver$Version for H700 - Full Package.zip"
  if (-not (Test-Path -LiteralPath $archive)) { throw "Expected archive was not produced: $archive" }
  Write-Host "[h700] output: $archive"
} else {
  Write-Host "[h700] staged: $(Join-Path $PSScriptRoot 'dist_app\release_stage')"
}
