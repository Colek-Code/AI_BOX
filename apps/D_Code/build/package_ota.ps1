param(
    [switch]$PrepareOnly,
    [string]$BuildDir = (Join-Path $PSScriptRoot 'out/wl82/ota'),
    [string]$WorkDir = (Join-Path $PSScriptRoot 'out/product_ui_ota/packages'),
    [string]$SdkDir = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
if (-not $SdkDir) { $SdkDir = Join-Path $projectRoot 'cpu/wl82/tools' }

function Set-IniValue([string]$Text, [string]$Name, [string]$Value) {
    $pattern = '(?m)^' + [regex]::Escape($Name) + '\s*=[^;\r\n]+;'
    if ([regex]::Matches($Text, $pattern).Count -ne 1) { throw "配置项缺失或重复：$Name" }
    return [regex]::Replace($Text, $pattern, ($Name + '=' + $Value + ';'))
}

function FileHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose(); $stream.Dispose() }
}

try {
    foreach ($name in @('sdk.elf','download.bat','isd_config.ini','d_code_config.mk')) {
        if (-not (Test-Path -LiteralPath (Join-Path $BuildDir $name) -PathType Leaf)) {
            throw '请先正常编译 D_Code，再双击打包入口。'
        }
    }
    $config = @{}
    foreach ($line in Get-Content -LiteralPath (Join-Path $BuildDir 'd_code_config.mk')) {
        if ($line -match '^(D_BUILD_\w+)\s*:=\s*(\S+)\s*$') { $config[$matches[1]] = $matches[2] }
    }
    foreach ($key in @('D_BUILD_OTA_KEEP_VM','D_BUILD_OTA_BOOT_VERSION','D_BUILD_FIRMWARE_VERSION')) {
        if (-not $config.ContainsKey($key)) { throw '编译配置不完整，请重新编译 D_Code。' }
    }
    if ($config.D_BUILD_BOARD_TARGET -ne '1001' -or $config.D_BUILD_UI_PRODUCT -ne '1' -or
        $config.D_BUILD_DOUBLE_BANK -ne '1' -or $config.D_BUILD_FEATURE_OTA -ne '1') {
        throw '此入口用于已启用 OTA 的 GC9307/HYN 双备份 Product UI。'
    }
    if ($config.D_BUILD_OTA_KEEP_VM -notmatch '^[01]$' -or
        $config.D_BUILD_OTA_BOOT_VERSION -notmatch '^\d{1,5}$' -or
        [int]$config.D_BUILD_OTA_BOOT_VERSION -lt 1 -or [int]$config.D_BUILD_OTA_BOOT_VERSION -gt 65535) {
        throw 'VM 策略必须为 0/1；OTA 升级序号必须为 1—65535，且高于设备现有序号。'
    }
    $ini = Get-Content -LiteralPath (Join-Path $BuildDir 'isd_config.ini') -Raw
    if ($ini -notmatch '(?m)^BR22_TWS_DB=YES;' -or $ini -notmatch '(?m)^FLASH_SIZE=16M;') {
        throw '编译结果不是 16 MB 双备份配置。'
    }
    $ini = Set-IniValue $ini 'VM_OPT' $config.D_BUILD_OTA_KEEP_VM
    $ini = Set-IniValue $ini 'BR22_TWS_VERSION' $config.D_BUILD_OTA_BOOT_VERSION
    $WorkDir = [IO.Path]::GetFullPath($WorkDir)
    $boundary = Join-Path $projectRoot 'apps\D_Code\build\out\product_ui_ota'
    if (-not $WorkDir.StartsWith($boundary + '\', [StringComparison]::OrdinalIgnoreCase)) { throw '打包输出必须位于本工程 product_ui_ota 任务目录。' }
    for ($parent = $WorkDir; $parent.Length -ge $boundary.Length; $parent = Split-Path -Parent $parent) {
        if ((Test-Path -LiteralPath $parent) -and ((Get-Item -LiteralPath $parent).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw '打包输出不能使用目录联接或符号链接。'
        }
    }
    $stage = Join-Path $WorkDir ([DateTime]::Now.ToString('yyyyMMdd_HHmmss_fffffff'))
    if ($stage -match '[!%&^<>|]') { throw 'SDK 原脚本不支持此路径中的特殊字符。' }
    $null = New-Item -ItemType Directory -Path $stage -Force
    foreach ($name in @('sdk.elf','download.bat')) { Copy-Item -LiteralPath (Join-Path $BuildDir $name) -Destination $stage }
    [IO.File]::WriteAllText((Join-Path $stage 'isd_config.ini'), $ini, [Text.Encoding]::ASCII)
    # isd_download reads ota.bin implicitly when generating a dual-bank package.
    foreach ($name in @('生成升级固件.bat','note.txt','script.ver','wl82loader.bin','sd_update2.bin','usb_update2.bin','uboot.boot','ota.bin','cfg_tool.bin','audlogo','cfg')) {
        Copy-Item -LiteralPath (Join-Path $SdkDir $name) -Destination (Join-Path $stage $name) -Recurse
    }
    # Remove only the copied menu's final pause so the full output path appears immediately.
    # Byte-preserving encoding keeps the vendor batch's original Chinese code page intact.
    $menu = Join-Path $stage '生成升级固件.bat'
    $bytesEncoding = [Text.Encoding]::GetEncoding(28591)
    $menuText = [IO.File]::ReadAllText($menu, $bytesEncoding)
    $menuText = [regex]::Replace($menuText, '(?im)^pause[\t ]*(?:\r?\n)*\z', '')
    [IO.File]::WriteAllText($menu, $menuText, $bytesEncoding)
    Write-Host ("应用版本：{0}  SDK 升级序号：{1}  VM_OPT：{2}（1 保留，0 清空）" -f
        $config.D_BUILD_FIRMWARE_VERSION,$config.D_BUILD_OTA_BOOT_VERSION,$config.D_BUILD_OTA_KEEP_VM)
    Write-Host "工作目录：$stage"
    if ($PrepareOnly) { Write-Host '仅准备配置，未调用 SDK。'; exit 0 }
    Write-Host '将调用 SDK 原脚本，包含有线下载；此操作也使用上述 VM 策略。随后在官方菜单选择 2。'
    if ((Read-Host '确认请输入 RUN，其他输入取消') -cne 'RUN') { Write-Host '已取消。'; exit 0 }
    $env:PATH = $SdkDir + ';' + $env:PATH
    foreach ($name in @('KEY_FILE','UI_RES','AUDIO_RES','UPDATE_FILES')) { [Environment]::SetEnvironmentVariable($name,$null,'Process') }
    Push-Location -LiteralPath $stage
    try {
        & cmd.exe /d /c 'chcp 936 >nul & call 生成升级固件.bat'
        $result = $LASTEXITCODE
    } finally { Pop-Location; & cmd.exe /d /c 'chcp 65001 >nul' }
    $ufw = [IO.Path]::GetFullPath((Join-Path $stage 'upgrade/update-ota.ufw'))
    $source = Join-Path $stage 'db_update_files_data.bin'
    if ($result -ne 0 -or -not (Test-Path -LiteralPath $ufw) -or -not (Test-Path -LiteralPath $source)) {
        throw 'SDK 未生成本次 OTA 文件，请检查官方工具输出，并确认选择了 2。'
    }
    if ((Get-Item -LiteralPath $ufw).Length -eq 0 -or (FileHash $ufw) -ne (FileHash $source)) {
        throw 'OTA 文件为空或与 SDK 网络升级包不一致。'
    }
    $folder = Split-Path -Parent $ufw
    Write-Host ''
    Write-Host '打包完成。上传文件：update-ota.ufw'
    Write-Host '文件所在文件夹（可复制到资源管理器地址栏）：'
    Write-Host $folder
    Write-Host ''
    Write-Host '文件完整路径：'
    Write-Host $ufw
    Write-Host ('SHA-256：' + (FileHash $ufw))
    if ((Read-Host '输入 O 打开所在文件夹，直接回车跳过') -ieq 'O') {
        try { Start-Process explorer.exe -ArgumentList ('"' + $folder + '"') -ErrorAction Stop }
        catch { Write-Host '无法打开资源管理器，请复制上面的文件夹路径手动打开。' }
    }
} catch {
    Write-Host ('打包失败：' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
