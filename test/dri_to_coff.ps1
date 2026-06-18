# tools/dri_to_coff.ps1
#
# Builds a minimal, fully-linked 68K COFF executable from an absolute
# DRI c.out (`.68K`) file.  Used only as a test fixture for `mkdri`'s
# Phase 2 COFF reader, since the GNU `ld` in this workspace is not
# compiled with COFF output support.
#
# Output COFF layout:
#   FILHDR     (20 bytes)        magic=0x0150 (MC68KWRMAGIC)
#                                f_flags = F_EXEC|F_RELFLG|F_LNNO|F_LSYMS
#                                f_nscns = 3, f_opthdr = 28
#   AOUTHDR    (28 bytes)        magic=0x10B (F_NORMAL_EXECUTABLE)
#                                tsize, dsize, bsize, entry,
#                                text_start, data_start
#   SCNHDR x3  (3 * 40 bytes)    .text, .data, .bss
#   text bytes
#   data bytes
#
# Usage:
#   .\dri_to_coff.ps1 -InputDri foo.68K -OutputCoff foo.coff
#

param(
    [Parameter(Mandatory=$true)][string]$InputDri,
    [Parameter(Mandatory=$true)][string]$OutputCoff
)

$ErrorActionPreference = "Stop"

# ----- BE helpers -----

function Write-BE16 {
    param([System.IO.BinaryWriter]$Writer, [uint16]$Value)
    $Writer.Write([byte](($Value -shr 8) -band 0xFF))
    $Writer.Write([byte]($Value -band 0xFF))
}

function Write-BE32 {
    param([System.IO.BinaryWriter]$Writer, [uint32]$Value)
    $Writer.Write([byte](($Value -shr 24) -band 0xFF))
    $Writer.Write([byte](($Value -shr 16) -band 0xFF))
    $Writer.Write([byte](($Value -shr  8) -band 0xFF))
    $Writer.Write([byte]($Value -band 0xFF))
}

function Read-BE16 { param([byte[]]$Buf, [int]$Pos)
    return ([uint16]$Buf[$Pos] -shl 8) -bor [uint16]$Buf[$Pos+1]
}
function Read-BE32 { param([byte[]]$Buf, [int]$Pos)
    return ([uint32]$Buf[$Pos] -shl 24) -bor `
           ([uint32]$Buf[$Pos+1] -shl 16) -bor `
           ([uint32]$Buf[$Pos+2] -shl 8)  -bor `
           [uint32]$Buf[$Pos+3]
}

# ----- Parse DRI c.out header -----

$dri = [System.IO.File]::ReadAllBytes($InputDri)
$magic = Read-BE16 $dri 0
if ($magic -ne 0x601A -and $magic -ne 0x601B) {
    throw "Input is not a DRI c.out (magic=0x$('{0:X4}' -f $magic))"
}
$tsize    = Read-BE32 $dri 2
$dsize    = Read-BE32 $dri 6
$bsize    = Read-BE32 $dri 10
$ssize    = Read-BE32 $dri 14
$stksize  = Read-BE32 $dri 18
$entry    = Read-BE32 $dri 22
$rlbflg   = Read-BE16 $dri 26
$dstart   = $entry + $tsize
$bstart   = $dstart + $dsize
$hdrSize  = 28
if ($magic -eq 0x601B) {
    $dstart  = Read-BE32 $dri 28
    $bstart  = Read-BE32 $dri 32
    $hdrSize = 36
}
$textBytes = $dri[$hdrSize..($hdrSize + $tsize - 1)]
$dataBytes = @()
if ($dsize -gt 0) {
    $dataBytes = $dri[($hdrSize + $tsize)..($hdrSize + $tsize + $dsize - 1)]
}

Write-Host ("Input DRI: tsize=0x{0:X} dsize=0x{1:X} bsize=0x{2:X} entry=0x{3:X}" -f `
            $tsize, $dsize, $bsize, $entry)

# ----- Emit COFF -----

# COFF constants
$MC68KWRMAGIC      = 0x0150
$F_RELFLG          = 0x0001
$F_EXEC            = 0x0002
$F_LNNO            = 0x0004
$F_LSYMS           = 0x0008
$AOUT_MAGIC_EXEC   = 0x010B
$STYP_TEXT         = 0x00000020
$STYP_DATA         = 0x00000040
$STYP_BSS          = 0x00000080

