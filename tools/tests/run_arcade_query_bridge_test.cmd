@echo off
setlocal
rem Run from an x64 Native Tools Command Prompt for Visual Studio.
pushd "%~dp0..\.."
if not exist artifacts\ArcadeQueryBridgeTest mkdir artifacts\ArcadeQueryBridgeTest
ml64 /nologo /c /Fo artifacts\ArcadeQueryBridgeTest\bridge.obj src\mm_arcade_query_bridge.asm
if errorlevel 1 goto fail
ml64 /nologo /c /Fo artifacts\ArcadeQueryBridgeTest\probe.obj tools\tests\arcade_query_probe.asm
if errorlevel 1 goto fail
cl /nologo /EHsc /W4 /std:c++20 /O2 /Fo:artifacts\ArcadeQueryBridgeTest\test.obj /Fe:artifacts\ArcadeQueryBridgeTest\test.exe tools\tests\arcade_query_bridge_test.cpp artifacts\ArcadeQueryBridgeTest\bridge.obj artifacts\ArcadeQueryBridgeTest\probe.obj
if errorlevel 1 goto fail
artifacts\ArcadeQueryBridgeTest\test.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
