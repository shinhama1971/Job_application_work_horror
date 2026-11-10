# ============================================================================
# 配布用のフォルダと zip を作るスクリプト（PowerShell 5.1 で動く）。
#
#   1. Release（x64）をビルドし、警告が0か確かめる（-SkipBuild で省ける）
#   2. release\SignalLost_<日付_時刻>\ に、遊ぶのに必要な物だけを集める
#        SignalLost.exe          ゲーム本体（x64\Release の Job_application_work_horror.exe の名前を変えた物）
#        assimp-vc143-mt.dll     モデルの読み込み（Assimp）
#        assets\                 素材（モデル・音・画像・配置ファイル）
#        shader\*.cso            コンパイル済みのシェーダー（.hlsl のソースは入れない）
#        cache\                  変換済みのモデル（あれば。初めての起動を速くする）
#        README.txt              遊ぶ人向けの説明（distribution\README.txt）
#        THIRD_PARTY_NOTICES.txt 使っているライブラリと素材の一覧（distribution\THIRD_PARTY_NOTICES.txt）
#        licenses\               ライブラリのライセンスの全文
#   3. 同じ名前の zip を release\ に作る（日本語のファイル名も化けないよう、UTF-8 で名前を書く）
#   4. 配布用のフォルダの exe を、別のフォルダを作業フォルダにして自動撮影で動かし、最後まで動くか確かめる（-SkipTest で省ける）
#
# release\ は .gitignore で外している（[Rr]elease/）。前に作ったフォルダは消さず、毎回新しい名前で作る。
#
# 使い方（リポジトリの直下で）:
#   powershell -ExecutionPolicy Bypass -File Job_application_work_horror\tools\make-release.ps1
# ============================================================================
[CmdletBinding()]
param(
    # 配布用のフォルダを作る場所（既定はリポジトリ直下の release）
    [string]$OutputRoot = "",
    # ビルドを省く（すでに Release をビルドしてあるとき）
    [switch]$SkipBuild,
    # 配布用のフォルダからの起動の確認を省く
    [switch]$SkipTest
)

$ErrorActionPreference = "Stop"

# --- 場所の決定（このスクリプトは Job_application_work_horror\tools にある）
$projectDir = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $projectDir
if ([string]::IsNullOrEmpty($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot "release"
}
$projectFile = Join-Path $projectDir "Job_application_work_horror.vcxproj"
$buildDir = Join-Path $projectDir "x64\Release"
$distributionDir = Join-Path $repoRoot "distribution"

function Write-Step([string]$message) {
    Write-Host ""
    Write-Host "== $message" -ForegroundColor Cyan
}

# --- 1. ビルド
if (-not $SkipBuild) {
    Write-Step "Release（x64）をビルドしています"
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe が見つかりません。Visual Studio 2022 が入っているか確かめてください。"
    }
    $msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
    if ([string]::IsNullOrEmpty($msbuild)) {
        throw "MSBuild.exe が見つかりません。"
    }
    $buildOutput = & $msbuild $projectFile /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal /nr:false
    $buildExitCode = $LASTEXITCODE
    $warnings = @($buildOutput | Select-String -Pattern "warning [A-Z]+\d+" )
    $buildOutput | Select-String -Pattern "error|warning" | ForEach-Object { Write-Host $_.Line }
    if ($buildExitCode -ne 0) {
        throw "ビルドに失敗しました（終了コード $buildExitCode）。"
    }
    if ($warnings.Count -gt 0) {
        throw "ビルドに警告が $($warnings.Count) 件あります。警告0にしてから配布用のフォルダを作ってください。"
    }
    Write-Host "ビルド成功（警告0）"
}

# --- 必要なファイルがそろっているか
$exePath = Join-Path $buildDir "Job_application_work_horror.exe"
$dllPath = Join-Path $buildDir "assimp-vc143-mt.dll"
foreach ($required in @($exePath, $dllPath, (Join-Path $distributionDir "README.txt"), (Join-Path $distributionDir "THIRD_PARTY_NOTICES.txt"))) {
    if (-not (Test-Path $required)) {
        throw "必要なファイルがありません: $required"
    }
}
$shaderDir = Join-Path $projectDir "shader"
$shaderSources = @(Get-ChildItem $shaderDir -Filter *.hlsl -File | Where-Object { $_.Name -ne "common.hlsl" })
$missingCso = @($shaderSources | Where-Object { -not (Test-Path ([IO.Path]::ChangeExtension($_.FullName, ".cso"))) })
if ($missingCso.Count -gt 0) {
    throw "コンパイル済みのシェーダー（.cso）がない物があります: $($missingCso.Name -join ', ')。ビルドし直してください。"
}

