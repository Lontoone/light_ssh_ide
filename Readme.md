# Manual install dependency

## QT

```PowerShell
winget install StrawberryPerl.StrawberryPerl
mkdir third-party\qt
cd third-party\qt
aqt install-qt windows desktop 6.10.0 win64_msvc2022_64 --base https://qt-mirror.dannhauer.de/
```

### QT code hightlight
```
git submodule add --depth 1 https://github.com/Waqar144/QSourceHighlite.git third-party/QSourceHighlite
cd third-party/QSourceHighlite
```



# Build 

```bash
winget install StrawberryPerl.StrawberryPerl
mkdir third-party\qt
cd third-party\qt
aqt install-qt windows desktop 6.8.1 win64_msvc2022_64 --base https://qt-mirror.dannhauer.de/

Remove-Item -Recurse -Force "build" -ErrorAction SilentlyContinue

# 設定專案（初次執行若無 Qt 會自動呼叫 aqt 下載）
cmake -B build -S . -DCMAKE_PREFIX_PATH="third-party\qt\msvc2022_64"

cmake --build build --config Release

```
