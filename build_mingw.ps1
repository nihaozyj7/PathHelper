<#
    PathHelper - MinGW-w64 (g++) 构建脚本
    ---------------------------------------------------------------
    用途：在没有安装 Visual Studio 的机器上，用 g++ 直接编译出
          Release\PathHelper.exe 和 Release\Dll1.dll（x64）。

    依赖：MinGW-w64 (g++ / windres)，建议 UCRT 版、GCC 11 以上。
          若 g++ 已在 PATH 中，直接运行：
              powershell -ExecutionPolicy Bypass -File .\build_mingw.ps1
          清理后重新编译：
              powershell -ExecutionPolicy Bypass -File .\build_mingw.ps1 -Clean

    说明：
      * 产物为静态链接，不依赖 libstdc++-6.dll / libwinpthread-1.dll。
      * Dll1.dll / Dll2.dll 必须和 PathHelper.exe 放在同一目录（注入器按此查找）。
        Dll1.dll = 文件对话框增强面板；Dll2.dll = 资源管理器收藏面板。
      * Visual Studio 工程 (.sln/.vcxproj) 依然可用，本脚本只是另一条构建路径。
      * 脚本内部全部使用相对路径：windres 无法处理含中文的绝对路径。
#>
[CmdletBinding()]
param(
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $root

$buildDir = 'build'
$outDir   = 'Release'
$LF       = [string][char]10

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host '==> 清理 build 目录'
    Remove-Item -Recurse -Force $buildDir
}
New-Item -ItemType Directory -Force -Path $buildDir, $outDir | Out-Null

function Resolve-Tool([string]$name) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if (-not $cmd) { throw "找不到 $name，请先安装 MinGW-w64 并把它加入 PATH" }
    return $cmd.Source
}

$gpp     = Resolve-Tool 'g++'
$windres = Resolve-Tool 'windres'
Write-Host "g++     : $gpp"
Write-Host "windres : $windres"

$incPathHelper = 'PathHelper'
$incDll1       = 'Dll1'
$incDll2       = 'Dll2'

# 公共编译参数：x64 / Unicode / C++17 / 静态链接
$common = @(
    '-std=c++17', '-O2', '-static',
    '-DUNICODE', '-D_UNICODE', '-DNDEBUG', '-D_WINDOWS',
    '-D_WIN32_WINNT=0x0A00', '-DWINVER=0x0A00'
)

# ---------------------------------------------------------------
# 1. 资源文件
#    VS 保存的 .rc 是 UTF-16LE，windres 只接受单字节编码，
#    这里先转成 UTF-8，再用 --codepage=65001 编译。
# ---------------------------------------------------------------
$rcUtf8      = Join-Path $buildDir 'PathHelper.rc'
$rcRes       = Join-Path $buildDir 'PathHelper.res'
$manifestRc  = Join-Path $buildDir 'manifest.rc'
$manifestRes = Join-Path $buildDir 'manifest.res'

[System.IO.File]::WriteAllText(
    $rcUtf8,
    [System.IO.File]::ReadAllText((Join-Path $incPathHelper 'PathHelper.rc')),
    (New-Object System.Text.UTF8Encoding($false)))

Set-Content -Encoding ASCII -Path $manifestRc -Value '1 24 "PathHelper.manifest"'

foreach ($pair in @(@($rcUtf8, $rcRes), @($manifestRc, $manifestRes))) {
    & $windres --codepage=65001 -I $incPathHelper -O coff $pair[0] -o $pair[1]
    if ($LASTEXITCODE -ne 0) { throw "windres 编译失败: $($pair[0])" }
}

