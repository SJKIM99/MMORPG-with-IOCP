# world/manifest.json 에 등록된 에셋이 assets/vendor/ 에 실제로 존재하는지 검증한다.
#
# .gltf 는 .bin 과 .png 를 외부 파일로 참조하므로 파일 하나만 있다고 되는 게 아니다.
# 벤더 에셋은 .gitignore 대상이라 클론 직후에는 비어 있고, 여기서 무엇이 빠졌는지 알려준다.
#
# CLAUDE.md 2장의 툴체인은 Python 3 이지만, 이 스크립트는 클론 직후
# Python 설치 전에도 돌아야 하므로 PowerShell 로 작성한다.
# 배치 생성기(tools/gen_layout.py)와 내비메시 빌드는 규약대로 Python 을 쓴다.
#
# 사용법:  powershell -ExecutionPolicy Bypass -File tools\verify_assets.ps1

$ErrorActionPreference = 'Stop'

$repoRoot    = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $repoRoot 'world\manifest.json'

if (-not (Test-Path $manifestPath)) {
    Write-Host "manifest 를 찾을 수 없습니다: $manifestPath" -ForegroundColor Red
    exit 1
}

$manifest = Get-Content $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json

# PSCustomObject 의 .PSObject.Properties.Count 는 속성별 Count 배열을 돌려주므로
# 개수를 셀 때는 반드시 @() 로 감싼 뒤 .Count 를 읽는다.
$sourceProps = @($manifest.sources.PSObject.Properties)
$animProps   = @($manifest.animation_libraries.PSObject.Properties | Where-Object { -not $_.Name.StartsWith('$') })

$roots = @{}
foreach ($src in $sourceProps) {
    $roots[$src.Name] = $src.Value.root
}

$missingAssets = New-Object System.Collections.Generic.List[string]
$missingDeps   = New-Object System.Collections.Generic.List[string]
$assetCount = 0
$depCount   = 0

function Resolve-AssetPath {
    param($SourceName, $RelFile)
    return Join-Path $repoRoot (Join-Path $roots[$SourceName] $RelFile)
}

# --- 에셋 본체 ---
foreach ($entry in $manifest.assets.PSObject.Properties) {
    $assetCount++
    $asset = $entry.Value
    $full  = Resolve-AssetPath $asset.source $asset.file

    if (-not (Test-Path $full)) {
        $missingAssets.Add("$($entry.Name)  ->  $($roots[$asset.source])/$($asset.file)")
        continue
    }

    # .gltf 는 buffers/images 를 외부 파일로 참조한다. .glb 는 자체 포함이라 건너뛴다.
    if ($asset.file -notlike '*.gltf') { continue }

    $dir  = Split-Path $full
    $gltf = Get-Content $full -Raw | ConvertFrom-Json

    $uris = @()
    if ($gltf.buffers) { $uris += $gltf.buffers.uri }
    if ($gltf.images)  { $uris += $gltf.images.uri }

    foreach ($uri in ($uris | Where-Object { $_ -and $_ -notlike 'data:*' })) {
        $depCount++
        $dep = Join-Path $dir ([Uri]::UnescapeDataString($uri))
        if (-not (Test-Path $dep)) {
            $missingDeps.Add("$($entry.Name)  ->  $uri")
        }
    }
}

# --- 애니메이션 라이브러리 ---
$missingAnims = New-Object System.Collections.Generic.List[string]
foreach ($lib in $animProps) {
    $full = Resolve-AssetPath $lib.Value.source $lib.Value.file
    if (-not (Test-Path $full)) {
        $missingAnims.Add("$($lib.Name)  ->  $($roots[$lib.Value.source])/$($lib.Value.file)")
    }
}

# --- 라이선스 파일 ---
$missingLicenses = New-Object System.Collections.Generic.List[string]
foreach ($src in $sourceProps) {
    $lf = Join-Path $repoRoot $src.Value.license_file
    if (-not (Test-Path $lf)) {
        $missingLicenses.Add("$($src.Name)  ->  $($src.Value.license_file)")
    }
}

# --- 결과 ---
Write-Host ""
Write-Host "에셋            $($assetCount - $missingAssets.Count) / $assetCount"
Write-Host "외부 의존성     $($depCount - $missingDeps.Count) / $depCount   (.gltf 가 참조하는 .bin / .png)"
Write-Host "애니메이션      $($animProps.Count - $missingAnims.Count) / $($animProps.Count)"
Write-Host "라이선스        $($sourceProps.Count - $missingLicenses.Count) / $($sourceProps.Count)"
Write-Host ""

$allMissing = $missingAssets.Count + $missingDeps.Count + $missingAnims.Count + $missingLicenses.Count

if ($allMissing -eq 0) {
    Write-Host "모두 정상입니다." -ForegroundColor Green
    exit 0
}

foreach ($pair in @(
    @{ Title = '없는 에셋';          Items = $missingAssets },
    @{ Title = '없는 외부 의존성';   Items = $missingDeps },
    @{ Title = '없는 애니메이션';    Items = $missingAnims },
    @{ Title = '없는 라이선스 파일'; Items = $missingLicenses }
)) {
    if ($pair.Items.Count -gt 0) {
        Write-Host "[$($pair.Title)]" -ForegroundColor Yellow
        $pair.Items | ForEach-Object { Write-Host "  $_" }
        Write-Host ""
    }
}

Write-Host "README.md 의 '3D 에셋 받기' 를 참고해 해당 팩을 받아 지정된 경로에 푸세요." -ForegroundColor Yellow
exit 1
