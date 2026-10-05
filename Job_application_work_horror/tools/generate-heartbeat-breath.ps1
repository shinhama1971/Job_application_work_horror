# ============================================================================
# 心拍音（assets/Audio/heartbeat.wav）と呼吸音（assets/Audio/breath.wav）を合成して書き出します。
# 外部素材を使わず計算だけで作るため、ライセンスを気にせず同梱できます。
# 乱数の種を固定しているので、何度実行しても同じ音になります。
#
# ・心拍音は「ドクン」1回分です。ゲーム側で間隔・音量・高さを変えながら繰り返し鳴らします。
# ・呼吸音は「吐いて、吸う」1回分です。危険なほど速く・大きく鳴らします。
#
# 使い方（プロジェクトのフォルダで実行）:
#   powershell -ExecutionPolicy Bypass -File tools/generate-heartbeat-breath.ps1
# ============================================================================

$ErrorActionPreference = "Stop"
$sampleRate = 44100

function Write-MonoWav([double[]]$samples, [string]$fileName, [double]$peakLevel) {
    # 最大音量をそろえて16bitのモノラルWAVへ変換します
    $peak = 0.0
    foreach ($value in $samples) { $peak = [Math]::Max($peak, [Math]::Abs($value)) }
    $scale = $peakLevel / $peak

    $outputPath = Join-Path (Split-Path -Parent $PSScriptRoot) "assets\Audio\$fileName"
    $stream = [System.IO.File]::Create($outputPath)
    $writer = New-Object System.IO.BinaryWriter $stream
    try {
        $dataSize = $samples.Length * 2
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
        foreach ($value in $samples) {
            $writer.Write([int16][Math]::Round($value * $scale * 32767.0))
        }
    }
    finally {
        $writer.Dispose()
    }
    Write-Host "Wrote $outputPath ($($samples.Length) samples)"
}

# ---------------------------------------------------------------------------
# 心拍音: 低く短い「ドッ」と、少し高く弱い「クン」の2つの音でできています。
# 胸の中で聞こえるように、高い音はほとんど含めません。
# ---------------------------------------------------------------------------
$random = New-Object System.Random 1972
$heartCount = [int]($sampleRate * 0.62)
$heart = New-Object double[] $heartCount

function Add-Thump([double[]]$target, [double]$start, [double]$frequency, [double]$amplitude, [double]$decay) {
    $first = [int]($start * $sampleRate)
    for ($i = $first; $i -lt $target.Length; $i++) {
        $t = ($i - $first) / $sampleRate
        # 打った瞬間から音の高さが3割ほど下がって落ち着くと、柔らかい「ドッ」になります
        # 高さ f(t) = f * (0.7 + 0.3 * exp(-t / 0.05)) を時間で積分した位相です
        $phase = 2.0 * [Math]::PI * $frequency * (0.7 * $t + 0.015 * (1.0 - [Math]::Exp(-$t / 0.05)))
        $envelope = [Math]::Exp(-$t / $decay) * [Math]::Min(1.0, $t / 0.004)
        $target[$i] += $amplitude * $envelope * [Math]::Sin($phase)
    }
}

Add-Thump $heart 0.000 52.0 1.00 0.085   # 「ドッ」
Add-Thump $heart 0.000 96.0 0.28 0.040   # 「ドッ」の輪郭
Add-Thump $heart 0.235 64.0 0.68 0.070   # 「クン」
Add-Thump $heart 0.235 118.0 0.18 0.030

# 体の中を伝わるこもった雑音をわずかに足します（1次のローパスで高い音を除きます）
$noise = 0.0
for ($i = 0; $i -lt $heartCount; $i++) {
    $t = $i / $sampleRate
    $noise += (($random.NextDouble() * 2.0 - 1.0) - $noise) * 0.02
    $burst = [Math]::Exp(-$t / 0.05) + 0.7 * [Math]::Exp(-[Math]::Max(0.0, $t - 0.235) / 0.04) * [double]($t -ge 0.235)
    $heart[$i] += $noise * 0.9 * $burst
}

# 最後の0.1秒で消します
$fadeStart = $heartCount - [int](0.1 * $sampleRate)
for ($i = $fadeStart; $i -lt $heartCount; $i++) {
    $heart[$i] *= ($heartCount - $i) / ($heartCount - $fadeStart)
}
Write-MonoWav $heart "heartbeat.wav" 0.85

# ---------------------------------------------------------------------------
# 呼吸音: 口から漏れる息の雑音を、帯域を絞って「吐く（強め）→吸う（弱め）」の形に整えます。
# ---------------------------------------------------------------------------
$random = New-Object System.Random 1973
$breathSeconds = 2.1
$breathCount = [int]($sampleRate * $breathSeconds)
$breath = New-Object double[] $breathCount

# 2つのローパスの差で、およそ300Hz〜1800Hzの息らしい帯域だけを残します
$lowA = 0.0
$lowB = 0.0
for ($i = 0; $i -lt $breathCount; $i++) {
    $t = $i / $sampleRate
    $white = $random.NextDouble() * 2.0 - 1.0
    $lowA += ($white - $lowA) * 0.22
    $lowB += ($white - $lowB) * 0.04
    $band = $lowA - $lowB

    # 吐く: 0.05〜0.95秒、吸う: 1.15〜1.95秒。sinの2乗で滑らかに立ち上げて消します
    $exhale = 0.0
    if ($t -ge 0.05 -and $t -le 0.95) {
        $exhale = [Math]::Pow([Math]::Sin([Math]::PI * ($t - 0.05) / 0.90), 2.0)
    }
    $inhale = 0.0
    if ($t -ge 1.15 -and $t -le 1.95) {
        $inhale = 0.55 * [Math]::Pow([Math]::Sin([Math]::PI * ($t - 1.15) / 0.80), 2.0)
    }

    # 吐く息は少し低く太く、吸う息は細く高くするため、吸うときだけ高い成分を足します
    $breath[$i] = $band * $exhale + ($band * 0.6 + ($white - $lowA) * 0.25) * $inhale
}
Write-MonoWav $breath "breath.wav" 0.7
