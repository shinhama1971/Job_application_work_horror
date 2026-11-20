# 配電盤の横に貼る施設案内図を生成する。周回後の版では廊下の奥の扉を壁にする。
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$output = Join-Path $PSScriptRoot '../assets/texture'
$paper = [Drawing.Color]::FromArgb(225, 219, 189)
$ink = [Drawing.Color]::FromArgb(36, 46, 42)
$red = [Drawing.Color]::FromArgb(149, 44, 35)
$line = New-Object Drawing.Pen -ArgumentList @($ink, [single]5)
$door = New-Object Drawing.Pen -ArgumentList @($ink, [single]3)
$label = New-Object Drawing.SolidBrush -ArgumentList $ink
$marker = New-Object Drawing.SolidBrush -ArgumentList $red
$font = New-Object Drawing.Font -ArgumentList @('Meiryo', [single]32, [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)

try {
    foreach ($afterLoop in @($false, $true)) {
        $bitmap = New-Object Drawing.Bitmap -ArgumentList 600, 400
        $canvas = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $canvas.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
            $canvas.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit
            $canvas.Clear($paper)
            $canvas.DrawRectangle($line, 16, 16, 568, 368)
            foreach ($screw in @(@(30, 30), @(561, 30), @(30, 361), @(561, 361))) {
                $canvas.FillEllipse($label, $screw[0], $screw[1], 9, 9)
            }

            # 中央ホールの西側が西棟。北の開口から中央廊下が東へ折れる。
            $canvas.DrawLine($line, 195, 248, 195, 210)
            $canvas.DrawLine($line, 195, 210, 50, 210)
            $canvas.DrawLine($line, 50, 210, 50, 323)
            $canvas.DrawLine($line, 50, 323, 195, 323)
            $canvas.DrawLine($line, 195, 323, 195, 283)
            $canvas.DrawLine($line, 195, 248, 260, 248)
            $canvas.DrawLine($line, 195, 283, 260, 283)
            $canvas.DrawLine($line, 260, 195, 342, 195)
            $canvas.DrawLine($line, 260, 195, 260, 248)
            $canvas.DrawLine($line, 260, 283, 260, 336)
            $canvas.DrawLine($line, 260, 336, 479, 336)
            $canvas.DrawLine($line, 479, 336, 479, 195)
            $canvas.DrawLine($line, 479, 195, 394, 195)
            $canvas.DrawLine($line, 342, 195, 342, 77)
            $canvas.DrawLine($line, 342, 77, 519, 77)
            $canvas.DrawLine($line, 394, 195, 394, 130)
            $canvas.DrawLine($line, 394, 130, 519, 130)

            if ($afterLoop) {
                $canvas.DrawLine($line, 519, 77, 519, 130)
            }
            else {
                # 扉の開く向きを弧と戸板で示す。周回後はこの記号ごと消える。
                $canvas.DrawLine($door, 519, 77, 466, 77)
                $canvas.DrawArc($door, 466, 24, 106, 106, 90, 90)
            }

            $canvas.DrawString('西棟', $font, $label, [single]85, [single]244)
            $canvas.DrawString('中央廊下', $font, $label, [single]178, [single]82)
            $canvas.FillEllipse($marker, 294, 245, 20, 20)
            $canvas.DrawString('現在地', $font, $marker, [single]319, [single]237)
            $name = if ($afterLoop) { 'stage1_facility_map_loop.png' } else { 'stage1_facility_map.png' }
            $bitmap.Save((Join-Path $output $name), [Drawing.Imaging.ImageFormat]::Png)
        }
        finally {
            $canvas.Dispose()
            $bitmap.Dispose()
        }
    }
}
finally {
    $font.Dispose()
    $marker.Dispose()
    $label.Dispose()
    $door.Dispose()
    $line.Dispose()
}
