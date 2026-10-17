@echo off
setlocal
rem Run in an x64 Native Tools Command Prompt for Visual Studio.
pushd "%~dp0..\.."
call tools\tests\run_arcade_query_bridge_test.cmd
if errorlevel 1 goto fail
if not exist artifacts\HookTests mkdir artifacts\HookTests
cl /nologo /EHsc /W4 /std:c++20 /O2 /Fo:artifacts\HookTests\ui.obj /Fe:artifacts\HookTests\ui.exe tools\tests\ui_input_wrapper_test.cpp
if errorlevel 1 goto fail
cl /nologo /EHsc /W4 /std:c++20 /O2 /Fo:artifacts\HookTests\validation.obj /Fe:artifacts\HookTests\validation.exe tools\tests\hook_validation_test.cpp
if errorlevel 1 goto fail
pushd artifacts\HookTests
ui.exe
if errorlevel 1 goto testfail
validation.exe
if errorlevel 1 goto testfail
popd
popd
exit /b 0
:testfail
popd
:fail
popd
exit /b 1
