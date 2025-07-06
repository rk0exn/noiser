@echo off
rem "This command requires VS2022 Community."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
fxc /T cs_5_0 /E main "%~dp0noise_compute.hlsl" /Fo "%~dp0noise_compute.cso"