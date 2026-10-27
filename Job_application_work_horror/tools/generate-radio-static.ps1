# ============================================================================
# 携帯ラジオの雑音（assets/Audio/radio_static.wav）を合成して書き出します。
# サイレントヒルのラジオのように、影が近づくと鳴り出し、近いほど大きくなる音として使います（ループ再生）。
# 外部素材を使わず計算だけで作るため、ライセンスを気にせず同梱できます。
# 乱数の種を固定しているので、何度実行しても同じ音になります。
#
# 使い方（プロジェクトのフォルダで実行）:
#   powershell -ExecutionPolicy Bypass -File tools/generate-radio-static.ps1
# ============================================================================

$ErrorActionPreference = "Stop"

$sampleRate = 44100
$durationSeconds = 3.0
$sampleCount = [int]($sampleRate * $durationSeconds)
$samples = New-Object double[] $sampleCount
$random = New-Object System.Random 2024

# 白い雑音を、小さなラジオのスピーカーのように、低い音と高い音を削った帯域（約300Hz〜3kHz）にしています。
# 1次の低域通過と高域通過を重ねた、簡単なフィルターです。
$lowPassAlpha = 1.0 - [Math]::Exp(-2.0 * [Math]::PI * 3000.0 / $sampleRate)
$highPassAlpha = [Math]::Exp(-2.0 * [Math]::PI * 300.0 / $sampleRate)
$lowPassed = 0.0
$previousLowPassed = 0.0
$highPassed = 0.0

# 「ザザッ」という荒いはじける音（パチパチ）を、ランダムな時刻に入れています
$crackleTimer = 0
$crackleAmplitude = 0.0

for ($i = 0; $i -lt $sampleCount; $i++) {
    $t = $i / $sampleRate
    $white = $random.NextDouble() * 2.0 - 1.0

    $lowPassed += $lowPassAlpha * ($white - $lowPassed)
    $highPassed = $highPassAlpha * ($highPassed + $lowPassed - $previousLowPassed)
    $previousLowPassed = $lowPassed
    $value = $highPassed * 2.2

    # 電波が揺れるように、音の大きさをゆっくり（2.3Hzと0.7Hz）揺らしています
    $wobble = 0.72 + 0.18 * [Math]::Sin(2.0 * [Math]::PI * 2.3 * $t) + 0.10 * [Math]::Sin(2.0 * [Math]::PI * 0.7 * $t + 1.3)
    $value *= $wobble

    # パチパチ：まれに短く大きな雑音の粒を足しています
    if ($crackleTimer -le 0 -and $random.NextDouble() -lt 0.0009) {
        $crackleTimer = [int]($sampleRate * (0.004 + $random.NextDouble() * 0.020))
        $crackleAmplitude = 0.6 + $random.NextDouble() * 0.9
    }
    if ($crackleTimer -gt 0) {
        $value += ($random.NextDouble() * 2.0 - 1.0) * $crackleAmplitude
        $crackleTimer--
    }

    # かすかな電源のうなり（120Hz）
    $value += 0.05 * [Math]::Sin(2.0 * [Math]::PI * 120.0 * $t)
    $samples[$i] = $value
}

# ループしたときにつなぎ目が聞こえないよう、最後の0.25秒と最初の0.25秒を重ねてなめらかにつないでいます
$blend = [int](0.25 * $sampleRate)
for ($i = 0; $i -lt $blend; $i++) {
    $weight = $i / $blend
    $tailIndex = $sampleCount - $blend + $i
    $samples[$i] = $samples[$i] * $weight + $samples[$tailIndex] * (1.0 - $weight)
}
$loopCount = $sampleCount - $blend
$loopSamples = New-Object double[] $loopCount
[Array]::Copy($samples, $loopSamples, $loopCount)

# 最大音量を0.7にそろえて16bitへ変換します
$peak = 0.0
foreach ($value in $loopSamples) { $peak = [Math]::Max($peak, [Math]::Abs($value)) }
$scale = 0.7 / $peak

$outputPath = Join-Path (Split-Path -Parent $PSScriptRoot) "assets\Audio\radio_static.wav"
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
