param([string]$Path)

$bytes = [System.IO.File]::ReadAllBytes($Path)

function Be32 { param($b,$p) ([uint32]$b[$p] -shl 24) -bor ([uint32]$b[$p+1] -shl 16) -bor ([uint32]$b[$p+2] -shl 8) -bor [uint32]$b[$p+3] }
function Be16 { param($b,$p) ([uint16]$b[$p] -shl 8) -bor [uint16]$b[$p+1] }

$shoff = Be32 $bytes 32
$shnum = Be16 $bytes 48

$symtab_off = 0
$symtab_size = 0
$strtab_off = 0
for ($i = 0; $i -lt $shnum; $i++) {
  $sh = $shoff + $i*40
  $t = Be32 $bytes ($sh + 4)
  if ($t -eq 2) {
    $symtab_off  = Be32 $bytes ($sh+16)
    $symtab_size = Be32 $bytes ($sh+20)
    $stl         = Be32 $bytes ($sh+24)
    $strtab_off  = Be32 $bytes ($shoff + $stl*40 + 16)
  }
}

"symtab @ 0x{0:X} size 0x{1:X} strtab @ 0x{2:X}" -f $symtab_off,$symtab_size,$strtab_off
$n = $symtab_size / 16
for ($i = 0; $i -lt $n; $i++) {
  $sym = $symtab_off + $i*16
  $name_off = Be32 $bytes $sym
  $val      = Be32 $bytes ($sym+4)
  $info     = $bytes[$sym+12]
  $shndx    = Be16 $bytes ($sym+14)
  $name     = ""
  $p        = $strtab_off + $name_off
  while ($p -lt $bytes.Length -and $bytes[$p] -ne 0) {
    $name += [char]$bytes[$p]
    $p++
  }
  if ($name -eq "_start" -or $name -eq "start" -or $name -like "*start*") {
    "[{0}] {1}: val=0x{2:X} shndx=0x{3:X} info=0x{4:X} bind={5} type={6}" -f $i,$name,$val,$shndx,$info,($info -shr 4),($info -band 0xF)
  }
}
