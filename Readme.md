
# Build 

```
winget install StrawberryPerl.StrawberryPerl
Remove-Item -Recurse -Force "build" -ErrorAction SilentlyContinue

# 設定專案（初次執行若無 Qt 會自動呼叫 aqt 下載）
cmake -B build -S .

# Have to set the downloaded qt by previous step.
$env:Path = "D:\Code4FUN\light-ssh-ide\third-party\qt\6.8.1\msvc2022_64\bin;" + $env:Path
cmake --build build --config Release

# 編譯 Release 版本
cmake --build build --config Release
```


```
cmake -B build -S .

# 1. 確保環境變數
$env:Path = "D:\Code4FUN\light-ssh-ide\third-party\qt\6.8.1\msvc2022_64\bin;" + $env:Path

# 2. 先單獨建置數據與資源模組
cmake --build "D:/Code4FUN/light-ssh-ide/build/syntax-highlighting/src/build_syntax_highlighting-build" --target SyntaxHighlightingData --config Release

# 3. 數據生成成功後，再編譯核心庫與整體專案
cmake --build build --config Release
```