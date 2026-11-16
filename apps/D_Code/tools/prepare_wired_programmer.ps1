<#
.SYNOPSIS
校验构建选择和固定 SDK 输入，在生成的下载批处理中接入可选鉴权码适配，并写入回执。
.DESCRIPTION
校验构建选择和固定 SDK 输入，在生成的下载批处理中接入可选鉴权码适配，并写入回执。
.PARAMETER ConfigFile
本次构建生成的配置文件。
.PARAMETER SdkDirectory
本次生成输入及固定 SDK 下载器所在目录。
.OUTPUTS
按脚本入口输出诊断信息或回执；失败沿用脚本的异常与退出码。
#>

param([Parameter(Mandatory=$true)][string]$ConfigFile,
      [Parameter(Mandatory=$true)][string]$SdkDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
try {
    . (Join-Path $PSScriptRoot 'auth_code_download_process.ps1')
    $config = [IO.File]::ReadAllText([IO.Path]::GetFullPath($ConfigFile))
    $values = @{}
    foreach ($match in [regex]::Matches($config, '(?m)^(D_BUILD_\w+)\s*:=\s*(\S+)\s*$')) {
        if ($values.ContainsKey($match.Groups[1].Value)) { throw 'Duplicate build configuration.' }
        $values[$match.Groups[1].Value] = $match.Groups[2].Value
    }
    if (-not $values.ContainsKey('D_BUILD_QWEN_AUTH_MODE') -or -not $values.ContainsKey('D_BUILD_QWEN_ACTIVATION_SOURCE')) {
        throw 'Missing Qwen build selection.'
    }
    # 工厂记录的消费不取决于平台激活来源。
    $factoryName = $values.ContainsKey('D_BUILD_QWEN_FACTORY_NAME') -and $values['D_BUILD_QWEN_FACTORY_NAME'] -eq '1'
    $programmer = $values['D_BUILD_QWEN_AUTH_MODE'] -eq '2' -and $values['D_BUILD_QWEN_ACTIVATION_SOURCE'] -eq '4'
    if (-not $factoryName -and -not $programmer) { exit 0 }
    if ($values['D_BUILD_BOARD_TARGET'] -ne '1001' -or $values['D_BUILD_AUTH_CODE_READ'] -ne '1') {
        throw 'PROGRAMMER requires the qualified GC9307 reader configuration.'
    }
    $directory = [IO.Path]::GetFullPath($SdkDirectory)
    $pins = @{
        'isd_download.exe'='a153c8158f196b46a810a6eeb96c7c09361051d71b489b5348fba5c7482b4876'
        # AC79NN 1.2.14：已重新核对原始读取值流和 VM 状态布局。
        '../liba/cpu.a'='3d3e0876e21afe2c9cb7de713378d3802b1d0f544a1787f07e982269a0e7e67d'
        '../liba/cfg_tool.a'='906341ad25ed6d80b1bb552f0d2fa58c3dd6a76fadc8fbadf7bed3e97b1f06fd'
    }
    foreach ($name in $pins.Keys) {
        if ((Get-AuthInputHash (Join-Path $directory $name)) -ne $pins[$name]) { throw ('SDK changed; revalidate '+$name) }
    }
    $codePath = Join-Path $directory 'activation-code.txt'
    $codePresent = Test-Path -LiteralPath $codePath
    if ($codePresent) { $code = Read-ActivationCodeFile $codePath }
    $batchPath = Join-Path $directory 'download.bat'
    $iniPath = Join-Path $directory 'isd_config.ini'
    # 原厂生成文件可能含 GBK 注释；Latin-1 用于无损往返原字节；
    # 只替换 ASCII 配置键和命令行。
    $encoding = [Text.Encoding]::GetEncoding(28591)
    $batch = $encoding.GetString([IO.File]::ReadAllBytes($batchPath))
    $ini = $encoding.GetString([IO.File]::ReadAllBytes($iniPath))
    foreach ($entry in @('FLASH_SIZE=16M','BR22_TWS_DB=YES','NEED_RESERVED_4K=YES')) {
        $pair=$entry.Split('=');$found=[regex]::Matches($ini,'(?m)^'+$pair[0]+'\s*=\s*([^;\r\n]*);')
        if ($found.Count -ne 1 -or $found[0].Groups[1].Value -ne $pair[1]) { throw ('Unexpected '+$pair[0]) }
    }
    $vm = [regex]::Matches($ini, '(?m)^VM_OPT\s*=\s*([01]);')
    if ($vm.Count -ne 1) { throw 'Missing or ambiguous wired VM policy.' }
    # AUTH_CODE 属于人工维护的 SDK 配置；保留
    # 其值或缺省状态，本适配器不补值也不额外拒绝。
    # 替换范围不包含原行终止符；如果只消费
    # CR，会在原厂 GBK REM 行前留下裸 LF，CMD 在 CP936 下可能
    # 将其作为乱码命令执行并返回 9009。
    $line = [regex]::Matches($batch, '(?m)^isd_download\.exe[ \t]+([^\r\n]+)(?=\r?$)')
    if ($line.Count -ne 1) { throw 'Expected one unmodified SDK download command; rebuild before retrying.' }
    $arguments = $line[0].Groups[1].Value
    if ($arguments -match '(?i)-auth-code') { throw 'Unexpected existing authentication-code argument.' }
    $runner = Join-Path $PSScriptRoot 'run_wired_programmer.ps1'
    $replacement = 'set "D_CODE_DOWNLOAD_ARGS=' + $arguments + '"' + "`r`n" +
        'powershell -NoProfile -ExecutionPolicy Bypass -File "' + $runner + '" -SdkDirectory "%~dp0."' + "`r`n" +
        'if errorlevel 1 exit /b 1'
    $batch = $batch.Remove($line[0].Index,$line[0].Length).Insert($line[0].Index,$replacement)
    # 仅对生成的 Windows BAT 统一换行；通过上述 Latin-1 往返
    # 保留其余所有原厂字节。
    $batch = [regex]::Replace($batch, '(?<!\r)\n', "`r`n")
    $receipt = [ordered]@{mode=[int]$values['D_BUILD_QWEN_AUTH_MODE'];source=[int]$values['D_BUILD_QWEN_ACTIVATION_SOURCE'];factory_name=$factoryName;vm_policy=$vm[0].Groups[1].Value;
        elf_sha256=(Get-AuthInputHash (Join-Path $directory 'sdk.elf'));
        config_sha256=(Get-AuthInputHash ([IO.Path]::GetFullPath($ConfigFile)));
        config_file=[IO.Path]::GetFullPath($ConfigFile);
        downloader_sha256=(Get-AuthInputHash (Join-Path $directory 'isd_download.exe'))}
    [IO.File]::WriteAllBytes($batchPath,$encoding.GetBytes($batch))
    $receipt['ini_sha256'] = Get-AuthInputHash $iniPath
    $receipt['sdk_pins'] = $pins
    [IO.File]::WriteAllText((Join-Path $directory 'd_code_programmer.json'),($receipt | ConvertTo-Json),[Text.Encoding]::UTF8)
    Write-Host ('PROGRAMMER ready; authentication input present='+$codePresent+'; wired VM_OPT='+$receipt.vm_policy)
} catch { Write-Host ('ERROR: '+$_.Exception.Message); exit 1 }
finally { $code=$null }
