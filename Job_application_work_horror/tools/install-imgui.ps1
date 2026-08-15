param(
    [string]$Branch = "master"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$destination = Join-Path $projectRoot "external\imgui"
$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    "signal-lost-imgui-" + [Guid]::NewGuid().ToString("N"))
$archivePath = Join-Path $temporaryRoot "imgui.zip"
$extractPath = Join-Path $temporaryRoot "extract"
$downloadUrl = "https://github.com/ocornut/imgui/archive/refs/heads/$Branch.zip"

$rootPrefix = [System.IO.Path]::GetFullPath($projectRoot) + [System.IO.Path]::DirectorySeparatorChar
$destinationPath = [System.IO.Path]::GetFullPath($destination)
if (-not $destinationPath.StartsWith(
    $rootPrefix,
    [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "The ImGui destination escaped the project directory."
}

try {
    New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
    New-Item -ItemType Directory -Path $extractPath | Out-Null
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    New-Item -ItemType Directory -Path (
        Join-Path $destination "backends") -Force | Out-Null

    Write-Host "Downloading official Dear ImGui ($Branch)..."
    Invoke-WebRequest -Uri $downloadUrl -OutFile $archivePath
    Expand-Archive -LiteralPath $archivePath -DestinationPath $extractPath

    $sourceRoot = Get-ChildItem -LiteralPath $extractPath -Directory |
        Select-Object -First 1 -ExpandProperty FullName
    if (-not $sourceRoot) {
        throw "Dear ImGui archive did not contain a source directory."
    }

    $coreFiles = @(
        "LICENSE.txt", "imconfig.h", "imgui.cpp", "imgui.h",
        "imgui_draw.cpp", "imgui_internal.h", "imgui_tables.cpp",
        "imgui_widgets.cpp", "imstb_rectpack.h", "imstb_textedit.h",
        "imstb_truetype.h")
    foreach ($file in $coreFiles) {
        Copy-Item -LiteralPath (Join-Path $sourceRoot $file) -Destination (Join-Path $destination $file) -Force
    }

    foreach ($file in @(
        "imgui_impl_dx11.cpp", "imgui_impl_dx11.h",
        "imgui_impl_win32.cpp", "imgui_impl_win32.h")) {
        Copy-Item -LiteralPath (Join-Path $sourceRoot "backends\$file") -Destination (Join-Path $destination "backends\$file") -Force
    }

    Write-Host "Dear ImGui files installed in: $destination"
}
finally {
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}