# --- 2. 配布用のフォルダを作る（毎回新しい名前。前のフォルダは残す）
$stamp = Get-Date -Format "yyyyMMdd_HHmm"
$packageName = "SignalLost_$stamp"
$packageDir = Join-Path $OutputRoot $packageName
Write-Step "配布用のフォルダを作っています: $packageDir"
if (Test-Path $packageDir) {
    throw "同じ名前のフォルダがすでにあります（1分待ってやり直してください）: $packageDir"
}
New-Item -ItemType Directory -Force $packageDir | Out-Null

Copy-Item $exePath (Join-Path $packageDir "SignalLost.exe")
Copy-Item $dllPath $packageDir

# 素材（assets）は丸ごと
Copy-Item (Join-Path $projectDir "assets") (Join-Path $packageDir "assets") -Recurse

# シェーダーは .cso だけ（ゲームは .hlsl がなければ .cso を読む。RendererResources.cpp の CompileShader）
$packageShaderDir = Join-Path $packageDir "shader"
New-Item -ItemType Directory -Force $packageShaderDir | Out-Null
Get-ChildItem $shaderDir -Filter *.cso -File | Copy-Item -Destination $packageShaderDir

# 変換済みのモデル（あれば）。元のモデルの大きさと更新日時が合わないときは、ゲームが作り直す
$cacheDir = Join-Path $projectDir "cache"
if (Test-Path $cacheDir) {
    Copy-Item $cacheDir (Join-Path $packageDir "cache") -Recurse
}

# 説明書と、ライブラリ・素材の一覧
Copy-Item (Join-Path $distributionDir "README.txt") $packageDir
Copy-Item (Join-Path $distributionDir "THIRD_PARTY_NOTICES.txt") $packageDir

# ライブラリのライセンスの全文
$licenseDir = Join-Path $packageDir "licenses"
New-Item -ItemType Directory -Force $licenseDir | Out-Null
$licenses = @(
    @{ Source = "external\assimp\LICENSE";      Name = "Assimp_LICENSE.txt" },
    @{ Source = "external\directxtk\LICENSE";   Name = "DirectXTK_LICENSE.txt" },
    @{ Source = "external\imgui\LICENSE.txt";   Name = "DearImGui_LICENSE.txt" }
)
foreach ($license in $licenses) {
    $source = Join-Path $projectDir $license.Source
    if (-not (Test-Path $source)) {
        throw "ライセンスの文書がありません: $source"
    }
    Copy-Item $source (Join-Path $licenseDir $license.Name)
}

# --- 3. zip を作る
$zipPath = Join-Path $OutputRoot "$packageName.zip"
Write-Step "zip を作っています: $zipPath"
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $packageDir, $zipPath, [System.IO.Compression.CompressionLevel]::Optimal, $true, [System.Text.Encoding]::UTF8)

# --- 4. 配布用のフォルダから起動して確かめる
if (-not $SkipTest) {
    Write-Step "配布用のフォルダの exe を、別の作業フォルダから自動撮影で動かしています（森の道順。1分ほど）"
    $testDir = Join-Path ([IO.Path]::GetTempPath()) "SignalLost_release_check_$stamp"
    New-Item -ItemType Directory -Force $testDir | Out-Null
    $process = Start-Process -FilePath (Join-Path $packageDir "SignalLost.exe") `
        -ArgumentList "--forest", "--capture", "`"$testDir`"" `
        -WorkingDirectory ([IO.Path]::GetTempPath()) -PassThru -Wait
    $images = @(Get-ChildItem $testDir -Filter *.png -File -ErrorAction SilentlyContinue)
    if ($process.ExitCode -ne 0 -or $images.Count -eq 0) {
        throw "配布用のフォルダからの起動の確認に失敗しました（終了コード $($process.ExitCode)、画像 $($images.Count) 枚）。$testDir を確かめてください。"
    }
    Write-Host "起動の確認に成功（終了コード0、画像 $($images.Count) 枚。$testDir）"
    # 起動したときに、配布用のフォルダの中にファイルが増えていないか（シェーダーのコンパイルのやり直しなど）
    $newFiles = @(Get-ChildItem $packageDir -Recurse -File | Where-Object { $_.LastWriteTime -gt $process.StartTime })
    if ($newFiles.Count -gt 0) {
        Write-Host "注意：起動したときに、配布用のフォルダの中で書き換わったファイルがあります（zip には入っていません）:" -ForegroundColor Yellow
        $newFiles | ForEach-Object { Write-Host "  $($_.FullName)" }
    }
}

# --- まとめ
$sizeMB = (Get-Item $zipPath).Length / 1MB
Write-Step "完成"
Write-Host ("配布用のフォルダ: {0}" -f $packageDir)
Write-Host ("zip:             {0}（{1:N1} MB）" -f $zipPath, $sizeMB)
Write-Host "公開する前に、README.txt と THIRD_PARTY_NOTICES.txt の【要確認】の所を埋めてください。"
