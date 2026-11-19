# Joseph Sardin「Shoes in the Water」（BigSoundBank、CC0）のWAVから一歩ずつ切り出す。
# 入力: https://bigsoundbank.com/shoes-in-the-water-s0690.html のWAV
param([Parameter(Mandatory=$true)][string]$SourcePath)
$ErrorActionPreference = 'Stop'
$assetDir = Join-Path $PSScriptRoot '../assets/Audio'

. (Join-Path $PSScriptRoot "MonoPcmWave.ps1")
$attackFadeSeconds = 0.003

function Read-MonoPcm([string]$name) {
    $bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $name).Path)
    $ascii = [Text.Encoding]::ASCII
    if ($bytes.Length -lt $monoPcmRiffHeaderBytes -or $ascii.GetString($bytes, 0, $monoPcmChunkIdBytes) -ne "RIFF" -or
        $ascii.GetString($bytes, $monoPcmChunkHeaderBytes, $monoPcmChunkIdBytes) -ne "WAVE") {
        throw "$name はRIFF/WAVE形式のファイルではありません。"
    }
    $rate = 0
    for ($offset = $monoPcmRiffHeaderBytes; $offset + $monoPcmChunkHeaderBytes -le $bytes.Length; $offset += $monoPcmChunkHeaderBytes + $length + ($length % 2)) {
        $chunk = $ascii.GetString($bytes, $offset, $monoPcmChunkIdBytes)
        $length = [BitConverter]::ToInt32($bytes, $offset + $monoPcmChunkIdBytes)
        $start = $offset + $monoPcmChunkHeaderBytes
        if ($length -lt 0 -or $start + $length -gt $bytes.Length) { break }
        if ($chunk -eq 'fmt ' -and $length -ge $monoPcmFormatBytes) {
            if ([BitConverter]::ToInt16($bytes, $start) -ne $monoPcmFormatTag -or
                [BitConverter]::ToInt16($bytes, $start + 2) -ne $monoPcmChannelCount -or
                [BitConverter]::ToInt16($bytes, $start + 14) -ne $monoPcmSampleBits) {
                throw "$name はモノラル16ビットPCMが必要です。"
            }
            $rate = [BitConverter]::ToInt32($bytes, $start + 4)
        }
        if ($chunk -eq 'data' -and $rate -gt 0 -and $length % $monoPcmSampleBytes -eq 0) {
            $samples = New-Object 'int16[]' ($length / $monoPcmSampleBytes)
            [Buffer]::BlockCopy($bytes, $start, $samples, 0, $length)
            return @{ Rate = $rate; Samples = $samples }
        }
    }
    throw "$name の波形を読み込めません。"
}

$water = Read-MonoPcm $SourcePath
$sampleRate = $water.Rate
$clips = @(
    @{ Name = 'water_step_walk.wav'; Start = 0.797; Duration = 0.32; Fade = 0.05; Peak = 0.72 }
    @{ Name = 'water_step_run.wav'; Start = 5.255; Duration = 0.29; Fade = 0.04; Peak = 0.83 }
)
foreach ($clip in $clips) {
    $start = [int]($clip.Start * $sampleRate)
    $count = [int]($clip.Duration * $sampleRate)
    if ($start + $count -gt $water.Samples.Length) { throw '元の録音が短すぎます。' }
    $peak = 0
    for ($i = 0; $i -lt $count; $i++) {
        $peak = [Math]::Max($peak, [Math]::Abs([int]$water.Samples[$start + $i]))
    }
    if ($peak -eq 0) { throw '切り出した区間が無音です。' }
    $gain = $monoPcmSamplePeak * $clip.Peak / $peak
    $writer = New-Object IO.BinaryWriter ([IO.File]::Create((Join-Path $assetDir $clip.Name)))
    try {
        Write-MonoPcm16Header $writer $sampleRate $count
        for ($i = 0; $i -lt $count; $i++) {
            $t = $i / $sampleRate
            $envelope = [Math]::Min(1.0, [Math]::Min($t / $attackFadeSeconds, ($count - 1 - $i) / ($sampleRate * $clip.Fade)))
            $writer.Write([int16][Math]::Round($water.Samples[$start + $i] * $gain * $envelope))
        }
    }
    finally {
        $writer.Dispose()
    }
}
