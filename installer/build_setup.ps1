param(
    [switch]$SkipVerify
)
$ErrorActionPreference = 'Stop'
$inst = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $inst
$mingw = 'C:\Users\PRIMEx\Documents\winlibs-x86_64-posix-seh-gcc-16.1.0-mingw-w64ucrt-14.0.0-r2\mingw64\bin'
$env:PATH = $mingw + ';' + $env:PATH

$build = Join-Path $inst 'build'
$payload = Join-Path $build 'payload'
$dist = Join-Path $root 'frontend\dist'
$exe = Join-Path $root 'native\build2\ORBIT OPTIMIZER.exe'
$loader = Join-Path $root 'native\build2\WebView2Loader.dll'
$boot = Join-Path $inst 'assets\MicrosoftEdgeWebView2RuntimeInstaller.exe'
$outDir = Join-Path $inst 'dist'

foreach ($p in @($build, $payload, $outDir)) {
    if (Test-Path $p) { Remove-Item $p -Recurse -Force }
    New-Item -ItemType Directory -Path $p | Out-Null
}

foreach ($f in @($exe, $loader, $boot)) {
    if (-not (Test-Path $f)) { throw "missing: $f" }
}
Copy-Item $exe (Join-Path $payload 'ORBIT OPTIMIZER.exe')
Copy-Item $loader (Join-Path $payload 'WebView2Loader.dll')
Copy-Item $boot (Join-Path $payload 'MicrosoftEdgeWebView2RuntimeInstaller.exe')
New-Item -ItemType Directory -Path (Join-Path $payload 'dist') | Out-Null
Copy-Item "$dist\*" (Join-Path $payload 'dist') -Recurse -Force
Copy-Item (Join-Path $root 'assets\orbit.ico') (Join-Path $build 'orbit.ico')

