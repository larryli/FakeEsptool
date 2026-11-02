# gen_toolbar32.ps1 - Generate a 32x32 high-DPI toolbar bitmap strip
#
# Reads a 4bpp indexed-colour toolbar bitmap (16x16 icons laid out
# horizontally) and writes a 2x nearest-neighbour upscaled version
# (32x32 icons) while preserving the exact 4bpp pixel format and the
# colour palette.
#
# The background colour 0xC0C0C0 lives in the palette as a single
# index. Because the upscale copies palette indices verbatim, that
# index is preserved, so LR_LOADMAP3DCOLORS / 3D face colour mapping
# remains valid on the upscaled bitmap (no 24/32bpp conversion, no
# palette loss, no colour shift).
#
# Usage:
#   powershell -File tools/gen_toolbar32.ps1 [<in.bmp> [<out.bmp>]]

$root = Split-Path -Parent $PSScriptRoot
$srcPath = if ($args.Count -ge 1) { $args[0] } else { Join-Path $root 'src/res/toolbar.bmp' }
$outPath = if ($args.Count -ge 2) { $args[1] } else { Join-Path $root 'src/res/toolbar32.bmp' }

$src = [IO.File]::ReadAllBytes($srcPath)

$w = $src[18] + $src[19] * 256
$h = $src[22] + $src[23] * 256
$bpp = $src[28] + $src[29] * 256
if ($bpp -ne 4) { throw "expected 4bpp, got $bpp" }

$off = $src[10] + $src[11] * 256 + $src[12] * 65536 + $src[13] * 16777216

function RowBytes($width) {
    $rb = [math]::Ceiling($width / 2.0)
    $pad = (4 - ($rb % 4)) % 4
    return $rb + $pad
}
$srcRow = RowBytes $w
$dstRow = RowBytes ($w * 2)

function GetPx($x, $sBase) {
    $b = $src[$sBase + ($x -shr 1)]
    if (($x -band 1) -eq 0) { return ($b -shr 4) -band 0xF }
    else { return $b -band 0xF }
}

$ow = $w * 2
$oh = $h * 2
$px = New-Object int[] ($oh * $ow)
for ($Y = 0; $Y -lt $oh; $Y++) {
    $sy = $Y -shr 1
    $sBase = $off + $sy * $srcRow
    for ($X = 0; $X -lt $ow; $X++) {
        $sx = $X -shr 1
        $px[$Y * $ow + $X] = GetPx $sx $sBase
    }
}

$bihSize = $off
$fileSize = $bihSize + $dstRow * $oh
$outb = New-Object byte[] $fileSize
[Array]::Copy($src, 0, $outb, 0, $off)
$outb[18] = $ow -band 0xFF; $outb[19] = ($ow -shr 8) -band 0xFF
$outb[22] = $oh -band 0xFF; $outb[23] = ($oh -shr 8) -band 0xFF
$outb[2] = $fileSize -band 0xFF; $outb[3] = ($fileSize -shr 8) -band 0xFF
$outb[4] = ($fileSize -shr 16) -band 0xFF; $outb[5] = ($fileSize -shr 24) -band 0xFF

for ($Y = 0; $Y -lt $oh; $Y++) {
    $dBase = $off + $Y * $dstRow
    for ($X = 0; $X -lt $ow; $X += 2) {
        $hi = $px[$Y * $ow + $X]
        $lo = $px[$Y * $ow + $X + 1]
        $outb[$dBase + ($X -shr 1)] = (($hi -shl 4) -bor ($lo -band 0xF))
    }
}

[IO.File]::WriteAllBytes($outPath, $outb)
[Console]::WriteLine("OK wrote $outPath size=$fileSize (${ow} x ${oh})")
