# ============================================================================
# 作業報告用に、ゲームを自動で見て回って「動画（MP4）とスクリーンショット（PNG）」を保存する。
# 実行: powershell -ExecutionPolicy Bypass -File tools/capture-daily.ps1
#
# ・Release版の実行ファイルを --capture オプション付きで起動する（先にReleaseでビルドしておく）。
# ・ゲームのウィンドウは画面の外に置かれ、前面に出ず、音も出ません。キー入力も送らないので、
#   撮影中も他の作業を続けられる（2〜3分で終わる）。
# ・保存先: リポジトリの1つ上の daily_report\<今日の日付>\<撮影時刻>\
#   01〜11（と1面の天井の02b）のスクリーンショット、プレイ動画.mp4、capture_log.txt（撮影の経過）
# ============================================================================

$ErrorActionPreference = 'Stop'

$projectDirectory = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$repositoryRoot = Split-Path $projectDirectory -Parent
$executable = Join-Path $projectDirectory 'x64\Release\Job_application_work_horror.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf))
{
    throw "Release版の実行ファイルがありません。先にReleaseでビルドしてください: $executable"
}

$startedAt = Get-Date
$dayDirectory = Join-Path (Split-Path $repositoryRoot -Parent) ("daily_report\" + $startedAt.ToString("yyyy-MM-dd"))
$outputDirectory = Join-Path $dayDirectory $startedAt.ToString("HHmmss_fff")
New-Item -ItemType Directory -Path $dayDirectory -Force | Out-Null
# 再撮影の途中で止まっても、前回の素材と混ざらないよう毎回別の場所に保存する。
New-Item -ItemType Directory -Path $outputDirectory | Out-Null
$logPath = Join-Path $outputDirectory "capture_log.txt"
$timeoutMinutes = 10
$timeoutMilliseconds = [int][TimeSpan]::FromMinutes($timeoutMinutes).TotalMilliseconds

Write-Host "撮影中です（2〜3分）... 保存先: $outputDirectory"
$captureArguments = @('--capture', [string]::Format('"{0}"', $outputDirectory))
$process = Start-Process -FilePath $executable -ArgumentList $captureArguments `
    -WorkingDirectory $projectDirectory -WindowStyle Hidden -PassThru
try
{
    if (-not $process.WaitForExit($timeoutMilliseconds))
    {
        Stop-Process -InputObject $process -ErrorAction SilentlyContinue
        throw "撮影が $timeoutMinutes 分で完了しませんでした。ログ: $logPath"
    }
    if ($process.ExitCode -ne 0)
    {
        throw "撮影に失敗しました（終了コード $($process.ExitCode)）。ログ: $logPath"
    }
}
finally
{
    $process.Dispose()
}

$video = Get-Item -LiteralPath (Join-Path $outputDirectory "プレイ動画.mp4") -ErrorAction SilentlyContinue
$images = @(Get-ChildItem -LiteralPath $outputDirectory -Filter "*.png" -File | Where-Object Length -gt 0)
if ($null -eq $video -or $video.Length -le 0 -or $images.Count -eq 0)
{
    throw "プレイ動画またはスクリーンショットが出力されていません。ログ: $logPath"
}

Get-ChildItem -LiteralPath $outputDirectory | Select-Object Name, Length | Format-Table -AutoSize
Write-Host "完了しました。"
