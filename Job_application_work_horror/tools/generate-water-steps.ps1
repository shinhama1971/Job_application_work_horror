# Joseph Sardin「Shoes in the Water」（BigSoundBank、CC0）のWAVから一歩ずつ切り出す。
# 入力: https://bigsoundbank.com/shoes-in-the-water-s0690.html のWAV
param([Parameter(Mandatory=$true)][string]$SourcePath)
$ErrorActionPreference = 'Stop'
$assetDir = Join-Path $PSScriptRoot '../assets/Audio'

function Read-MonoPcm([string]$name) {
    $bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $name).Path)
    $rate = 0
    for ($offset = 12; $offset + 8 -le $bytes.Length; $offset += 8 + $length + ($length % 2)) {
        $chunk = [Text.Encoding]::ASCII.GetString($bytes, $offset, 4)
        $length = [BitConverter]::ToInt32($bytes, $offset + 4)
        $start = $offset + 8
        if ($length -lt 0 -or $start + $length -gt $bytes.Length) { break }
        if ($chunk -eq 'fmt ' -and $length -ge 16) {
            if ([BitConverter]::ToInt16($bytes, $start) -ne 1 -or
                [BitConverter]::ToInt16($bytes, $start + 2) -ne 1 -or
                [BitConverter]::ToInt16($bytes, $start + 14) -ne 16) {
                throw "$name はモノラル16ビットPCMが必要です。"
            }
            $rate = [BitConverter]::ToInt32($bytes, $start + 4)
        }
        if ($chunk -eq 'data' -and $rate -gt 0 -and $length % 2 -eq 0) {
            $samples = New-Object 'int16[]' ($length / 2)
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
    $gain = 32767.0 * $clip.Peak / $peak
    $writer = New-Object IO.BinaryWriter ([IO.File]::Create((Join-Path $assetDir $clip.Name)))
    try {
        $writer.Write([Text.Encoding]::ASCII.GetBytes('RIFF'))
        $writer.Write([int](36 + $count * 2))
        $writer.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt '))
        $writer.Write([int]16)
        $writer.Write([int16]1)
        $writer.Write([int16]1)
        $writer.Write([int]$sampleRate)
        $writer.Write([int]($sampleRate * 2))
        $writer.Write([int16]2)
        $writer.Write([int16]16)
        $writer.Write([Text.Encoding]::ASCII.GetBytes('data'))
        $writer.Write([int]($count * 2))
        for ($i = 0; $i -lt $count; $i++) {
            $t = $i / $sampleRate
            $envelope = [Math]::Min(1.0, [Math]::Min($t / 0.003, ($count - 1 - $i) / ($sampleRate * $clip.Fade)))
            $writer.Write([int16][Math]::Round($water.Samples[$start + $i] * $gain * $envelope))
        }
    }
    finally {
        $writer.Dispose()
    }
}
