param(
    [string]$QtRoot='C:/Qt/6.8.3/msvc2022_64',
    [string]$BuildDir='',
    [string]$Destination=''
)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$BuildDir){$BuildDir=Join-Path $projectRoot 'build'}
if(!$Destination){$Destination=Join-Path $projectRoot 'dist/SpineLoveEX-windows-x64'}
$app=Join-Path $BuildDir 'main/qt/spinelove_qt.exe'
$launcher=Join-Path $BuildDir 'SpineLoveEX.exe'
if(!(Test-Path -LiteralPath $app)){throw 'Build the application first.'}
if(!(Test-Path -LiteralPath $launcher)){throw 'Build the launcher first.'}
if((Test-Path -LiteralPath $Destination) -and (Get-ChildItem -LiteralPath $Destination -Force | Select-Object -First 1)){throw 'Use a new empty packaging directory.'}
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$internal=Join-Path $Destination '_internal'
$bin=Join-Path $internal 'bin'
$assets=Join-Path $internal 'assets'
$licenses=Join-Path $internal 'licenses'
New-Item -ItemType Directory -Force -Path $bin,$assets,$licenses | Out-Null
Copy-Item -LiteralPath $launcher -Destination (Join-Path $Destination 'SpineLoveEX.exe')
Copy-Item -LiteralPath $app -Destination $bin
Copy-Item -LiteralPath (Join-Path $projectRoot 'main/NotoSansSC-Regular.ttf') -Destination $assets
$shaderDir=Join-Path $assets 'render_d3d11/shaders'
New-Item -ItemType Directory -Force -Path $shaderDir | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'main/render_d3d11/shaders/sprite.hlsl') -Destination $shaderDir
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsRoot=''
if(Test-Path -LiteralPath $vswhere){$vsRoot=& $vswhere -all -prerelease -products '*' -property installationPath | Where-Object {Test-Path -LiteralPath (Join-Path $_ 'VC/Redist/MSVC')} | Select-Object -First 1}
if(!$vsRoot){$vsRoot=Join-Path $env:ProgramFiles 'Microsoft Visual Studio/2022/Community'}
if(Test-Path -LiteralPath $vsRoot){$env:VCINSTALLDIR=Join-Path $vsRoot 'VC'}
& (Join-Path $QtRoot 'bin/windeployqt.exe') --release --qmldir (Join-Path $projectRoot 'main/qt') --no-translations --no-compiler-runtime (Join-Path $bin 'spinelove_qt.exe')
if($LASTEXITCODE -ne 0){throw 'Qt deployment failed.'}
$crtPath=Get-ChildItem -LiteralPath (Join-Path $vsRoot 'VC/Redist/MSVC') -Directory | Where-Object {$_.Name -match '^\d+(\.\d+)+$'} | Sort-Object {[version]$_.Name} -Descending | ForEach-Object {Join-Path $_.FullName 'x64/Microsoft.VC143.CRT'} | Where-Object {Test-Path -LiteralPath $_} | Select-Object -First 1
if(!$crtPath){throw 'The MSVC x64 runtime was not found.'}
Get-ChildItem -LiteralPath $crtPath -File -Filter '*.dll' | Copy-Item -Destination $bin
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination $licenses
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSES') -Destination $licenses -Recurse
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $licenses
Copy-Item -LiteralPath (Join-Path $projectRoot 'README_en.md') -Destination $licenses
$archive=$Destination+'.zip'
if(Test-Path -LiteralPath $archive){throw 'The archive already exists.'}
Compress-Archive -LiteralPath $Destination -DestinationPath $archive
Write-Output $archive
