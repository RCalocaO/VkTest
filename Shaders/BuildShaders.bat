@ECHO OFF
set COMPILE=%VULKAN_SDK%\Bin\glslangValidator.exe
for /r %%i in (*.vert) do %COMPILE% -V -r -H -l -o %%i.spv %%i
for /r %%i in (*.frag) do %COMPILE% -V -r -H -l -o %%i.spv %%i
for /r %%i in (*.comp) do %COMPILE% -V -r -H -l -o %%i.spv %%i
