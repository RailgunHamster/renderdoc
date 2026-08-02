# 新电脑操作文档：拿到项目 → 截到帧

> 假设：一台干净的 Windows 机器，已安装《无限暖暖》PC 版（能正常玩），拿到本项目（D:\git\rendertst-nikki，含 nikki\ 目录）。
> 目标：从零到按 F10 截帧。约 30 分钟。

---

## 第 0 步：前置条件

| 需要 | 说明 |
|---|---|
| Windows 10/11 x64 | 游戏已装好（`C:\Users\Administrator\game\InfinityNikki Launcher\`） |
| Visual Studio 2022+ | 只需 MSBuild + MSVC（`C:\Program Files\Microsoft Visual Studio\18\Community\...`） |
| Python + uv | 跑自动点击脚本（`pip install uv` 或直接 `pip install pyautogui pygetwindow`） |
| 管理员 PowerShell | 构建/部署/杀进程用 |

确认游戏目录结构：
```
C:\Users\Administrator\game\InfinityNikki Launcher\
├─ 1.3.1\xstarter.exe          # 启动器 UI（双击它开始）
└─ InfinityNikki\
   ├─ InfinityNikki.exe        # bootstrap（加载 VERSION.dll 代理）
   ├─ X6Game\Binaries\Win64\X6Game-Win64-Shipping.exe  # 真正的游戏进程
   └─ VERSION.dll / version_real.dll / rendertest.dll   # 部署后在这里
```

## 第 1 步：构建 rendertest.dll（约 5-10 分钟）

```powershell
# 前台运行（后台方式会挂起）；/nodeReuse:false 必须
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" `
  "D:\git\rendertst-nikki\renderdoc\renderdoc.vcxproj" `
  /p:SolutionDir=D:\git\rendertst-nikki\ /p:Configuration=Release /p:Platform=x64 `
  /m:8 /nodeReuse:false /v:minimal /nologo
```
- 产物：`D:\git\rendertst-nikki\x64\Release\rendertest.dll`（约 25.8MB，看 LastWriteTime 确认是新的）
- 报错 LNK1257（代码生成失败）：清理 `x64\Release\*.ipdb`、`*.iobj` 后重跑；还不行就删 `x64\Release\rendertest.dll` 再跑

## 第 2 步：编译 VERSION.dll 代理（约 1 分钟）

```powershell
# 必须先在 VS 开发者环境（vcvars64）下跑：直接调 cl.exe 会报 C1034（windows.h 不在路径集）
# 且 cmd 里绝对路径引号会被转义破坏（LNK1104 打不开 .def）→ 用相对路径 + workdir
cd D:\git\rendertst-nikki\nikki
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul 2>&1 && cl /nologo /LD /O2 /DUNICODE /D_UNICODE version_proxy.c /Fe:VERSION.dll /link /DEF:version_proxy.def"
```
- `version_proxy.def` 导出：GetFileVersionInfoA/W、VerQueryValueA/W 等 20 个符号（见 nikki\ 目录现有 .def，若缺失从 system version.dll 导出表生成）
- `version_real.dll`：复制 `C:\Windows\System32\version.dll` 改名为 `version_real.dll` 即可

## 第 3 步：消毒（必须！约 1 分钟）

DLL 里的 "renderdoc/RenderDoc/RENDERDOC" 字符串会被 ACE 反作弊按特征杀进程，必须等长替换成 "rendertst/RenderTst/RENDERTST"（ASCII + UTF-16 两种编码）。

```powershell
# 用 nikki\sanitize_dll.ps1（或手动字节替换），输出 nikki\test\rendertest.dll
# 验证：替换计数应约 600 处
& "D:\git\rendertst-nikki\nikki\sanitize_dll.ps1"
```

> 说明：源码里 marker/开关路径已硬编码为 `D:\git\rendertst-nikki\nikki\`（真目录），消毒只替换品牌字符串（"rendertst" 不是替换目标），路径不会被改动，**无需 junction**。
> 若项目路径不同：把源码里所有 `D:\git\rendertst-nikki` 等长替换为新路径（搜 "rendertst-nikki"，注意等长才能保住二进制偏移），然后重新构建 + 消毒。

## 第 4 步：部署（约 1 分钟）

```powershell
# 1. 关闭所有游戏相关进程（DLL 被加载会锁文件）
Get-Process | Where-Object { $_.Name -match "X6Game|Nikki|xstarter" } | Stop-Process -Force

# 2. 复制三个文件到游戏根目录
$g = "C:\Users\Administrator\game\InfinityNikki Launcher\InfinityNikki"
Copy-Item "D:\git\rendertst-nikki\nikki\test\rendertest.dll" "$g\rendertest.dll" -Force
Copy-Item "D:\git\rendertst-nikki\nikki\VERSION.dll"          "$g\VERSION.dll" -Force
Copy-Item "D:\git\rendertst-nikki\nikki\version_real.dll"     "$g\version_real.dll" -Force

