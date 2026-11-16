<#
.SYNOPSIS
复核生成回执及可选鉴权码，CheckOnly 提前返回，其余路径以脱敏进程函数执行下载。
.DESCRIPTION
复核生成回执及可选鉴权码，CheckOnly 提前返回，其余路径以脱敏进程函数执行下载。
.PARAMETER SdkDirectory
本次生成输入及固定 SDK 下载器所在目录。
.PARAMETER CheckOnly
仅验证输入；在调用任何下载器之前返回。
.OUTPUTS
按脚本入口输出诊断信息或回执；失败沿用脚本的异常与退出码。
#>

param([Parameter(Mandatory=$true)][string]$SdkDirectory, [switch]$CheckOnly)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$temporaryCode = $null
try {
    . (Join-Path $PSScriptRoot 'auth_code_download_process.ps1')
    $directory = [IO.Path]::GetFullPath($SdkDirectory)
    $receipt = Get-Content -LiteralPath (Join-Path $directory 'd_code_programmer.json') -Raw | ConvertFrom-Json
    $factoryName = $receipt.PSObject.Properties['factory_name'] -and $receipt.factory_name
    if (-not $factoryName -and ($receipt.mode -ne 2 -or $receipt.source -ne 4)) { throw 'Incorrect download configuration.' }
    foreach ($item in $receipt.sdk_pins.PSObject.Properties) {
        if ((Get-AuthInputHash (Join-Path $directory $item.Name)) -ne $item.Value) { throw ('SDK changed; rebuild and revalidate '+$item.Name) }
    }
    foreach ($item in @(@('sdk.elf',$receipt.elf_sha256),@('isd_config.ini',$receipt.ini_sha256),
                         @('isd_download.exe',$receipt.downloader_sha256))) {
        if ((Get-AuthInputHash (Join-Path $directory $item[0])) -ne $item[1]) { throw ('Changed '+$item[0]+'; rebuild first.') }
    }
    if ((Get-AuthInputHash $receipt.config_file) -ne $receipt.config_sha256) { throw 'Build mode changed; rebuild first.' }
    $code = $null
    $codePath = Join-Path $directory 'activation-code.txt'
    if (Test-Path -LiteralPath $codePath) { $code = Read-ActivationCodeFile $codePath }
    if ($CheckOnly) { Write-Host 'PROGRAMMER inputs verified; no download performed.'; exit 0 }
    $arguments = [Environment]::GetEnvironmentVariable('D_CODE_DOWNLOAD_ARGS')
    if (-not $arguments -or $arguments -match '(?i)-auth-code') { throw 'Use the generated SDK download.bat entry.' }
    $downloadArguments = @($arguments)
    if ($null -ne $code) {
        $path = Join-Path $directory '.d_code_activation_input.txt'
        if (Test-Path -LiteralPath $path) { throw 'Temporary code input already exists; preserve and inspect it before retrying.' }
        $temporaryCode=$path
        [IO.File]::WriteAllBytes($path,[Text.Encoding]::ASCII.GetBytes($code))
        $downloadArguments += @('-auth-code','.d_code_activation_input.txt')
    }
    $result=Invoke-AuthCodeDownloader -Executable (Join-Path $directory 'isd_download.exe') -Directory $directory `
        -Arguments $downloadArguments -Secret $code
    if ($result -ne 0) { throw ('Downloader failed: '+$result) }
} catch { Write-Host ('ERROR: '+$_.Exception.Message); exit 1 }
finally {
    if ($temporaryCode -and (Test-Path -LiteralPath $temporaryCode)) { Remove-Item -LiteralPath $temporaryCode }
    $code=$null
}
