# ============================================================================
# 携帯ラジオの雑音（assets/Audio/radio_static.wav）を合成して書き出す。
# サイレントヒルのラジオのように、影が近づくと鳴り出し、近いほど大きくなる音として使う（ループ再生）。
# 外部素材を使わず計算だけで作るため、ライセンスを気にせず同梱できる。
# 乱数の種を固定しているので、何度実行しても同じ音になる。
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
$lowPassCutoffHz = 3000.0
$highPassCutoffHz = 300.0
$crackleMinSeconds = 0.004
$crackleExtraSeconds = 0.020
$crackleMinGain = 0.6
$crackleExtraGain = 0.9
$loopBlendSeconds = 0.25
$targetPeak = 0.7
$wobbleBase = 0.72
$wobbleFirstGain = 0.18
$wobbleSecondGain = 0.10
$wobbleFirstHz = 2.3
$wobbleSecondHz = 0.7
$wobbleSecondPhase = 1.3
$staticGain = 2.2

# 白い雑音を、小さなラジオのスピーカーのように、低い音と高い音を削った帯域（約300Hz〜3kHz）にしている。
# 1次の低域通過と高域通過を重ねた、簡単なフィルター。
$lowPassAlpha = 1.0 - [Math]::Exp(-2.0 * [Math]::PI * $lowPassCutoffHz / $sampleRate)
$highPassAlpha = [Math]::Exp(-2.0 * [Math]::PI * $highPassCutoffHz / $sampleRate)
$lowPassed = 0.0
$previousLowPassed = 0.0
$highPassed = 0.0

# 「ザザッ」という荒いはじける音（パチパチ）を、ランダムな時刻に入れている
$crackleChancePerSample = 0.0009
$crackleTimer = 0
$crackleAmplitude = 0.0

for ($i = 0; $i -lt $sampleCount; $i++) {
    $t = $i / $sampleRate
    $white = $random.NextDouble() * 2.0 - 1.0

    $lowPassed += $lowPassAlpha * ($white - $lowPassed)
    $highPassed = $highPassAlpha * ($highPassed + $lowPassed - $previousLowPassed)
    $previousLowPassed = $lowPassed
    $value = $highPassed * $staticGain

    # 電波が揺れるように、音の大きさをゆっくり（2.3Hzと0.7Hz）揺らしている
    $wobble = $wobbleBase + $wobbleFirstGain * [Math]::Sin(2.0 * [Math]::PI * $wobbleFirstHz * $t) + $wobbleSecondGain * [Math]::Sin(2.0 * [Math]::PI * $wobbleSecondHz * $t + $wobbleSecondPhase)
    $value *= $wobble

    # パチパチ：まれに短く大きな雑音の粒を足している
    if ($crackleTimer -le 0 -and $random.NextDouble() -lt $crackleChancePerSample) {
        $crackleTimer = [int]($sampleRate * ($crackleMinSeconds + $random.NextDouble() * $crackleExtraSeconds))
        $crackleAmplitude = $crackleMinGain + $random.NextDouble() * $crackleExtraGain
    }
    if ($crackleTimer -gt 0) {
        $value += ($random.NextDouble() * 2.0 - 1.0) * $crackleAmplitude
        $crackleTimer--
    }

    # かすかな電源のうなり（120Hz）
    $value += 0.05 * [Math]::Sin(2.0 * [Math]::PI * 120.0 * $t)
    $samples[$i] = $value
}

# ループしたときにつなぎ目が聞こえないよう、最後の0.25秒と最初の0.25秒を重ねてなめらかにつないでいる
$blend = [int]($loopBlendSeconds * $sampleRate)
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
$scale = $targetPeak / $peak

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
