# Shared helpers for Fcitx5 TSF IME install / uninstall (dot-source only).
# CLSID matches win32/dll/util.cpp — keep in sync with FCITX_CLSID.

$script:FcitxImeClsid = '{FC3869BA-51E3-4078-8EE2-5FE49493A1F4}'
# IME DLL is x86_64: must use 64-bit regsvr32. In 32-bit WOW64 PowerShell, System32 points at SysWOW64.
$script:Regsvr32Path = if (
    [Environment]::Is64BitOperatingSystem -and -not [Environment]::Is64BitProcess) {
    $n = Join-Path $env:SystemRoot 'Sysnative\regsvr32.exe'
    if (Test-Path -LiteralPath $n) { $n } else { Join-Path $env:SystemRoot 'System32\regsvr32.exe' }
} else {
    Join-Path $env:SystemRoot 'System32\regsvr32.exe'
}

function Get-FcitxImeStrings {
    param(
        [ValidateSet('en', 'zh')]
        [string] $Lang = 'en'
    )
    $en = @{
        AdminWarn        = 'Not running as Administrator. Registration usually needs elevation (HKCR).'
        Robocopy         = 'Robocopy:'
        Register         = 'Register IME:'
        Unregister       = 'Unregister IME:'
        RegsvrFail       = 'regsvr32 failed with exit'
        ImeMissing       = 'IME DLL not found under'
        PurgeReg         = 'Removing residual registry keys (CLSID + CTF TIP)...'
        PurgeDone        = 'Registry cleanup finished.'
        RemoveTree       = 'Removing deploy directory:'
        RemoveCancelled  = 'File removal cancelled.'
        ConfirmRemove    = 'Delete the entire deploy directory? (yes/no)'
        StageMissing     = 'Stage path not found:'
        RobocopyFail     = 'robocopy failed with exit'
        IcoWarn          = 'penguin.ico missing in bin (optional TSF icon).'
        NextSettings     = 'Next: Settings -> Time & language -> Language & region -> add Fcitx5 (zh-Hans).'
        TestNotepad      = 'Test: Notepad, select Fcitx5, Ctrl+Space, type pinyin.'
        ProfileHint      = 'Copy share\fcitx5\profile.windows.example (or profile.pinyin*.example) to config\fcitx5\profile or set FCITX_TS_IM=pinyin.'
        DeployRoot       = 'Deploy root:'
        DeployAbsent     = 'Deploy directory already absent.'
        HkcuPurge        = 'Removing per-user registry keys (HKCU CTF TIP / CLSID) referencing this IME...'
        HkcuDone         = 'Per-user (HKCU) registry cleanup finished.'
    }
    $zh = @{
        AdminWarn        = '当前未以管理员身份运行。注册通常需要提升权限（HKCR）。'
        Robocopy         = '正在 robocopy 同步:'
        Register         = '正在注册 IME:'
        Unregister       = '正在注销 IME:'
        RegsvrFail       = 'regsvr32 失败，退出码'
        ImeMissing       = '未在以下路径找到 IME DLL:'
        PurgeReg         = '正在清理残留注册表（CLSID + CTF TIP）...'
        PurgeDone        = '注册表清理完成。'
        RemoveTree       = '正在删除部署目录:'
        RemoveCancelled  = '已取消删除文件。'
        ConfirmRemove    = '是否删除整个部署目录？(yes/no)'
        StageMissing     = '找不到 stage 目录:'
        RobocopyFail     = 'robocopy 失败，退出码'
        IcoWarn          = 'bin 下缺少 penguin.ico（TSF 图标，可选）。'
        NextSettings     = '下一步：设置 -> 时间和语言 -> 语言和区域 -> 为简体中文添加 Fcitx5 键盘。'
        TestNotepad      = '测试：记事本中选择 Fcitx5，Ctrl+Space 中文，输入拼音。'
        ProfileHint      = '将 share\fcitx5\profile.windows.example（或 profile.pinyin*.example）复制到 config\fcitx5\profile 或设置 FCITX_TS_IM=pinyin。'
        DeployRoot       = '部署根目录:'
        DeployAbsent     = '部署目录已不存在。'
        HkcuPurge        = '正在删除当前用户下引用本 IME 的注册表项（HKCU CTF TIP / CLSID）...'
        HkcuDone         = '当前用户（HKCU）注册表清理完成。'
    }
    if ($Lang -eq 'zh') { return $zh }
    return $en
}

function Resolve-FcitxImeLang {
    param([string] $Preferred = '')
    if ($Preferred -eq 'zh') { return 'zh' }
    if ($Preferred -eq 'en') { return 'en' }
    if ($PSUICulture.Name -like 'zh*') { return 'zh' }
    return 'en'
}

