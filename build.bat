@echo off
:: 1. 確保 Submodule 已初始化並拉取
git submodule update --init --recursive

:: 2. 清理舊的 build 資料夾（若之前失敗過）
::Remove-Item -Recurse -Force "build" -ErrorAction SilentlyContinue
::if exist "build" rmdir /s /q "build" 2>nul

:: 3. 執行 CMake 配置（若本地沒有 Qt 會自動透過 aqt 下載到 third-party/qt）
cmake -B build -S .

:: 4. 將自動下載的 Qt bin 加入當前終端機 PATH（動態取得相對路徑）
::$QtBinPath = Join-Path $PWD "third-party\qt\6.8.1\msvc2022_64\bin"
::if (Test-Path $QtBinPath) {
::    $env:Path = "$QtBinPath;" + $env:Path
::}
set "QtBinPath=%~dp0third-party\qt\6.8.1\msvc2022_64\bin"
if exist "%QtBinPath%" (
    set "PATH=%QtBinPath%;%PATH%"
)

:: 5. 編譯專案
cmake --build build --config Release