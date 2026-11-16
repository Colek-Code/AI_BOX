<#
.SYNOPSIS
将 SDK 打包器遗漏的 JLSP 可执行段纳入生成链接脚本，或只读校验 ELF 的可执行段覆盖。
.DESCRIPTION
将 SDK 打包器遗漏的 JLSP 可执行段纳入生成链接脚本，或只读校验 ELF 的可执行段覆盖。
.PARAMETER InputPath
成对的已预处理 .ld.vendor 输入路径。
.PARAMETER OutputPath
生成输出路径；链接脚本模式限制在已声明的工程输出范围内。
.PARAMETER ElfPath
只读检查的目标 ELF；与 InputPath/OutputPath 模式互斥。
.OUTPUTS
按脚本入口输出诊断信息或回执；失败沿用脚本的异常与退出码。
#>

param(
    [string]$InputPath,
    [string]$OutputPath,
    [string]$ElfPath
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($ElfPath -and -not $InputPath -and -not $OutputPath) {
    # 校验 SDK 打包器实际提取的同一组已分配可执行段。
    [byte[]]$bytes = [IO.File]::ReadAllBytes([IO.Path]::GetFullPath($ElfPath))
    if ($bytes.Length -lt 52 -or [BitConverter]::ToString($bytes, 0, 6) -cne '7F-45-4C-46-01-01') {
        throw 'Expected a target little-endian ELF32'
    }
    [long]$table = [BitConverter]::ToUInt32($bytes, 32)
    [int]$entrySize = [BitConverter]::ToUInt16($bytes, 46)
    [int]$count = [BitConverter]::ToUInt16($bytes, 48)
    [int]$stringIndex = [BitConverter]::ToUInt16($bytes, 50)
    if ($entrySize -ne 40 -or $count -lt 1 -or $count -gt 4096 -or $stringIndex -ge $count -or
        $table + $entrySize * $count -gt $bytes.LongLength) { throw 'Invalid ELF section table' }
    [int]$stringHeader = $table + $stringIndex * $entrySize
    [long]$strings = [BitConverter]::ToUInt32($bytes, $stringHeader + 16)
    [long]$stringSize = [BitConverter]::ToUInt32($bytes, $stringHeader + 20)
    if ($strings + $stringSize -gt $bytes.LongLength) { throw 'Invalid ELF string table' }
    $packed = @('.text', '.data', '.dynamic_data', '.ram0_data', '.cache_ram_data')
    for ($index = 0; $index -lt $count; $index++) {
        [int]$header = $table + $index * $entrySize
        [uint32]$flags = [BitConverter]::ToUInt32($bytes, $header + 8)
        [uint32]$size = [BitConverter]::ToUInt32($bytes, $header + 20)
        if (($flags -band 6) -ne 6 -or $size -eq 0) { continue }
        [long]$nameOffset = [BitConverter]::ToUInt32($bytes, $header)
        if ($nameOffset -ge $stringSize) { throw 'Invalid ELF section name' }
        [long]$end = $strings + $nameOffset
        while ($end -lt $strings + $stringSize -and $bytes[$end] -ne 0) { $end++ }
        if ($end -eq $strings + $stringSize) { throw 'Unterminated ELF section name' }
        $name = [Text.Encoding]::ASCII.GetString($bytes, $strings + $nameOffset, $end - $strings - $nameOffset)
        if ($packed -cnotcontains $name) { throw "Executable ELF section omitted by SDK packaging: $name" }
    }
    Write-Output 'ELF executable section coverage PASS'
    exit 0
}

if ($ElfPath -or -not $InputPath -or -not $OutputPath) { throw 'Use -ElfPath or the -InputPath/-OutputPath pair' }
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$inputFile = [IO.Path]::GetFullPath($InputPath)
$outputFile = [IO.Path]::GetFullPath($OutputPath)
$allowed = [IO.Path]::GetFullPath((Join-Path $projectRoot 'apps/D_Code/build/out')) + [IO.Path]::DirectorySeparatorChar
$normal = [IO.Path]::GetFullPath((Join-Path $projectRoot 'cpu/wl82/sdk.ld'))
if ($outputFile -ne $normal -and -not $outputFile.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Generated linker output must stay in this project'
}
if ($inputFile -ne ($outputFile + '.vendor') -or [IO.Path]::GetExtension($outputFile) -ne '.ld') {
    throw 'Expected the paired preprocessed .ld.vendor input'
}
foreach ($checkedFile in @($inputFile, $outputFile)) {
    $checkedPath = $checkedFile
    while ($checkedPath -and $checkedPath -ne $projectRoot) {
        if (Test-Path -LiteralPath $checkedPath) {
            if ((Get-Item -LiteralPath $checkedPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw 'Generated linker input/output must not traverse a link'
            }
        }
        $checkedPath = [IO.Path]::GetDirectoryName($checkedPath)
    }
}
$text = [IO.File]::ReadAllText($inputFile, [Text.Encoding]::UTF8)
$marker = '*(.text*)'
if ([regex]::Matches($text, [regex]::Escape($marker)).Count -ne 1) {
    throw 'Vendor .text input pattern changed; review linker placement'
}
if ($text.Contains('.jlsp_dns_code')) { throw 'Vendor now places JLSP code; review and retire this adapter' }
$text = $text.Replace($marker, $marker + "`n        *(.jlsp_dns_code .jlsp_dns_code.*)")
$text = $text.Replace("`r`n", "`n").Replace("`r", "`n")
[IO.File]::WriteAllText($outputFile, $text, (New-Object Text.UTF8Encoding($false)))
