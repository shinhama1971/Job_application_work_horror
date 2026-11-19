# ============================================================================
# 提出用のソースファイルのフォルダと zip を作るスクリプト（PowerShell 5.1 で動く）。
#
#   1. release\source_<日付_時刻>\通信途絶_ソースファイル\SignalLost_source\ に、リポジトリのファイルを集める
#        集めるのは git が管理しているファイルと、まだコミットしていない新しいファイル（.gitignore で外した物は入れない）。
#        ビルドで作られる物（x64\ の .obj と exe、cache\ の変換済みモデル、shader\*.cso、.vcxproj.user）は入れない。
#        どれも Visual Studio でビルドすれば作り直される。
#   2. 同じフォルダに 通信途絶_ソースファイル.zip を作る（日本語のファイル名も化けないよう、UTF-8 で名前を書く）
#   3. zip を別の場所へ展開し、そこで Release（x64）をビルドして、警告0で通るか確かめる（-SkipBuild で省ける）
#
# release\ は .gitignore で外している（[Rr]elease/）。前に作ったフォルダは消さず、毎回新しい名前で作る。
#
# 使い方（リポジトリの直下で）:
#   powershell -ExecutionPolicy Bypass -File Job_application_work_horror\tools\make-source-package.ps1
#   文書（.md と docs\）を入れないときは、後ろに -ExcludeDocuments を付ける
# ============================================================================
[CmdletBinding()]
param(
    # 作る場所（既定はリポジトリ直下の release）
    [string]$OutputRoot = "",
    # 展開した zip でのビルドの確認を省く
    [switch]$SkipBuild,
    # 文書を入れない（.md のファイルと、docs\ のフォルダ。ソースとビルドに要る物だけにする）
    [switch]$ExcludeDocuments
)

$ErrorActionPreference = "Stop"

# --- 場所の決定（このスクリプトは Job_application_work_horror\tools にある）
$projectDir = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $projectDir
if ([string]::IsNullOrEmpty($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot "release"
}
$packageName = "通信途絶_ソースファイル"
$sourceFolderName = "SignalLost_source"
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$workDir = Join-Path $OutputRoot "source_$stamp"
$packageDir = Join-Path $workDir $packageName
$sourceDir = Join-Path $packageDir $sourceFolderName
$zipPath = Join-Path $workDir "$packageName.zip"

# --- 1. ファイルを集める
# core.quotepath=false で、日本語のファイル名をそのままの文字で受け取る
$previousEncoding = [Console]::OutputEncoding
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
try {
    $files = @(git -C $repoRoot -c core.quotepath=false ls-files --cached --others --exclude-standard)
}
finally {
    [Console]::OutputEncoding = $previousEncoding
}
if ($LASTEXITCODE -ne 0 -or $files.Count -eq 0) {
    throw "git からファイルの一覧を受け取れませんでした"
}

New-Item -ItemType Directory -Force $sourceDir | Out-Null
$copied = 0
foreach ($relative in $files) {
    $source = Join-Path $repoRoot $relative
    # 消したがまだコミットしていないファイルは、一覧にあっても実物がないので飛ばす
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        continue
    }
    if ($ExcludeDocuments -and ($relative -like "*.md" -or $relative -like "docs/*")) {
        continue
    }
    $destination = Join-Path $sourceDir $relative
    New-Item -ItemType Directory -Force (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
    $copied++
}
Write-Host "集めたファイル: $copied 個 → $sourceDir"

# --- 2. zip を作る（中の一番上が 通信途絶_ソースファイル/ になる）
# ZipFile.CreateFromDirectory は .NET Framework では区切りを「\」で書くことがあり、Windows 以外で開くと
# フォルダにならない。そのため、1つずつ「/」区切りの名前で入れている
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zipStream = [System.IO.File]::Open($zipPath, [System.IO.FileMode]::CreateNew)
$archive = New-Object System.IO.Compression.ZipArchive($zipStream, [System.IO.Compression.ZipArchiveMode]::Create, $false, [System.Text.Encoding]::UTF8)
try {
    foreach ($file in Get-ChildItem -LiteralPath $packageDir -Recurse -File) {
        $entryName = $packageName + "/" + $file.FullName.Substring($packageDir.Length + 1).Replace("\", "/")
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, $file.FullName, $entryName, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
}
finally {
    $archive.Dispose()
    $zipStream.Dispose()
}
$zipMegabytes = [math]::Round((Get-Item $zipPath).Length / 1MB, 1)
Write-Host "zip: $zipPath（$zipMegabytes MB）"

# --- 3. 展開した zip でビルドできるか確かめる
if (-not $SkipBuild) {
    $checkDir = Join-Path $workDir "build_check"
    [System.IO.Compression.ZipFile]::ExtractToDirectory($zipPath, $checkDir, [System.Text.Encoding]::UTF8)
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $msbuild = & $vswhere -latest -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
    if ([string]::IsNullOrEmpty($msbuild)) {
        throw "MSBuild が見つかりません"
    }
    $checkProject = Join-Path $checkDir "$packageName\$sourceFolderName\Job_application_work_horror\Job_application_work_horror.vcxproj"
    $buildLog = & $msbuild $checkProject /p:Configuration=Release /p:Platform=x64 /m /v:m /nologo
    $warnings = @($buildLog | Select-String -Pattern ": warning ")
    if ($LASTEXITCODE -ne 0) {
        $buildLog | Select-Object -Last 30 | ForEach-Object { Write-Host $_ }
        throw "展開した zip のビルドに失敗しました"
    }
    if ($warnings.Count -gt 0) {
        $warnings | ForEach-Object { Write-Host $_ }
        throw "展開した zip のビルドで警告が $($warnings.Count) 個出ました"
    }
    Write-Host "展開した zip の Release ビルド: 成功・警告0（$checkDir）"
}
