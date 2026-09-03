# 포트폴리오 PDF 재생성
#
#   powershell -ExecutionPolicy Bypass -File portfolio\build.ps1
#
# portfolio.html을 고친 뒤 이 스크립트를 돌리면 루트의
# MMORPG-with-IOCP_포트폴리오.pdf가 다시 만들어진다.
#
# PDF를 직접 편집하는 대신 항상 portfolio.html을 원본으로 두는 이유:
# PDF만 남아 있으면 나중에 내용을 고칠 때 텍스트를 다시 추출해야 하는데,
# 한글은 폰트 임베딩 방식 때문에 추출이 깨져 사실상 처음부터 다시 만들어야 한다.

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$html = Join-Path $PSScriptRoot "portfolio.html"
$temp = Join-Path $PSScriptRoot "portfolio.pdf"
$out  = Join-Path $root "MMORPG-with-IOCP_포트폴리오.pdf"

if (-not (Test-Path $html)) { throw "원본을 찾을 수 없습니다: $html" }

# Chrome이 없으면 Edge로 대체 (둘 다 동일한 렌더링 엔진)
$browser = @(
    "${env:ProgramFiles}\Google\Chrome\Application\chrome.exe",
    "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
    "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
    "${env:ProgramFiles}\Microsoft\Edge\Application\msedge.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $browser) { throw "Chrome 또는 Edge를 찾을 수 없습니다." }

if (Test-Path $temp) { Remove-Item $temp }

$uri = ([System.Uri]$html).AbsoluteUri

# 2>&1로 stderr를 받지 않는다 — Windows PowerShell 5.1은 네이티브 프로그램의
# stderr 한 줄 한 줄을 오류로 승격시켜서, Chrome이 무해한 경고만 찍어도
# ErrorActionPreference="Stop" 때문에 스크립트가 중단된다.
# --log-level=3으로 잡음을 줄이고, 성공 여부는 아래에서 파일 존재로 판단한다.
$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $browser --headless --disable-gpu --no-pdf-header-footer --log-level=3 `
           --print-to-pdf="$temp" --virtual-time-budget=8000 $uri | Out-Null
$ErrorActionPreference = $prevEAP

# 렌더링이 끝나 파일이 완성될 때까지 잠깐 기다린다
$waited = 0
while (-not (Test-Path $temp) -and $waited -lt 30) { Start-Sleep -Seconds 1; $waited++ }
if (-not (Test-Path $temp)) { throw "PDF 생성에 실패했습니다." }
Start-Sleep -Seconds 2

Move-Item $temp $out -Force
Write-Host ("완료: {0} ({1} KB)" -f $out, [math]::Round((Get-Item $out).Length / 1KB, 1))
