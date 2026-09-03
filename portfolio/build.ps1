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


# ─────────────────────────────────────────────────────────────────────────────
# README에 넣는 페이지 이미지(portfolio/pages/portfolio-NN.png)도 함께 갱신한다.
#
# PDF를 이미지로 변환하지 않고 HTML을 페이지 단위로 다시 렌더링하는 이유:
# PDF -> PNG 변환에는 poppler 같은 외부 도구가 필요한데, 어차피 같은 Chrome으로
# 낱장을 직접 찍으면 의존성 없이 더 선명한 결과가 나온다.
# ─────────────────────────────────────────────────────────────────────────────

$pagesDir = Join-Path $PSScriptRoot "pages"
$workDir  = Join-Path $env:TEMP ("portfolio-pages-" + [guid]::NewGuid().ToString("N").Substring(0, 8))

New-Item -ItemType Directory -Force -Path $pagesDir | Out-Null
New-Item -ItemType Directory -Force -Path $workDir  | Out-Null

try {
    $lines = Get-Content $html -Encoding UTF8

    # <style> 블록까지가 모든 페이지가 공유하는 머리말이다.
    $styleEnd = ($lines | Select-String -Pattern '^</style>' | Select-Object -First 1).LineNumber
    if (-not $styleEnd) { throw "portfolio.html에서 </style>을 찾지 못했습니다." }
    $header = $lines[0..($styleEnd - 1)]

    # 페이지 하나씩 잘라 낱장 HTML로 쓴다. 각 페이지는 열림이 '<div class="page'로,
    # 닫힘이 열 0의 '</div>'로 끝나는 형태다(포트폴리오 전체가 이 규칙을 지킨다).
    $pageFiles = New-Object System.Collections.Generic.List[string]
    $current   = $null
    foreach ($line in $lines) {
        if ($line -match '^<div class="page') {
            $current = New-Object System.Collections.Generic.List[string]
            $current.AddRange([string[]]$header)
        }
        if ($current -ne $null) {
            $current.Add($line)
            if ($line -match '^</div>\s*$') {
                $name = "pg_{0:d2}.html" -f ($pageFiles.Count + 1)
                $path = Join-Path $workDir $name
                Set-Content -Path $path -Value $current -Encoding UTF8
                $pageFiles.Add($path)
                $current = $null
            }
        }
    }
    if ($pageFiles.Count -eq 0) { throw "페이지를 하나도 찾지 못했습니다." }

    # 이전 실행보다 페이지가 줄어들었을 때 옛 이미지가 남지 않도록 먼저 비운다.
    Remove-Item (Join-Path $pagesDir "portfolio-*.png") -ErrorAction SilentlyContinue

    # 위 PDF 생성부와 같은 이유로 stderr를 오류로 승격시키지 않는다.
    $prevEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    for ($i = 0; $i -lt $pageFiles.Count; $i++) {
        $png = Join-Path $pagesDir ("portfolio-{0:d2}.png" -f ($i + 1))
        $pageUri = ([System.Uri]$pageFiles[$i]).AbsoluteUri
        # 1280x720 레이아웃을 2배로 찍어 2560x1440 — GitHub에서 고해상도로 보인다.
        & $browser --headless --disable-gpu --log-level=3 --hide-scrollbars `
                   --force-device-scale-factor=2 --window-size=1280,720 `
                   --screenshot="$png" --virtual-time-budget=5000 $pageUri | Out-Null
        Start-Sleep -Milliseconds 400
        if (-not (Test-Path $png)) { throw ("페이지 이미지 생성에 실패했습니다: {0}" -f $png) }
    }
    $ErrorActionPreference = $prevEAP

    $totalKB = (Get-ChildItem (Join-Path $pagesDir "portfolio-*.png") | Measure-Object -Property Length -Sum).Sum / 1KB
    Write-Host ("완료: {0} ({1}장, {2} MB)" -f $pagesDir, $pageFiles.Count, [math]::Round($totalKB / 1024, 2))
}
finally {
    Remove-Item $workDir -Recurse -Force -ErrorAction SilentlyContinue
}
