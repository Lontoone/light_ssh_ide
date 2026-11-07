
# Build 

```
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```


```
Remove-Item -Recurse -Force "build" -ErrorAction SilentlyContinue

# 設定專案（初次執行若無 Qt 會自動呼叫 aqt 下載）
cmake -B build -S .

# 編譯 Release 版本
cmake --build build --config Release
```