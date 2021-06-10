@echo off

setlocal

call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"

set CommonCompilerFlags= -MTd -I%VULKAN_SDK%\Include -I..\external\SPIRV-Reflect -I..\external\imgui -I..\external\lz4\lib -I..\external\json\single_include -I..\external\VulkanMemoryAllocator\src -I..\external\vk-bootstrap\src -I..\external\stb -I..\external\tinyobjloader -I..\external\glm -diagnostics:column -WL -Od -nologo -fp:fast -fp:except- -Gm- -GR- -EHa- -Zo -Oi -WX -W4 -wd4324 -wd4201 -wd4530 -wd4100 -wd4189 -wd4505 -wd4127 -wd4239 -FC -GS- -Gs9999999
set CompilerFlags2= -O2 -nologo -fp:fast -WX -W4 -wd4324 -wd4201 -wd4530 -wd4100 -wd4189 -wd4505 -wd4127 -wd4239
set AssetsConverterFlags= -MT -I..\external\lz4\lib -I..\external\json\single_include -I..\external\stb -I..\external\tinyobjloader -I..\external\glm
set DebugCompilerFlags= /Z7 /FC /LD

set CompilerFlags= /c /std:c++17 %CommonCompilerFlags% -DVK_USE_PLATFORM_WIN32_KHR -D_CRT_SECURE_NO_WARNINGS -DVKE_INTERNAL=1 -DVKE_SLOW=1 -DVKE_WIN32=1 %DebugCompilerFlags%
set AssetsConverterCompilerFlags= /c /std:c++17 %AssetsConverterFlags% -DWIN32 -DNDEBUG -D_WINDOWS -DVK_USE_PLATFORM_WIN32_KHR -D_CRT_SECURE_NO_WARNINGS -DVKE_INTERNAL=1 -DVKE_SLOW=0 -DVKE_CONSOLE=1 %CompilerFlags2%
set LibCompilerFlags= /c /std:c++17

set AssetsConverterLinkerFlags= /SUBSYSTEM:console
set CommonLinkerFlags= /SUBSYSTEM:console /STACK:0x100000,0x100000 /debug:full /incremental:no /opt:ref /PDB:..\.dbg\main.pdb user32.lib gdi32.lib Shell32.lib ole32.lib %VULKAN_SDK%\Lib\vulkan-1.lib
rem /SUBSYSTEM:windows /STACK:0x100000,0x100000 /debug:full /incremental:no /opt:ref /PDB:..\.dbg\win32_disus.pdb user32.lib gdi32.lib ..\src\vulkan-1.lib
rem winmm.lib kernel32.lib

IF NOT EXIST .bin mkdir .bin
IF NOT EXIST .objs mkdir .objs
IF NOT EXIST .dbg mkdir .dbg

pushd .bin

del ..\.dbg\*.pdb > NUL 2> NUL

echo ##############################
echo ### Compile Vulkan Shaders ###
echo ##############################

call "%VULKAN_SDK%\Bin\glslangValidator.exe" -V ..\data\shaders\triangle.vert -o ..\data\shaders\triangle.vert.spv
call "%VULKAN_SDK%\Bin\glslangValidator.exe" -V ..\data\shaders\triangle.frag -o ..\data\shaders\triangle.frag.spv

call "%VULKAN_SDK%\Bin\glslangValidator.exe" -V ..\data\shaders\colored_triangle.vert -o ..\data\shaders\colored_triangle.vert.spv
call "%VULKAN_SDK%\Bin\glslangValidator.exe" -V ..\data\shaders\colored_triangle.frag -o ..\data\shaders\colored_triangle.frag.spv

call "%VULKAN_SDK%\Bin\glslangValidator.exe" -V ..\data\shaders\tri_mesh.vert -o ..\data\shaders\tri_mesh.vert.spv
call "%VULKAN_SDK%\Bin\glslangValidator.exe" -V ..\data\shaders\default_lit.frag -o ..\data\shaders\default_lit.frag.spv
call "%VULKAN_SDK%\Bin\glslangValidator.exe" -V ..\data\shaders\textured_lit.frag -o ..\data\shaders\textured_lit.frag.spv

@REM echo ########################
@REM echo ### Compile Boostrap ###
@REM echo ########################

@REM cl %CompilerFlags% /Fo..\.objs\vk_bootstrap.obj ..\external\vk-bootstrap\src\VkBootstrap.cpp

echo #######################
echo ### Compile Program ###
echo #######################

cl %CompilerFlags% /Fo..\.objs\main.obj ..\src\main.cpp

link %CommonLinkerFlags% /MAP:..\.dbg\main.map ..\.objs\main.obj /out:engine.exe

@REM cl %AssetsConverterCompilerFlags% /Fo..\.objs\assets_converter.obj ..\src\asset\assets_converter.cpp

@REM link %AssetsConverterLinkerFlags% ..\.objs\assets_converter.obj /out:assets_converter.exe

@rem ..\.objs\vk_bootstrap.obj

IF NOT EXIST data mklink /j "data" "../data"

popd