function Test-FcitxImeAdmin {
    $p = New-Object Security.Principal.WindowsPrincipal(
        [Security.Principal.WindowsIdentity]::GetCurrent())
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-FcitxImeDll {
    param([string] $BinDir)
    if (-not (Test-Path -LiteralPath $BinDir)) { return $null }
    foreach ($name in @('fcitx5-x86_64.dll', 'libfcitx5-x86_64.dll')) {
        $p = Join-Path $BinDir $name
        if (Test-Path -LiteralPath $p) { return Get-Item -LiteralPath $p }
    }
    return Get-ChildItem -LiteralPath $BinDir -Filter '*fcitx5*x86_64*.dll' -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
}

function Copy-FcitxImeStage {
    param(
        [string] $Stage,
        [string] $DeployDir
    )
    New-Item -ItemType Directory -Force -Path $DeployDir | Out-Null
    # TSF keeps libfcitx5-x86_64.dll (or fcitx5-x86_64.dll) mapped; updating it in one full-tree
    # copy often yields robocopy ERROR 32. Sync everything except the IME DLL, then copy the DLL
    # with many retries.
    $rc = Start-Process -FilePath 'robocopy.exe' -ArgumentList @(
        $Stage, $DeployDir, '/E', '/COPY:DAT',
        '/XF', 'libfcitx5-x86_64.dll', 'fcitx5-x86_64.dll',
        '/R:3', '/W:2', '/NFL', '/NDL', '/NJH', '/NJS'
    ) -Wait -PassThru
    if ($rc.ExitCode -gt 7) {
        throw "robocopy exit $($rc.ExitCode)"
    }

    $stageBin = Join-Path $Stage 'bin'
    $deployBin = Join-Path $DeployDir 'bin'
    if (-not (Test-Path -LiteralPath $stageBin)) {
        return
    }
    New-Item -ItemType Directory -Force -Path $deployBin | Out-Null
    foreach ($imeName in @('libfcitx5-x86_64.dll', 'fcitx5-x86_64.dll')) {
        $srcDll = Join-Path $stageBin $imeName
        if (-not (Test-Path -LiteralPath $srcDll)) {
            continue
        }
        $rcDll = Start-Process -FilePath 'robocopy.exe' -ArgumentList @(
            $stageBin, $deployBin, $imeName,
            '/COPY:DAT', '/R:25', '/W:2', '/NFL', '/NDL', '/NJH', '/NJS'
        ) -Wait -PassThru
        if ($rcDll.ExitCode -gt 7) {
            throw (
                "robocopy IME DLL '$imeName' exit $($rcDll.ExitCode). " +
                'ERROR 32 = file in use: uninstall IME (regsvr32 /u), close apps that used Fcitx5, ' +
                'or sign out/reboot, then run install again. ' +
                "To list PIDs: .\scripts\find-fcitx5-bin-lock.ps1 -DeployDir '$DeployDir'"
            )
        }
    }
}

function Remove-FcitxImeRegistryTree {
    # HKCR CLSID + HKLM CTF TIP — same trees as register.cpp UnregisterServer / UnregisterCategoriesAndProfiles
    $clsidPath = "Registry::HKEY_CLASSES_ROOT\CLSID\$($script:FcitxImeClsid)"
    $tipPath = "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\CTF\TIP\$($script:FcitxImeClsid)"
    foreach ($p in @($clsidPath, $tipPath)) {
        if (Test-Path -LiteralPath $p) {
            Remove-Item -LiteralPath $p -Recurse -Force -ErrorAction Stop
        }
    }
}

function Get-FcitxImeHkcuResidualRegistryPaths {
    <#
    .SYNOPSIS
      Paths under HKCU that sometimes remain after HKCR/HKLM purge (CTF tip cache, WOW CLSID mirror).
    #>
    $g = $script:FcitxImeClsid
    $list = @(
        "HKCU:\Software\Microsoft\CTF\TIP\$g",
        "HKCU:\Software\Classes\CLSID\$g"
    )
    foreach ($p in $list) {
        if (Test-Path -LiteralPath $p) {
            $p
        }
    }
}

function Remove-FcitxImeHkcuResidualRegistry {
    foreach ($p in Get-FcitxImeHkcuResidualRegistryPaths) {
        Remove-Item -LiteralPath $p -Recurse -Force -ErrorAction Stop
    }
}

function Invoke-FcitxImeRegister {
    param([System.IO.FileInfo] $Dll)
    if (-not (Test-Path -LiteralPath $script:Regsvr32Path)) {
        throw "regsvr32 not found: $($script:Regsvr32Path)"
    }
    $bin = $Dll.DirectoryName
    $savedPath = $env:PATH
    try {
        $env:PATH = "$bin;$savedPath"
        $proc = Start-Process -FilePath $script:Regsvr32Path -ArgumentList @('/s', $Dll.FullName) `
            -WorkingDirectory $bin -Wait -PassThru -WindowStyle Hidden
    } finally {
        $env:PATH = $savedPath
    }
    if ($proc.ExitCode -ne 0) {
        $code = $proc.ExitCode
        $hint = switch ($code) {
            3 { ' (DLL_LOAD_FAILED — missing dependency: redeploy from a fresh stage build/install so bin includes libc++.dll and other MinGW runtimes next to the IME; see win32/dll/CMakeLists.txt.)' }
            5 { ' (ACCESS_DENIED — open an elevated “Windows PowerShell” / “Terminal” as Administrator and retry.)' }
            Default {
                if (-not (Test-FcitxImeAdmin)) {
                    ' If you are not Admin, elevation is required for HKCR. Use 64-bit PowerShell for x64 IME DLL (script picks Sysnative\regsvr32 under WOW64).'
                } else {
                    ' See regsvr32 docs; 0x80040201 often means DllRegisterServer failed (dependencies or TSF).'
                }
            }
        }
        throw "regsvr32 exit $code$hint"
    }
}

function Invoke-FcitxImeUnregister {
    param([System.IO.FileInfo] $Dll)
    if (-not (Test-Path -LiteralPath $script:Regsvr32Path)) {
        throw "regsvr32 not found: $($script:Regsvr32Path)"
    }
    $bin = $Dll.DirectoryName
    $savedPath = $env:PATH
    try {
        $env:PATH = "$bin;$savedPath"
        $proc = Start-Process -FilePath $script:Regsvr32Path -ArgumentList @('/u', '/s', $Dll.FullName) `
            -WorkingDirectory $bin -Wait -PassThru -WindowStyle Hidden
    } finally {
        $env:PATH = $savedPath
    }
    # regsvr32 may return non-zero if partially unregistered; still try registry purge
    return $proc.ExitCode
}

function Get-FcitxImeProcessesLockingBin {
    <#
    .SYNOPSIS
      Lists processes whose main image or loaded modules live under DeployDir\bin (typical DLL lock when robocopy fails with ERROR 32).
    #>
    param(
        [string] $DeployDir = 'C:\Fcitx5Portable'
    )
    $DeployDir = $DeployDir.TrimEnd('\', '/')
    $bin = Join-Path $DeployDir 'bin'
    if (-not (Test-Path -LiteralPath $bin)) {
        Write-Warning "Bin directory not found: $bin (listing fcitx5*x86_64*.dll modules anywhere)."
    }
    $binFull = if (Test-Path -LiteralPath $bin) {
        [System.IO.Path]::GetFullPath((Get-Item -LiteralPath $bin).FullName)
    } else {
        $null
    }
    $rows = New-Object System.Collections.Generic.List[object]
    foreach ($proc in Get-Process -ErrorAction SilentlyContinue) {
        if ($binFull -and $proc.Path) {
            try {
                $mp = [System.IO.Path]::GetFullPath($proc.Path)
                if ($mp.StartsWith($binFull, [System.StringComparison]::OrdinalIgnoreCase)) {
                    $rows.Add([PSCustomObject]@{
                            ProcessId   = $proc.Id
                            ProcessName = $proc.ProcessName
                            Kind        = 'MainExe'
                            Path        = $proc.Path
                        })
                }
            }
            catch {}
        }
        try {
            foreach ($mod in $proc.Modules) {
                $fp = $mod.FileName
                if (-not $fp) { continue }
                $matchBin = $false
                if ($binFull) {
                    try {
                        $fpFull = [System.IO.Path]::GetFullPath($fp)
                        if ($fpFull.StartsWith($binFull, [System.StringComparison]::OrdinalIgnoreCase)) {
                            $matchBin = $true
                        }
                    }
                    catch {}
                }
                if (-not $matchBin -and ($fp -match 'fcitx5.*x86_64\.dll' -or $fp -match '[\\/]Fcitx5ImePipeServer\.exe')) {
                    $matchBin = $true
                }
                if ($matchBin) {
                    $rows.Add([PSCustomObject]@{
                            ProcessId   = $proc.Id
                            ProcessName = $proc.ProcessName
                            Kind        = 'Module'
                            Path        = $fp
                        })
                }
            }
        }
        catch {
            # Access denied for some protected processes (e.g. System, csrss)
        }
    }
    return $rows | Sort-Object ProcessId, Kind, Path -Unique
}
