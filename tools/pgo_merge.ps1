# The console's PGO sessions (pgo/session-NN.stream from the instrumented NRO, switch/pgo.cpp) into the .gcda files of
# build-switch, which `PGO=use ./switch/build.sh` reads. The old .gcda files go first; every session given is added up
# (gcov-tool merge-stream merges into the files a stream names).
# pgo_merge.ps1 <folder with the .stream files>
param([Parameter(Mandatory = $true)][string]$Streams)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$tool = 'D:\msys64\opt\devkitpro\devkitA64\bin\aarch64-none-elf-gcov-tool.exe'
$build = Join-Path $root 'build-switch'
Get-ChildItem -Recurse -Filter *.gcda $build | Remove-Item
$files = @(Get-ChildItem -Filter 'session-*.stream' $Streams | Sort-Object Name)
if (-not $files) { throw "No session-*.stream in $Streams" }
foreach ($f in $files) {
    Write-Host "merging $($f.Name) ($([math]::Round($f.Length / 1MB, 1)) MiB)"
    & $tool merge-stream $f.FullName
    if ($LASTEXITCODE) { throw "gcov-tool failed on $($f.Name)" }
}
# pgo.cpp is other code in the build that uses the profile (empty functions): its counters would not match
Get-ChildItem -Recurse -Filter pgo.gcda $build | Remove-Item
$gcda = @(Get-ChildItem -Recurse -Filter *.gcda $build)
Write-Host "$($gcda.Count) .gcda files in build-switch"
# ninja does not see the .gcda files: without this the next PGO=use build only links the objects of the old profile
Get-ChildItem -Recurse -Filter *.o (Join-Path $build 'CMakeFiles\street2.dir') | Remove-Item
