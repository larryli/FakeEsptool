# verify_toolbar32.ps1 - Verify the generated 32x32 toolbar bitmap
#
# Checks: 2x dimensions, matching bpp, identical palette, presence of
# the 0xC0C0C0 background palette index, and correct nearest-neighbour
# upscaling of every pixel.
#
# Usage:
#   powershell -File tools/verify_toolbar32.ps1 [<in.bmp> [<out.bmp>]]

$root = Split-Path -Parent $PSScriptRoot
$srcPath = if ($args.Count -ge 1) { $args[0] } else { Join-Path $root 'src/res/toolbar.bmp' }
$outPath = if ($args.Count -ge 2) { $args[1] } else { Join-Path $root 'src/res/toolbar32.bmp' }

$src = [IO.File]::ReadAllBytes($srcPath)
$dst = [IO.File]::ReadAllBytes($outPath)

$sw = $src[18] + $src[19] * 256; $sh = $src[22] + $src[23] * 256
$dw = $dst[18] + $dst[19] * 256; $dh = $dst[22] + $dst[23] * 256
$sbpp = $src[28] + $src[29] * 256; $dbpp = $dst[28] + $dst[29] * 256
$soff = $src[10] + $src[11] * 256 + $src[12] * 65536 + $src[13] * 16777216
$doff = $dst[10] + $dst[11] * 256 + $dst[12] * 65536 + $dst[13] * 16777216

[Console]::WriteLine("src ${sw}x${sh} bpp=$sbpp  dst ${dw}x${dh} bpp=$dbpp")
[Console]::WriteLine("dims 2x: $(($dw -eq $sw * 2) -and ($dh -eq $sh * 2))  bppMatch: $($dbpp -eq $sbpp)")

$palOk = $true
for ($i = 54; $i -lt $soff; $i++) {
    if ($src[$i] -ne $dst[$i]) { $palOk = $false; break }
}
[Console]::WriteLine("palette identical: $palOk")

$bg = -1
for ($i = 0; $i -lt ($soff - 54) / 4; $i++) {
    $b = $src[54 + $i * 4]; $g = $src[55 + $i * 4]; $r = $src[56 + $i * 4]
    if ($r -eq 0xC0 -and $g -eq 0xC0 -and $b -eq 0xC0) { $bg = $i; break }
}
[Console]::WriteLine("bg 0xC0C0C0 palette index: $bg")

function RowBytes($width) { $rb = [math]::Ceiling($width / 2.0); $pad = (4 - ($rb % 4)) % 4; $rb + $pad }
$sr = RowBytes $sw; $dr = RowBytes $dw
function GetSrc($x, $y) { $b = $src[$soff + $y * $sr + ($x -shr 1)]; if (($x -band 1) -eq 0) { ($b -shr 4) -band 0xF } else { $b -band 0xF } }
function GetDst($x, $y) { $b = $dst[$doff + $y * $dr + ($x -shr 1)]; if (($x -band 1) -eq 0) { ($b -shr 4) -band 0xF } else { $b -band 0xF } }

$err = 0
for ($Y = 0; $Y -lt $dh; $Y++) { $sy = $Y -shr 1
    for ($X = 0; $X -lt $dw; $X++) { $sx = $X -shr 1
        if ((GetDst $X $Y) -ne (GetSrc $sx $sy)) { $err++; if ($err -le 5) { [Console]::WriteLine("mismatch at $X,$Y") } }
    }
}
[Console]::WriteLine("nearest-neighbor mismatches: $err")
if (($dw -ne $sw * 2) -or ($dh -ne $sh * 2) -or ($dbpp -ne $sbpp) -or (-not $palOk) -or ($bg -lt 0) -or ($err -ne 0)) {
    [Console]::WriteLine("VERIFY_FAILED")
    exit 1
}
[Console]::WriteLine("VERIFY_OK")
