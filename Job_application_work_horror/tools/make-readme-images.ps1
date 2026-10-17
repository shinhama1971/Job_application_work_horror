# ============================================================================
# README に載せる画像を、自動撮影（tools/capture-daily.ps1 や --capture）のスクリーンショットから作ります。
# 実行: powershell -ExecutionPolicy Bypass -File tools/make-readme-images.ps1 -CaptureFolder <撮影のフォルダ>
#
# ・ゲームは暗いので、資料で見やすいよう明るさを上げています（露出を1.35倍にし、ガンマで暗い所を持ち上げる）。
#   ゲームの中の見た目は変えていません。資料の画像だけを明るくしています。
# ・保存先: リポジトリの docs\images\（JPEG、品質88）。README から相対パスで読んでいます。
# ============================================================================
param(
    [Parameter(Mandatory = $true)]
    [string]$CaptureFolder
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$projectDirectory = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$repositoryRoot = Split-Path $projectDirectory -Parent
$outputDirectory = Join-Path $repositoryRoot 'docs\images'
New-Item -ItemType Directory -Force $outputDirectory | Out-Null

# 撮影の画像の名前の始まり → 書き出す名前
$images = [ordered]@{
    '01_' = 'game_01_writing.jpg'
    '05b_' = 'game_02_west_wing.jpg'
    '05l_' = 'game_03_handprint.jpg'
    '05i_' = 'game_04_safe_light.jpg'
    '06_' = 'game_05_stage2_corridor.jpg'
    '05k_' = 'tech_01_ripple_wall_light.jpg'
    '05c_' = 'tech_02_ripple_floor.jpg'
    '05d_' = 'tech_03_pcss_shadow.jpg'
}

# 明るさ：色に1.35を掛け（露出）、ガンマ0.8で暗い所を持ち上げている
$exposure = 1.35
$matrix = New-Object System.Drawing.Imaging.ColorMatrix
$matrix.Matrix00 = $exposure
$matrix.Matrix11 = $exposure
$matrix.Matrix22 = $exposure
$attributes = New-Object System.Drawing.Imaging.ImageAttributes
$attributes.SetColorMatrix($matrix)
$attributes.SetGamma(0.8)

$jpegCodec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq 'image/jpeg' }
$encoderParameters = New-Object System.Drawing.Imaging.EncoderParameters 1
$encoderParameters.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality), ([long]88)

foreach ($prefix in $images.Keys)
{
    $source = Get-ChildItem -LiteralPath $CaptureFolder -Filter ($prefix + '*.png') | Select-Object -First 1
    if ($null -eq $source)
    {
        Write-Warning "見つかりません: $prefix*.png"
        continue
    }
    $image = [System.Drawing.Image]::FromFile($source.FullName)
    try
    {
        $bitmap = New-Object System.Drawing.Bitmap $image.Width, $image.Height
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $graphics.DrawImage($image,
            (New-Object System.Drawing.Rectangle 0, 0, $image.Width, $image.Height),
            0, 0, $image.Width, $image.Height, [System.Drawing.GraphicsUnit]::Pixel, $attributes)
        $graphics.Dispose()
        $outputPath = Join-Path $outputDirectory $images[$prefix]
        $bitmap.Save($outputPath, $jpegCodec, $encoderParameters)
        $bitmap.Dispose()
        Write-Host ("{0} → docs\images\{1}" -f $source.Name, $images[$prefix])
    }
    finally
    {
        $image.Dispose()
    }
}
