# ============================================================================
# 外の森の雨音（assets/Audio/rain.wav）を合成して書き出す（ループ再生）。
# 外部素材を使わず計算だけで作るため、ライセンスを気にせず同梱できる。
# 乱数の種を固定しているので、何度実行しても同じ音になる。
#
# ・「サー」という雨の面の音：白い雑音を、約500Hz〜6kHzの帯域にし、ゆっくりした風の強弱で揺らしている。
# ・低い「ゴー」という遠くの雨：白い雑音を約250Hz以下に削った音を、小さく重ねている。
# ・葉や地面を打つ細かい粒：短く減衰する高い音（2〜6kHz）を、1秒に約90回、ばらばらな時刻に足している。
# ・ときどき落ちる大きな滴「ポツ」：低めの音（0.6〜1.5kHz）を、1秒に約2回足している。
#
# 使い方（プロジェクトのフォルダで実行）:
#   powershell -ExecutionPolicy Bypass -File tools/generate-rain.ps1
# ============================================================================

$ErrorActionPreference = "Stop"

$sampleRate = 44100
$durationSeconds = 8.0
$sampleCount = [int]($sampleRate * $durationSeconds)
$samples = New-Object double[] $sampleCount
$random = New-Object System.Random 1109

# 1次の低域通過・高域通過の係数
function Get-LowPassAlpha([double]$cutoff) { return 1.0 - [Math]::Exp(-2.0 * [Math]::PI * $cutoff / $sampleRate) }
function Get-HighPassAlpha([double]$cutoff) { return [Math]::Exp(-2.0 * [Math]::PI * $cutoff / $sampleRate) }

$bandLow = Get-LowPassAlpha 6000.0
$bandHigh = Get-HighPassAlpha 500.0
$rumbleLow = Get-LowPassAlpha 250.0
$lowPassed = 0.0
$previousLowPassed = 0.0
$highPassed = 0.0
$rumble = 0.0

for ($i = 0; $i -lt $sampleCount; $i++) {
    $t = $i / $sampleRate
    $white = $random.NextDouble() * 2.0 - 1.0
    $lowPassed += $bandLow * ($white - $lowPassed)
    $highPassed = $bandHigh * ($highPassed + $lowPassed - $previousLowPassed)
    $previousLowPassed = $lowPassed
    $rumble += $rumbleLow * (($random.NextDouble() * 2.0 - 1.0) - $rumble)
    # 風の強弱（0.13Hzと0.41Hz）。ループの長さ（8秒）で1周しなくても、最後のつなぎで重ねるので途切れない
    $gust = 0.78 + 0.14 * [Math]::Sin(2.0 * [Math]::PI * 0.13 * $t) + 0.08 * [Math]::Sin(2.0 * [Math]::PI * 0.41 * $t + 0.7)
    $samples[$i] = $highPassed * 1.6 * $gust + $rumble * 2.4
}

# 短く減衰する音を1つ足している（frequency の正弦波に、少し雑音を混ぜている）
function Add-Drop([int]$start, [double]$frequency, [double]$decaySeconds, [double]$amplitude) {
    $length = [int]($decaySeconds * 5.0 * $sampleRate)
    for ($k = 0; $k -lt $length; $k++) {
        $index = $start + $k
        if ($index -ge $sampleCount) { $index -= $sampleCount }
        $time = $k / $sampleRate
        $envelope = [Math]::Exp(-$time / $decaySeconds)
        $tone = [Math]::Sin(2.0 * [Math]::PI * $frequency * $time) * 0.7 + ($random.NextDouble() * 2.0 - 1.0) * 0.3
        $samples[$index] += $tone * $envelope * $amplitude
    }
}

# 細かい粒（ループの端をまたぐ粒は、先頭へ回して足しているので、つなぎ目でも途切れない）
$fineCount = [int](90 * $durationSeconds)
for ($n = 0; $n -lt $fineCount; $n++) {
    Add-Drop ($random.Next($sampleCount)) (2000.0 + $random.NextDouble() * 4000.0) (0.0015 + $random.NextDouble() * 0.003) (0.05 + $random.NextDouble() * 0.12)
}
# 大きな滴
$largeCount = [int](2 * $durationSeconds)
for ($n = 0; $n -lt $largeCount; $n++) {
    Add-Drop ($random.Next($sampleCount)) (600.0 + $random.NextDouble() * 900.0) (0.008 + $random.NextDouble() * 0.012) (0.12 + $random.NextDouble() * 0.15)
}

# ループしたときにつなぎ目が聞こえないよう、最後の0.5秒と最初の0.5秒を重ねてなめらかにつないでいる
$blend = [int](0.5 * $sampleRate)
for ($i = 0; $i -lt $blend; $i++) {
    $weight = $i / $blend
    $tailIndex = $sampleCount - $blend + $i
    $samples[$i] = $samples[$i] * $weight + $samples[$tailIndex] * (1.0 - $weight)
}
$loopCount = $sampleCount - $blend
$loopSamples = New-Object double[] $loopCount
[Array]::Copy($samples, $loopSamples, $loopCount)

# 最大音量を0.7にそろえて16bitへ変換する
$peak = 0.0
foreach ($value in $loopSamples) { $peak = [Math]::Max($peak, [Math]::Abs($value)) }
$scale = 0.7 / $peak

$outputPath = Join-Path (Split-Path -Parent $PSScriptRoot) "assets\Audio\rain.wav"
$stream = [System.IO.File]::Create($outputPath)
$writer = New-Object System.IO.BinaryWriter $stream
try {
    $dataSize = $loopCount * 2
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes("RIFF"))
    $writer.Write([int](36 + $dataSize))
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes("WAVE"))
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes("fmt "))
    $writer.Write([int]16)
    $writer.Write([int16]1)                  # PCM
    $writer.Write([int16]1)                  # モノラル
    $writer.Write([int]$sampleRate)
    $writer.Write([int]($sampleRate * 2))
    $writer.Write([int16]2)
    $writer.Write([int16]16)
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes("data"))
    $writer.Write([int]$dataSize)
    foreach ($value in $loopSamples) {
        $writer.Write([int16][Math]::Round($value * $scale * 32767.0))
    }
}
finally {
    $writer.Dispose()
}

Write-Host "Wrote $outputPath ($loopCount samples)"
