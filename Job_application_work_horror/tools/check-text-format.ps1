param([Parameter(Mandatory=$true)][string[]]$Path, [ValidateSet('Any','CRLF','LF')][string]$Expected='Any')
$ErrorActionPreference='Stop'
$taskFailures=0
$taskUtf8=New-Object System.Text.UTF8Encoding($false,$true)
foreach($taskPath in $Path){
 $taskFile=Get-Item -LiteralPath $taskPath
 if($taskFile.PSIsContainer){throw 'ファイルを明示してください。ディレクトリを自動で書き換える機能はありません。'}
 if($taskFile.Extension -notin @('.h','.cpp','.hlsl','.hlsli','.vcxproj','.filters','.ps1','.md','.txt','.slnx')){Write-Output "SKIP type: $($taskFile.Name)";continue}
 $taskBytes=[IO.File]::ReadAllBytes($taskFile.FullName)
 # 名前がコードでも、画像やNULを含むデータをテキストとして扱わない。読み取り専用。
 if(($taskBytes.Length -ge 4 -and (($taskBytes[0] -eq 137 -and $taskBytes[1] -eq 80 -and $taskBytes[2] -eq 78 -and $taskBytes[3] -eq 71) -or ($taskBytes[0] -eq 255 -and $taskBytes[1] -eq 216))) -or $taskBytes.Contains([byte]0)){Write-Output "ERROR unexpected binary: $($taskFile.Name)";$taskFailures++;continue}
 try{$taskText=$taskUtf8.GetString($taskBytes)}catch{Write-Output "ERROR UTF-8: $($taskFile.Name)";$taskFailures++;continue}
 $taskCrLf=([regex]::Matches($taskText,"`r`n")).Count
 $taskBareLf=([regex]::Matches($taskText,"(?<!`r)`n")).Count
 $taskBareCr=([regex]::Matches($taskText,"`r(?!`n)")).Count
 $taskBom=$taskBytes.Length -ge 3 -and $taskBytes[0] -eq 239 -and $taskBytes[1] -eq 187 -and $taskBytes[2] -eq 191
 $taskPolicy=if($taskFile.Extension -eq '.vcxproj'){'CRLF'}else{$Expected}
 $taskBad=($taskBareCr -gt 0) -or ($taskCrLf -gt 0 -and $taskBareLf -gt 0) -or ($taskPolicy -eq 'CRLF' -and $taskBareLf -gt 0) -or ($taskPolicy -eq 'LF' -and $taskCrLf -gt 0) -or ($taskBom -and $taskFile.Extension -in @('.hlsl','.hlsli'))
 if($taskBad){$taskFailures++}
 [pscustomobject]@{File=$taskFile.FullName;CRLF=$taskCrLf;LF=$taskBareLf;BOM=$taskBom;Passed=(-not $taskBad)}
}
if($taskFailures -gt 0){exit 1}
