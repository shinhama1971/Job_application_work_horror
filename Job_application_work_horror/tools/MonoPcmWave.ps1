# 音声生成・切り出しで共通のモノラル16ビットPCM形式。
$monoPcmRiffHeaderBytes = 12
$monoPcmChunkIdBytes = 4
$monoPcmChunkHeaderBytes = 8
$monoPcmFormatBytes = 16
$monoPcmFormatTag = 1
$monoPcmChannelCount = 1
$monoPcmSampleBytes = 2
$monoPcmSampleBits = 16
$monoPcmSamplePeak = 32767.0

function Write-MonoPcm16Header([IO.BinaryWriter]$writer, [int]$sampleRate, [int]$sampleCount) {
    $ascii = [Text.Encoding]::ASCII
    $dataBytes = $sampleCount * $monoPcmSampleBytes
    $formatAndHeaders = $monoPcmChunkIdBytes + $monoPcmChunkHeaderBytes * 2 + $monoPcmFormatBytes
    $writer.Write($ascii.GetBytes("RIFF"))
    $writer.Write([int]($formatAndHeaders + $dataBytes))
    $writer.Write($ascii.GetBytes("WAVEfmt "))
    $writer.Write([int]$monoPcmFormatBytes)
    $writer.Write([int16]$monoPcmFormatTag)
    $writer.Write([int16]$monoPcmChannelCount)
    $writer.Write([int]$sampleRate)
    $writer.Write([int]($sampleRate * $monoPcmSampleBytes))
    $writer.Write([int16]$monoPcmSampleBytes)
    $writer.Write([int16]$monoPcmSampleBits)
    $writer.Write($ascii.GetBytes("data"))
    $writer.Write([int]$dataBytes)
}