$nscns         = 3
$filhdrSize    = 20
$aouthdrSize   = 28
$scnhdrSize    = 40
$scnTableOff   = $filhdrSize + $aouthdrSize     # 48
$scnTableSize  = $scnhdrSize * $nscns           # 120
$rawDataOff    = $scnTableOff + $scnTableSize   # 168
$textOff       = $rawDataOff                    # 168
$dataOff       = if ($dsize -gt 0) { $textOff + $tsize } else { 0 }

$stream = [System.IO.File]::Open($OutputCoff,
    [System.IO.FileMode]::Create,
    [System.IO.FileAccess]::Write)
$w = New-Object System.IO.BinaryWriter($stream)
try {
    # ----- FILHDR -----
    Write-BE16 $w $MC68KWRMAGIC
    Write-BE16 $w $nscns
    Write-BE32 $w 0                            # f_timdat (don't care)
    Write-BE32 $w 0                            # f_symptr (no syms)
    Write-BE32 $w 0                            # f_nsyms
    Write-BE16 $w $aouthdrSize
    Write-BE16 $w ([uint16]($F_EXEC -bor $F_RELFLG -bor $F_LNNO -bor $F_LSYMS))

    # ----- AOUTHDR -----
    Write-BE16 $w $AOUT_MAGIC_EXEC
    Write-BE16 $w 0                            # vstamp
    Write-BE32 $w $tsize
    Write-BE32 $w $dsize
    Write-BE32 $w $bsize
    Write-BE32 $w $entry
    Write-BE32 $w $entry                        # text_start
    Write-BE32 $w $dstart                       # data_start

    # ----- SCNHDR .text -----
    $textName = [byte[]](0x2E,0x74,0x65,0x78,0x74,0,0,0)    # ".text\0\0\0"
    $w.Write($textName, 0, 8)
    Write-BE32 $w $entry                        # s_paddr
    Write-BE32 $w $entry                        # s_vaddr
    Write-BE32 $w $tsize                        # s_size
    Write-BE32 $w ([uint32]$textOff)
    Write-BE32 $w 0                            # s_relptr
    Write-BE32 $w 0                            # s_lnnoptr
    Write-BE16 $w 0                            # s_nreloc
    Write-BE16 $w 0                            # s_nlnno
    Write-BE32 $w $STYP_TEXT

    # ----- SCNHDR .data -----
    $dataName = [byte[]](0x2E,0x64,0x61,0x74,0x61,0,0,0)    # ".data\0\0\0"
    $w.Write($dataName, 0, 8)
    Write-BE32 $w $dstart                       # s_paddr
    Write-BE32 $w $dstart                       # s_vaddr
    Write-BE32 $w $dsize                        # s_size
    Write-BE32 $w ([uint32]$dataOff)
    Write-BE32 $w 0
    Write-BE32 $w 0
    Write-BE16 $w 0
    Write-BE16 $w 0
    Write-BE32 $w $STYP_DATA

    # ----- SCNHDR .bss -----
    $bssName = [byte[]](0x2E,0x62,0x73,0x73,0,0,0,0)        # ".bss\0\0\0\0"
    $w.Write($bssName, 0, 8)
    Write-BE32 $w $bstart                       # s_paddr
    Write-BE32 $w $bstart                       # s_vaddr
    Write-BE32 $w $bsize                        # s_size
    Write-BE32 $w 0                            # s_scnptr (no on-disk bytes)
    Write-BE32 $w 0
    Write-BE32 $w 0
    Write-BE16 $w 0
    Write-BE16 $w 0
    Write-BE32 $w $STYP_BSS

    # ----- Raw section data -----
    if ($tsize -gt 0) { $w.Write([byte[]]$textBytes, 0, $tsize) }
    if ($dsize -gt 0) { $w.Write([byte[]]$dataBytes, 0, $dsize) }
}
finally {
    $w.Dispose()
    $stream.Dispose()
}

$out = Get-Item $OutputCoff
Write-Host ("Wrote {0} ({1} bytes)" -f $out.FullName, $out.Length)