# 3. 创建必需开关（存在=生效）
New-Item -ItemType File "D:\git\rendertst-nikki\nikki\nikkiproxy_skip_registerhooks.txt" -Force
```

**开关文件清单**（都在 `D:\git\rendertst-nikki\nikki\`）：
| 文件 | 正常部署 |
|---|---|
| nikkiproxy_skip_registerhooks.txt | **必需**（防 ACE 冻结） |
| nikkiproxy_install_hooks_now.txt | 不需要（2 秒自动） |
| nikkiproxy_disable_hookall.txt | 不需要（默认关 IAT 二分管） |
| nikkiproxy_capture_start/end.txt | 触发捕获用（用完自动删除） |

> 注意：marker/开关路径就是真实路径（源码硬编码 `D:\git\rendertst-nikki\nikki\`），无需 junction。

## 第 5 步：启动游戏并验证注入（约 5 分钟）

```powershell
# 1. 启动 launcher
Start-Process "C:\Users\Administrator\game\InfinityNikki Launcher\1.3.1\xstarter.exe"
# 等 40-60 秒（窗口加载）

# 2. 自动点击"开始游戏"（截图模板匹配；窗口 2880x1620，按钮约 (2859,1658)）
cd D:\git\rendertst-nikki\nikki
uv run --with pyautogui --with pygetwindow python find_click_start.py
# 输出 "FOUND at scale=1.0 ... clicked" = 成功；"NOT FOUND" = 窗口没加载完，等 30 秒重试

# 3. 等 2.5-3 分钟（游戏启动 + hooks 装上），验证 marker：
Get-Content nikki\marker_d3d12_create.txt   # "WRAPPED via Create_Internal hr=0x00000000" = 设备已 wrap
Get-Content nikki\marker_swapchain.txt      # "wrapDevice=0000000..." 非 0 = swapchain 已 wrap
Get-Content nikki\marker_capture.txt        # "swapchain AddFrameCapturer dev=非0" = 捕获器就绪

# 4. 游戏应正常（Responding=True、窗口有画面）
Get-Process | Where-Object { $_.Name -match "X6Game" } | Select-Object Id,Responding
```

## 第 6 步：捕获（进世界后）

```powershell
# 方式 A：游戏中直接按 F10（开始/结束各按一次，边沿触发）
# 方式 B：文件触发（游戏无响应/黑屏时用）
New-Item -ItemType File "D:\git\rendertst-nikki\nikki\nikkiproxy_capture_start.txt"   # 开始
Start-Sleep 5
New-Item -ItemType File "D:\git\rendertst-nikki\nikki\nikkiproxy_capture_end.txt"     # 结束
# 触发文件会被自动删除（消费一次）

# 结果：
Get-ChildItem "D:\git\rendertst-nikki\nikki\captures\"
# nikki_capture*.rdc（几百 MB 正常）——用 RenderDoc qrenderdoc 打开回放验证
```

## 第 7 步：常见问题速查

| 现象 | 处理 |
|---|---|
| 游戏无响应、窗口卡死、线程全 Suspended | ACE 冻结：确认 skip_registerhooks 开关存在；hooks 必须在 DllMain 之后装 |
| 游戏秒崩（ACCESS_VIOLATION 0x0） | 用最新的构建（旧版 dxgi inline 调 NULL orig 会崩） |
| marker 全空（无 dllmain） | 注入失败：确认三个 DLL 部署正确、launcher 确实加载（version_proxy.log 有记录） |
| marker_d3d12_create 空 | hooks 没装：等更久或手动创建 install_hooks_now 开关 |
| marker_swapchain 的 wrapDevice=0 | 旧构建（无 repatch）导致游戏用真实设备；更新构建 |
| 按 F10 没反应 | 游戏暂停/加载黑屏时 Tick 不跑；用文件触发 |
| captures 目录空 | 先看 marker_capture 是否有 "after Start ... isCapturing=1"；没有则 capturer 未注册（查 swapchain marker） |
| find_click_start NOT FOUND | 窗口尺寸/状态变了：手动点"开始游戏"或用窗口比例点击 |
| 游戏正常但 xstarter 没"开始"按钮 | 重启 xstarter；窗口可能停在公告页 |
| 构建卡住 | 前台跑 + /nodeReuse:false；先杀残留 MSBuild 进程 |

## 第 8 步：回滚（还原游戏）

```powershell
Get-Process | Where-Object { $_.Name -match "X6Game|Nikki|xstarter" } | Stop-Process -Force
$g = "C:\Users\Administrator\game\InfinityNikki Launcher\InfinityNikki"
Remove-Item "$g\VERSION.dll", "$g\version_real.dll", "$g\rendertest.dll" -Force
# 或从备份还原（VERSION.dll.bak 等）
```

---

## 附加说明

- **qrenderdoc（魔改 RenderDoc UI）不能直接触发捕获**：游戏必须经 xstarter 带 token 启动，UI 的启动注入路径用不上。但它可以**打开 .rdc 回放**验证结果。
- 更多原理与踩坑：见 `nikki\REPRODUCE.md` 和 `nikki\进度文档.md`（阶段8）。
- 代码位置速查：`nikki\REPRODUCE.md` 第 8 节。