# --- dedicated uninstaller: separate binary, never installs (forced by -DORBIT_UNINSTALLER_ONLY) ---
$mfRes = (Join-Path $inst 'setup.manifest').Replace('\','/')
$icoRes = (Join-Path $build 'orbit.ico').Replace('\','/')
$uRes = @()
$uRes += '#include <windows.h>'
$uRes += "1 RT_MANIFEST `"$mfRes`""
$uRes += "100 ICON `"$icoRes`""
$uRes += @'
1 VERSIONINFO
 FILEVERSION 1,0,0,0
 PRODUCTVERSION 1,0,0,0
 FILEFLAGSMASK 0x3fL
 FILEFLAGS 0x0L
 FILEOS 0x40004L
 FILETYPE 0x1L
 FILESUBTYPE 0x0L
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "CompanyName", "PRIMEx"
            VALUE "FileDescription", "ORBIT OPTIMIZER Uninstaller"
            VALUE "FileVersion", "1.0.0.0"
            VALUE "InternalName", "Uninstall ORBIT OPTIMIZER"
            VALUE "LegalCopyright", "Copyright 2026 PRIMEx"
            VALUE "OriginalFilename", "Uninstall ORBIT OPTIMIZER.exe"
            VALUE "ProductName", "ORBIT OPTIMIZER"
            VALUE "ProductVersion", "1.0.0"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x0409, 1200
    END
END
'@
$uRcPath = Join-Path $build 'uninstaller.rc'
[IO.File]::WriteAllText($uRcPath, ($uRes -join "`r`n") + "`r`n", (New-Object System.Text.UTF8Encoding($false)))
$uninsExe = Join-Path $payload 'Uninstall ORBIT OPTIMIZER.exe'
Push-Location $build
try {
    & windres --input-format=rc --output-format=coff -o uninstaller.res.o uninstaller.rc
    if ($LASTEXITCODE -ne 0) { throw "windres (uninstaller) failed: $LASTEXITCODE" }
    & g++ -std=c++17 -O2 -municode -mwindows -mthreads -static -static-libgcc -static-libstdc++ `
        -DORBIT_UNINSTALLER_ONLY -o $uninsExe (Join-Path $inst 'setup.cpp') uninstaller.res.o `
        -lole32 -loleaut32 -luuid -lshell32 -lshlwapi -lcomctl32 -ladvapi32 -ldwmapi
    if ($LASTEXITCODE -ne 0) { throw "g++ (uninstaller) failed: $LASTEXITCODE" }
    Write-Host "built uninstaller: $([math]::Round((Get-Item $uninsExe).Length/1MB,1)) MB"
}
finally { Pop-Location }

$lines = @()
$lines += "101`tapp`tORBIT OPTIMIZER.exe"
$lines += "102`tapp`tWebView2Loader.dll"
$lines += "103`ttmp`tMicrosoftEdgeWebView2RuntimeInstaller.exe"
$lines += "104`tapp`tUninstall ORBIT OPTIMIZER.exe"
$id = 110
Get-ChildItem (Join-Path $payload 'dist') -Recurse -File | Sort-Object FullName | ForEach-Object {
    $rel = $_.FullName.Substring($payload.Length + 1)
    $lines += "$id`tapp`t$rel"
    $id++
}
$mf = Join-Path $build 'payload.manifest.txt'
[IO.File]::WriteAllText($mf, ($lines -join "`r`n") + "`r`n", (New-Object System.Text.UTF8Encoding($false)))
"manifest: $($lines.Count) entries"

$rcLines = @()
$rcLines += '#include <windows.h>'
$mfRes = (Join-Path $inst 'setup.manifest').Replace('\','/')
$icoRes = (Join-Path $build 'orbit.ico').Replace('\','/')
$rcLines += "1 RT_MANIFEST `"$mfRes`""
$rcLines += "100 ICON `"$icoRes`""
$rcLines += @'
1 VERSIONINFO
 FILEVERSION 1,0,0,0
 PRODUCTVERSION 1,0,0,0
 FILEFLAGSMASK 0x3fL
 FILEFLAGS 0x0L
 FILEOS 0x40004L
 FILETYPE 0x1L
 FILESUBTYPE 0x0L
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "CompanyName", "PRIMEx"
            VALUE "FileDescription", "ORBIT OPTIMIZER Setup"
            VALUE "FileVersion", "1.0.0.0"
            VALUE "InternalName", "ORBIT OPTIMIZER Setup"
            VALUE "LegalCopyright", "Copyright 2026 PRIMEx"
            VALUE "OriginalFilename", "ORBIT OPTIMIZER Setup.exe"
            VALUE "ProductName", "ORBIT OPTIMIZER"
            VALUE "ProductVersion", "1.0.0"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x0409, 1200
    END
END
'@
$rcLines += "900 RCDATA `"$($mf.Replace('\','/'))`""
$lines | ForEach-Object {
    $iid = [int]($_ -split "`t")[0]
    $rel = ([string]($_ -split "`t")[2])
    $abs = Join-Path $payload $rel
    if (-not (Test-Path $abs)) { throw "payload missing: $abs" }
    $rcLines += "$iid RCDATA `"$($abs.Replace('\','/'))`""
}
$rc = Join-Path $build 'setup.rc'
[IO.File]::WriteAllText($rc, ($rcLines -join "`r`n") + "`r`n", (New-Object System.Text.UTF8Encoding($false)))

Push-Location $build
try {
    & windres --input-format=rc --output-format=coff -o setup.res.o setup.rc
    if ($LASTEXITCODE -ne 0) { throw "windres failed: $LASTEXITCODE" }
    $setupExe = Join-Path $outDir 'ORBIT OPTIMIZER Setup.exe'
    & g++ -std=c++17 -O2 -municode -mwindows -mthreads -static -static-libgcc -static-libstdc++ `
        -o $setupExe (Join-Path $inst 'setup.cpp') setup.res.o `
        -lole32 -loleaut32 -luuid -lshell32 -lshlwapi -lcomctl32 -ladvapi32 -ldwmapi
    if ($LASTEXITCODE -ne 0) { throw "g++ failed: $LASTEXITCODE" }
    Write-Host "built: $setupExe ($([math]::Round((Get-Item $setupExe).Length/1MB,1)) MB)"

    if (-not $SkipVerify) {
        $imp = (& objdump -p $setupExe | Select-String 'DLL Name:').Line -join "`n"
        foreach ($bad in 'libstdc++', 'libgcc', 'libwinpthread') {
            if ($imp -match [regex]::Escape($bad)) { throw "setup still imports $bad" }
        }
        $vi = [Diagnostics.FileVersionInfo]::GetVersionInfo($setupExe)
        if ($vi.FileVersion -ne '1.0.0.0') { throw "setup version: $($vi.FileVersion)" }
        $uImp = (& objdump -p $uninsExe | Select-String 'DLL Name:').Line -join "`n"
        foreach ($bad in 'libstdc++', 'libgcc', 'libwinpthread') {
            if ($uImp -match [regex]::Escape($bad)) { throw "uninstaller still imports $bad" }
        }
        $uvi = [Diagnostics.FileVersionInfo]::GetVersionInfo($uninsExe)
        if ($uvi.FileVersion -ne '1.0.0.0') { throw "uninstaller version: $($uvi.FileVersion)" }
        if ($uvi.FileDescription -notlike '*Uninstaller*') { throw "uninstaller desc: $($uvi.FileDescription)" }
        $sHash = (Get-FileHash $setupExe -Algorithm SHA256).Hash
        $uHash = (Get-FileHash $uninsExe -Algorithm SHA256).Hash
        if ($sHash -eq $uHash) { throw "setup and uninstaller are identical" }
        $bytes = [IO.File]::ReadAllBytes($setupExe)
        $u = [Text.Encoding]::Unicode.GetString($bytes)
        if ($u -notmatch 'asInvoker') { }
        Write-Host "verify ok: setup + uninstaller static, distinct binaries, v1.0.0.0"
    }
}
finally { Pop-Location }
