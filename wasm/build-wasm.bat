@echo off
rem Compile the pure emission core + C bridge to WebAssembly with Emscripten.
rem Requires emcc on PATH (emsdk: install latest, activate latest, emsdk_env.bat).
rem Output: web\src\generated\codegen.mjs + codegen.wasm (committed, so the web
rem app builds without emsdk installed).
rem
rem Equivalent sh command (single line):
rem   emcc -O2 -std=c++20 -fwasm-exceptions -Icore/include -Ithird_party \
rem     core/src/naming.cpp core/src/validator.cpp core/src/type_map.cpp \
rem     core/src/api_spec.cpp core/src/emit.cpp wasm/wrapper.cpp \
rem     -sMODULARIZE=1 -sEXPORT_ES6=1 \
rem     -sEXPORTED_FUNCTIONS=_convex_ue_codegen_generate,_convex_ue_codegen_free,_malloc,_free \
rem     -sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToUTF8,lengthBytesUTF8 \
rem     -sALLOW_MEMORY_GROWTH=1 --no-entry -o web/src/generated/codegen.mjs
setlocal

set "ROOT=%~dp0.."

where emcc >nul 2>nul
if errorlevel 1 (
    echo error: emcc not found on PATH. Install and activate the Emscripten SDK:>&2
    echo   git clone https://github.com/emscripten-core/emsdk ^&^& cd emsdk>&2
    echo   emsdk.bat install latest ^&^& emsdk.bat activate latest ^&^& emsdk_env.bat>&2
    exit /b 1
)

if not exist "%ROOT%\web\src\generated" mkdir "%ROOT%\web\src\generated"

rem -fwasm-exceptions: the core reports malformed input via C++ exceptions the
rem wrapper catches; without it, emcc's default (exceptions disabled) turns
rem every throw into an abort before the catch runs.
call emcc -O2 -std=c++20 -fwasm-exceptions ^
    -I"%ROOT%\core\include" -I"%ROOT%\third_party" ^
    "%ROOT%\core\src\naming.cpp" ^
    "%ROOT%\core\src\validator.cpp" ^
    "%ROOT%\core\src\type_map.cpp" ^
    "%ROOT%\core\src\api_spec.cpp" ^
    "%ROOT%\core\src\emit.cpp" ^
    "%ROOT%\wasm\wrapper.cpp" ^
    -sMODULARIZE=1 ^
    -sEXPORT_ES6=1 ^
    -sEXPORTED_FUNCTIONS=_convex_ue_codegen_generate,_convex_ue_codegen_free,_malloc,_free ^
    -sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToUTF8,lengthBytesUTF8 ^
    -sALLOW_MEMORY_GROWTH=1 ^
    --no-entry ^
    -o "%ROOT%\web\src\generated\codegen.mjs"
if errorlevel 1 exit /b 1

echo built web\src\generated\codegen.mjs + codegen.wasm
