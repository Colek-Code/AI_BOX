<#
.SYNOPSIS
校验 CSV 的工厂码及设备名称唯一性，生成供既有原厂转换步骤使用的逐行记录。
.DESCRIPTION
校验 CSV 的工厂码及设备名称唯一性，生成供既有原厂转换步骤使用的逐行记录。
.PARAMETER InputCsv
含 activationCode/deviceName 列的 UTF-8 CSV。
.PARAMETER OutputPath
生成输出路径；链接脚本模式限制在已声明的工程输出范围内。
.OUTPUTS
按脚本入口输出诊断信息或回执；失败沿用脚本的异常与退出码。
#>

param([Parameter(Mandatory=$true)][string]$InputCsv,
      [Parameter(Mandatory=$true)][string]$OutputPath)
# 输入 CSV 使用 activationCode/deviceName 列；输出每行一个载荷，
# 供既有 JieLi license_to_auth_csv 转换步骤使用。
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'auth_code_download_process.ps1')
$destination = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $destination) { throw 'Output exists; preserve it and choose a new file.' }
$rows = @(Import-Csv -LiteralPath $InputCsv -Encoding UTF8)
if (-not $rows.Count) { throw 'No factory rows.' }
$names = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
$codes = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
$payloads = New-Object 'System.Collections.Generic.List[string]'
foreach ($row in $rows) {
    if (-not $row.PSObject.Properties['activationCode'] -or -not $row.PSObject.Properties['deviceName']) {
        throw 'CSV needs activationCode and deviceName columns.'
    }
    $payload = 'Q1|' + $row.activationCode + '|' + $row.deviceName
    Assert-FactoryRecord $payload
    if (-not $names.Add($row.deviceName) -or -not $codes.Add($row.activationCode)) {
        throw 'Duplicate device name or activation code in factory batch.'
    }
    $payloads.Add($payload)
}
[IO.File]::WriteAllLines($destination, $payloads, [Text.Encoding]::ASCII)
Write-Host ('Prepared ' + $payloads.Count + ' factory records. Contents hidden; no downloader executed.')