# ---------------------------------------------------------------
# 2. 自定义 specs
#    MinGW 的 gcc 通过 specs 自动链入自带的 default-manifest.o，
#    它和我们自己的清单资源冲突（.rsrc merge failure）。
#    这里把 dumpspecs 里注入 default-manifest.o 的片段去掉。
# ---------------------------------------------------------------
$specsArg  = $null
$specsText = [string]::Join($LF, (& $gpp -dumpspecs))
$dmToken   = '%{!shared:%:if-exists(default-manifest.o%s)}'
if ($specsText.Contains($dmToken)) {
    $specsFile = Join-Path $buildDir 'mingw.specs'
    [System.IO.File]::WriteAllText(
        $specsFile,
        $specsText.Replace($dmToken, ''),
        (New-Object System.Text.UTF8Encoding($false)))
    $specsArg = '-specs=' + ($specsFile -replace '\\', '/')
    Write-Host '==> 已生成自定义 specs（关闭 MinGW 默认清单，改用自带清单）'
}
else {
    Write-Host '==> 未发现 default-manifest.o 注入点，跳过自定义 specs'
}

# ---------------------------------------------------------------
# 3. PathHelper.exe（托盘主程序 + DLL 注入器）
# ---------------------------------------------------------------
Write-Host '==> 编译 PathHelper.exe'
$exeOut = Join-Path $outDir 'PathHelper.exe'
$exeArgs = $common.Clone()
$exeArgs += @('-municode', '-mwindows', '-I', $incPathHelper)
if ($specsArg) { $exeArgs += $specsArg }
$exeArgs += @(Get-ChildItem (Join-Path $incPathHelper '*.cpp') |
              ForEach-Object { Join-Path $incPathHelper $_.Name })
$exeArgs += @($rcRes, $manifestRes, '-o', $exeOut)
$exeArgs += @('-lole32', '-loleaut32', '-luuid', '-lshell32', '-lcomctl32',
              '-luxtheme', '-lshlwapi', '-luser32', '-lgdi32', '-ladvapi32')
& $gpp @exeArgs
if ($LASTEXITCODE -ne 0) { throw 'PathHelper.exe 编译失败' }

# ---------------------------------------------------------------
# 4. Dll1.dll（注入到文件对话框进程的伴侣面板）
# ---------------------------------------------------------------
Write-Host '==> 编译 Dll1.dll'
$dllOut = Join-Path $outDir 'Dll1.dll'
$dllArgs = $common.Clone()
$dllArgs += @('-shared', '-D_USRDLL', '-DDLL1_EXPORTS', '-I', $incDll1)
$dllArgs += @(Get-ChildItem (Join-Path $incDll1 '*.cpp') |
              ForEach-Object { Join-Path $incDll1 $_.Name })
$dllArgs += @('-o', $dllOut)
$dllArgs += @('-lole32', '-loleaut32', '-luuid', '-lshell32', '-lcomctl32',
              '-ld2d1', '-ldwrite', '-luxtheme', '-lshlwapi', '-luser32',
              '-lgdi32', '-ladvapi32', '-luiautomationcore')
& $gpp @dllArgs
if ($LASTEXITCODE -ne 0) { throw 'Dll1.dll 编译失败' }

# ---------------------------------------------------------------
# 5. Dll2.dll（注入到 explorer.exe 的收藏面板）
# ---------------------------------------------------------------
Write-Host '==> 编译 Dll2.dll'
$favOut = Join-Path $outDir 'Dll2.dll'
$favArgs = $common.Clone()
$favArgs += @('-shared', '-D_USRDLL', '-I', $incDll2)
$favArgs += @(Get-ChildItem (Join-Path $incDll2 '*.cpp') |
              ForEach-Object { Join-Path $incDll2 $_.Name })
$favArgs += @('-o', $favOut)
$favArgs += @('-lole32', '-loleaut32', '-luuid', '-lshell32', '-lcomctl32',
              '-lshlwapi', '-luser32', '-lgdi32', '-ladvapi32')
& $gpp @favArgs
if ($LASTEXITCODE -ne 0) { throw 'Dll2.dll 编译失败' }

# ---------------------------------------------------------------
# 6. 结果
# ---------------------------------------------------------------
Write-Host ''
Write-Host '构建完成，输出目录: ' -NoNewline
Write-Host (Resolve-Path $outDir).Path
Get-ChildItem $outDir |
    Select-Object Name, @{n = 'Size(KB)'; e = { [math]::Round($_.Length / 1KB, 1) } }, LastWriteTime |
    Format-Table -AutoSize | Out-String -Width 120 | Write-Host
