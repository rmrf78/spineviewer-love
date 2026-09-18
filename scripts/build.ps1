param(
    [string]$QtRoot='C:/Qt/6.8.3/msvc2022_64',
    [string]$BuildDir='',
    [string]$VsRoot='',
    [string]$ShaderToolsRoot='',
    [ValidateSet('Release','Debug')][string]$Configuration='Release'
)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$BuildDir){$BuildDir=Join-Path $projectRoot 'build'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if(!$VsRoot -and (Test-Path -LiteralPath $vswhere)){
    $VsRoot=& $vswhere -all -prerelease -products '*' -property installationPath | Where-Object {Test-Path -LiteralPath (Join-Path $_ 'VC/Auxiliary/Build/vcvars64.bat')} | Select-Object -First 1
}
if(!$VsRoot){$VsRoot=Join-Path $env:ProgramFiles 'Microsoft Visual Studio/2022/Community'}
$vcvars=Join-Path $VsRoot 'VC/Auxiliary/Build/vcvars64.bat'
if(!(Test-Path -LiteralPath $vcvars)){throw 'Visual Studio 2022 C++ tools are required; pass -VsRoot for a custom installation.'}
$buildEnvironment=& $env:ComSpec /d /c ('"{0}" >nul && set' -f $vcvars)
if($LASTEXITCODE -ne 0){throw 'MSVC environment initialization failed.'}
foreach($line in $buildEnvironment){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$env:VSLANG='1033'
$env:PATH=(Join-Path $QtRoot 'bin')+';'+$env:PATH
$cmake=Join-Path $VsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja=Join-Path $VsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$prefixPath=$QtRoot
$extraArgs=@()
if($ShaderToolsRoot){
    $prefixPath+=';'+$ShaderToolsRoot
    $extraArgs+=('-DQt6ShaderTools_DIR='+ (Join-Path $ShaderToolsRoot 'lib/cmake/Qt6ShaderTools'))
    $extraArgs+=('-DQt6ShaderToolsTools_DIR='+ (Join-Path $ShaderToolsRoot 'lib/cmake/Qt6ShaderToolsTools'))
}
& $cmake -S $projectRoot -B $BuildDir -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_PREFIX_PATH=$prefixPath" "-DCMAKE_BUILD_TYPE=$Configuration" @extraArgs
if($LASTEXITCODE -ne 0){throw 'CMake configuration failed.'}
& $cmake --build $BuildDir --target spinelove_qt SpineLoveEX --parallel 6
if($LASTEXITCODE -ne 0){throw 'Build failed.'}
