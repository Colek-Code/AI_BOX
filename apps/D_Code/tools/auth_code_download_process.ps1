<#
.SYNOPSIS
提供工厂记录校验、输入摘要和脱敏下载进程函数；加载文件本身不启动下载器。
.DESCRIPTION
提供工厂记录校验、输入摘要和脱敏下载进程函数；加载文件本身不启动下载器。
.OUTPUTS
按脚本入口输出诊断信息或回执；失败沿用脚本的异常与退出码。
#>

<#
.SYNOPSIS
检查旧式 16 字符工厂码或 Q1 记录的完整格式。
.DESCRIPTION
检查旧式 16 字符工厂码或 Q1 记录的完整格式。
.PARAMETER Record
完整工厂记录字符串，不得输出其内容。
.OUTPUTS
无管道输出；格式无效时抛出异常。
#>
function Assert-FactoryRecord([string]$Record) {
    if ($Record -cnotmatch '\A(?:[0-9A-HJKMNP-TV-Z]{16}|Q1\|[0-9A-HJKMNP-TV-Z]{16}\|[A-Za-z0-9_-]{1,32})\z') {
        throw 'Factory record must be a legacy 16-character code or Q1|code|deviceName (1-32 ASCII letters/digits/_/-).'
    }
}

<#
.SYNOPSIS
以 UTF-8 读取输入，去除 BOM 和一个末尾换行，再校验工厂记录。
.DESCRIPTION
以 UTF-8 读取输入，去除 BOM 和一个末尾换行，再校验工厂记录。
.PARAMETER Path
待读取文件的路径。
.OUTPUTS
校验后的记录字符串；调用方负责保密。
#>
function Read-ActivationCodeFile([string]$Path) {
    $code = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($Path)).TrimStart([char]0xfeff)
    $code = [regex]::Replace($code, '\r?\n\z', '')
    Assert-FactoryRecord $code
    return $code
}

<#
.SYNOPSIS
计算输入文件摘要，finally 中释放读取流和哈希器。
.DESCRIPTION
计算输入文件摘要，finally 中释放读取流和哈希器。
.PARAMETER Path
待读取文件的路径。
.OUTPUTS
小写十六进制 SHA-256。
#>
function Get-AuthInputHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $hash = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $hash.Dispose() }
}

# 并行排空原生标准输出及错误流；不要将原生错误流接入
# PowerShell 错误管道，Windows PowerShell 5 可能因此中断下载。
<#
.SYNOPSIS
并行排空原生进程的标准输出和错误流，隐藏鉴权码回显。
.DESCRIPTION
并行排空原生进程的标准输出和错误流，隐藏鉴权码回显。
.PARAMETER Executable
原生下载器完整路径。
.PARAMETER Directory
下载器工作目录。
.PARAMETER Arguments
按现有规则连接的原生参数数组。
.PARAMETER Secret
须从普通输出和错误流中移除的工厂记录。
.OUTPUTS
原生进程退出码；启动或读取失败时抛出异常。
#>
function Invoke-AuthCodeDownloader {
    param([string]$Executable, [string]$Directory, [string[]]$Arguments, [string]$Secret)
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $Executable
    $start.WorkingDirectory = $Directory
    $start.Arguments = ($Arguments -join ' ')
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { throw 'Cannot start downloader.' }
        $streams = @($process.StandardOutput, $process.StandardError)
        $pending = @($streams[0].ReadLineAsync(), $streams[1].ReadLineAsync())
        while ($null -ne $pending[0] -or $null -ne $pending[1]) {
            $progress = $false
            for ($index = 0; $index -lt 2; $index++) {
                if ($null -ne $pending[$index] -and $pending[$index].IsCompleted) {
                    $progress = $true
                    $line = $pending[$index].GetAwaiter().GetResult()
                    if ($null -eq $line) { $pending[$index] = $null }
                    else {
                        # 隐藏固定写入工具的码值回显，并移除
                        # 普通输出或错误中再次出现的完整码值。
                        if ($line -match 'auth_code\.txt\s*:') { $line = 'Authentication code input processed.' }
                        elseif ($Secret) {
                            $line = $line.Replace($Secret, '[REDACTED]')
                            if ($Secret.StartsWith('Q1|')) {
                                $line = $line.Replace($Secret.Split('|')[1], '[REDACTED]')
                            }
                        }
                        Write-Host $line
                        $pending[$index] = $streams[$index].ReadLineAsync()
                    }
                }
            }
            if (-not $progress) { Start-Sleep -Milliseconds 10 }
        }
        $process.WaitForExit()
        return $process.ExitCode
    } finally { $process.Dispose() }
}
