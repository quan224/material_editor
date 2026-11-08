@echo off
rem Compile HLSL shaders to SPIR-V. Run this after editing .hlsl files.
rem -T = stage (vs/ps) _6_0 = Shader Model 6.0; -E = entry point; -spirv = Vulkan bytecode
rem vs_6_0 = VK_SHADER_STAGE_VERTEX_BIT / ps_6_0 = VK_SHADER_STAGE_FRAGMENT_BIT

set "DXC=C:\VulkanSDK\1.4.363.0\Bin\dxc.exe"
set "SHADER_DIR=%~dp0"

"%DXC%" -T vs_6_0 -E main -spirv -Fo "%SHADER_DIR%\vert.spv" "%SHADER_DIR%\shader.vert.hlsl"
if errorlevel 1 goto :err

"%DXC%" -T ps_6_0 -E main -spirv -Fo "%SHADER_DIR%\frag.spv" "%SHADER_DIR%\shader.frag.hlsl"
if errorlevel 1 goto :err

echo OK: vert.spv + frag.spv
pause
exit /b 0

:err
echo COMPILE FAILED - see error line above
pause
exit /b 1
