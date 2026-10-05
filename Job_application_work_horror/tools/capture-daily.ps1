# ============================================================================
# 作業報告用に、ゲームを自動で見て回って「動画（MP4）とスクリーンショット（PNG）」を保存します。
# 実行: powershell -ExecutionPolicy Bypass -File tools/capture-daily.ps1
#
# ・Release版の実行ファイルを --capture オプション付きで起動します（先にReleaseでビルドしてください）。
# ・ゲームのウィンドウは画面の外に置かれ、前面に出ず、音も出ません。キー入力も送らないので、
#   撮影中も他の作業を続けられます（2〜3分で終わります）。
# ・保存先: リポジトリの1つ上の daily_report\<今日の日付>\
#   01〜11（と1面の天井の02b）のスクリーンショット、プレイ動画.mp4、capture_log.txt（撮影の経過）
# ============================================================================

$ErrorActionPreference = 'Stop'

$projectDirectory = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$repositoryRoot = Split-Path $projectDirectory -Parent
$executable = Join-Path $projectDirectory 'x64\Release\Job_application_work_horror.exe'
if (-not (Test-Path $executable))
{
    throw "Release版の実行ファイルがありません。先にReleaseでビルドしてください: $executable"
}

$outputDirectory = Join-Path (Split-Path $repositoryRoot -Parent) ('daily_report\' + (Get-Date -Format 'yyyy-MM-dd'))
New-Item -ItemType Directory -Force $outputDirectory | Out-Null
$logPath = Join-Path $outputDirectory 'capture_log.txt'
if (Test-Path $logPath)
{
    Clear-Content $logPath
}

Write-Host "撮影中です（2〜3分）... 保存先: $outputDirectory"
$process = Start-Process $executable -ArgumentList @('--capture', "`"$outputDirectory`"") `
    -WorkingDirectory $projectDirectory -PassThru
if (-not $process.WaitForExit(600000))
{
    Stop-Process $process.Id
    throw '10分たっても終わらなかったため中断しました。capture_log.txt を確認してください。'
}

Get-ChildItem $outputDirectory | Select-Object Name, Length | Format-Table -AutoSize
Write-Host '完了しました。'
