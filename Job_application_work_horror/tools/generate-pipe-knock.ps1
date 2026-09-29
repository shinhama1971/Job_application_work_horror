# ============================================================================
# 金属の配管を叩いたような音（assets/Audio/pipe_knock.wav）を合成して書き出します。
# 外部素材を使わず計算だけで作るため、ライセンスを気にせず同梱できます。
# 乱数の種を固定しているので、何度実行しても同じ音になります。
#
# 使い方（プロジェクトのフォルダで実行）:
#   powershell -ExecutionPolicy Bypass -File tools/generate-pipe-knock.ps1
# ============================================================================

$ErrorActionPreference = "Stop"

$sampleRate = 44100
$durationSeconds = 2.0
$sampleCount = [int]($sampleRate * $durationSeconds)
$samples = New-Object double[] $sampleCount
$random = New-Object System.Random 1971

# 金属管の響き。管の固有振動は基音の整数倍にならない（1 : 2.76 : 5.40 : 8.93）ため、
# この比で重ねると「カーン」という金属らしい音になります。高い音ほど早く消えます。
$baseFrequency = 212.0
$modes = @(
    @{ Ratio = 1.00; Amplitude = 0.55; Decay = 1.40 },
    @{ Ratio = 1.012; Amplitude = 0.22; Decay = 1.10 },   # わずかにずれた音で「うなり」を作る
    @{ Ratio = 2.76; Amplitude = 0.34; Decay = 0.70 },
    @{ Ratio = 5.40; Amplitude = 0.20; Decay = 0.36 },
    @{ Ratio = 8.93; Amplitude = 0.10; Decay = 0.18 }
)

for ($i = 0; $i -lt $sampleCount; $i++) {
    $t = $i / $sampleRate
    $value = 0.0
    foreach ($mode in $modes) {
        $frequency = $baseFrequency * $mode.Ratio
        $value += $mode.Amplitude * [Math]::Exp(-$t / $mode.Decay) *
            [Math]::Sin(2.0 * [Math]::PI * $frequency * $t)
    }

    # 叩いた瞬間の低い「ドン」
    $value += 0.45 * [Math]::Exp(-$t / 0.06) * [Math]::Sin(2.0 * [Math]::PI * 74.0 * $t)

    # 叩いた瞬間の短い雑音（最初の約10ミリ秒）
    if ($t -lt 0.012) {
        $value += 0.35 * (1.0 - $t / 0.012) * ($random.NextDouble() * 2.0 - 1.0)
    }

    # 立ち上がりの2ミリ秒だけなめらかにし、プツッという音を防ぎます
    $value *= [Math]::Min(1.0, $t / 0.002)
    $samples[$i] = $value
}

# 廊下で反響したように、遅れた小さな音を重ねます
$echoes = @(
    @{ Delay = 0.090; Gain = 0.26 },
    @{ Delay = 0.175; Gain = 0.12 }
)
$dry = [double[]]$samples.Clone()
foreach ($echo in $echoes) {
    $offset = [int]($echo.Delay * $sampleRate)
    for ($i = $offset; $i -lt $sampleCount; $i++) {
        $samples[$i] += $dry[$i - $offset] * $echo.Gain
    }
}

# 最後の0.3秒で音を消し、ループ時の途切れを防ぎます
$fadeStart = $sampleCount - [int](0.3 * $sampleRate)
for ($i = $fadeStart; $i -lt $sampleCount; $i++) {
    $samples[$i] *= ($sampleCount - $i) / ($sampleCount - $fadeStart)
}

# 最大音量を0.8にそろえて16bitへ変換します
$peak = 0.0
foreach ($value in $samples) { $peak = [Math]::Max($peak, [Math]::Abs($value)) }
$scale = 0.8 / $peak

$outputPath = Join-Path (Split-Path -Parent $PSScriptRoot) "assets\Audio\pipe_knock.wav"
$stream = [System.IO.File]::Create($outputPath)
$writer = New-Object System.IO.BinaryWriter $stream
try {
    $dataSize = $sampleCount * 2
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes("RIFF"))
    $writer.Write([int](36 + $dataSize))
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes("WAVE"))
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes("fmt "))
    $writer.Write([int]16)
    $writer.Write([int16]1)                  # PCM
    $writer.Write([int16]1)                  # モノラル（立体音響で位置を付けるため）
    $writer.Write([int]$sampleRate)
    $writer.Write([int]($sampleRate * 2))
    $writer.Write([int16]2)
    $writer.Write([int16]16)
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes("data"))
    $writer.Write([int]$dataSize)
    foreach ($value in $samples) {
        $writer.Write([int16][Math]::Round($value * $scale * 32767.0))
    }
}
finally {
    $writer.Dispose()
}

Write-Host "Wrote $outputPath ($sampleCount samples)"
