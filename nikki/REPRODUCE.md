# 无限暖暖 RenderDoc 截帧复现文档

> 目标：让《无限暖暖》PC 版（ACE 反作弊）在游戏正常运行时，能被魔改 RenderDoc 截帧。
> 状态：**已实现并验证**（git tag `working`，提交 a3f361d96）。游戏正常 + 设备/swapchain wrap + F10/文件触发捕获生成 .rdc。

---

## 1. 总体架构

游戏必须经 `xstarter` 启动器（带 token）启动，因此不能使用 RenderDoc 标准的"启动并注入"。
方案：**VERSION.dll 代理注入链** + **延迟 hooks 安装** + **inline（restore-call-repatch）hook 技术**。

```
xstarter（launcher UI，用户双击）
  └─ 点"开始游戏" → 游戏根目录 InfinityNikki.exe（bootstrap）
       ├─ 加载 VERSION.dll（我们的代理）→ 转发真实 version API 给 version_real.dll
       │    ├─ LoadLibrary 同目录 rendertest.dll（魔改 renderdoc.dll）
       │    ├─ 设置 capture 配置（hookIntoChildren=true）
       │    └─ 注册 inline CreateProcessW/A hooks（restore-call-repatch，不依赖 IAT patch）
       └─ CreateProcess("X6Game-Win64-Shipping.exe") 命中 inline hook
            └─ CREATE_SUSPENDED + InjectIntoProcess（进程挂起时注入，ACE 未激活 → 成功）
                 ├─ X6Game 的 rendertest DllMain：跳过 RegisterHooks（防止 ACE 冻结进程）
                 ├─ 立即尝试安装 DXGI 工厂 inline hooks
                 └─ 轮询线程（每 250ms）：
                      ├─ 持续重试 DXGI 工厂 inline hooks（游戏延迟加载 dxgi.dll）
                      ├─ 2 秒后从普通线程调用 LibraryHooks::RegisterHooks()（不冻结）
                      └─ 检查 capture_start/capture_end 触发文件
```

---

## 2. 为什么这些设计决策（踩坑记录）

| 决策 | 原因 |
|---|---|
| 不注入 d3d12.dll 代理（文件替换） | ACE 完整性检查杀（实验 C 确认，死路） |
| 不在 DllMain 里注册 hooks | ACE 冻结整个进程（248 线程全部 SuspendThread） |
| 延迟 2 秒后普通线程注册 | 游戏运行中注册 hooks 不冻结（经验值，设备创建在 ~7 秒，来得及） |
| CreateDevice hook 必须 repatch | 游戏创建多个设备；no-repatch 时后续设备走真实 → swapchain 传真实设备 → 无法 wrap |
| GetInterface 非 DRED riid 返回 E_NOINTERFACE（不调真实） | 调真实 D3D12GetInterface 会卡死（11934 次实验）；DRED Settings2 返回 dummy 否则游戏无限重试加载 d3d12 |
| CreateProcess/工厂 hooks 用 inline 而非 IAT | 游戏 GetProcAddress 直调；且 IAT patch（HookAllModules）与此方案重复 |
| VEH 忽略 0x4001000A/0x406D1388 | 良性调试异常每 5 秒触发 8.6GB 全内存 dump → 磁盘风暴 → 游戏"假卡死" |
| 捕获键 F10 | F12 被游戏功能占用（用户确认）；PrintScreen 也可能冲突 |
| 构建禁 LTCG | 全量 LTCG 代码生成 LNK1257 失败（IPDB 损坏后无法恢复） |

---

## 3. 环境与目录

- 仓库：`D:\git\renderdoc-nikki`（RenderDoc v1.45 魔改，git tag `working`）
- 游戏：`C:\Users\Administrator\game\InfinityNikki Launcher\InfinityNikki\`
  - `VERSION.dll`（代理）、`version_real.dll`（系统 version.dll 副本）、`rendertest.dll`（魔改 renderdoc.dll）
  - 游戏本体 `X6Game\Binaries\Win64\X6Game-Win64-Shipping.exe`（静态导入 d3d12.dll，不导入 dxgi）
- 构建工具：VS 2022/2026 MSBuild（`C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe`）
- 工作目录：`D:\git\renderdoc-nikki\nikki\`（marker、开关、脚本、捕获文件）
- 路径消毒 junction：`D:\git\rendertst-nikki` → `D:\git\renderdoc-nikki`（DLL 内 "renderdoc" 字符串被消毒成 "rendertst"，marker 路径经 junction 写回真实目录）

---

## 4. 构建

```powershell
# 必须前台运行（Start-Process 后台方式会挂起不动）；/nodeReuse:false 避免 MSBuild server 卡死
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" `
  "D:\git\renderdoc-nikki\renderdoc\renderdoc.vcxproj" `
  /p:SolutionDir=D:\git\renderdoc-nikki\ /p:Configuration=Release /p:Platform=x64 `
  /m:8 /nodeReuse:false /v:minimal /nologo
```

- 产物：`D:\git\renderdoc-nikki\x64\Release\rendertest.dll`（约 25.8MB）
- 注意：构建中途被杀会损坏 IPDB 导致下次链接 LNK1257；清理 `x64\Release\*.ipdb/*.iobj` 可恢复（但 LTCG 已关，不再产生）

## 5. 消毒（必须）

DLL 内所有 "renderdoc/RenderDoc/RENDERDOC" 变体（ASCII + UTF-16）等长替换为 "rendertst/RenderTst/RENDERTST"，否则 ACE 按字符串特征杀进程。
参考 `nikki\sanitize_dll.ps1`（或内联 PowerShell 字节替换，替换计数约 600 处）。

```powershell
# 替换后输出到 nikki\test\rendertest.dll，再复制到游戏目录
```

## 6. 部署

```powershell
# 先关闭所有游戏进程（X6Game/InfinityNikki/xstarter），DLL 被加载时会锁文件
Get-Process | Where-Object { $_.Name -match "X6Game|Nikki|xstarter" } | Stop-Process -Force
Copy-Item "D:\git\renderdoc-nikki\nikki\test\rendertest.dll" `
  "C:\Users\Administrator\game\InfinityNikki Launcher\InfinityNikki\rendertest.dll" -Force
# 创建必需开关
New-Item -ItemType File "D:\git\renderdoc-nikki\nikki\nikkiproxy_skip_registerhooks.txt" -Force
```

开关文件列表见进度文档阶段8（nikki\进度文档.md）。

---

## 7. 启动与捕获

```powershell
# 1. 启动 launcher（xstarter）
Start-Process "C:\Users\Administrator\game\InfinityNikki Launcher\1.3.1\xstarter.exe"
# 等待窗口加载（40-60 秒）

# 2. 点击"开始游戏"按钮（截图模板匹配，窗口 2880x1620 @ (482,216)，按钮约 (2859,1658) 450x126）
cd D:\git\renderdoc-nikki\nikki
uv run --with pyautogui --with pygetwindow python find_click_start.py
# 输出 "FOUND ... clicked" 即成功；"NOT FOUND" 说明窗口未加载完，重试

# 3. 验证注入与 hooks（等 2.5 分钟，检查 marker）
Get-Content nikki\marker_d3d12_create.txt    # 应有 "WRAPPED via Create_Internal hr=0x00000000"
Get-Content nikki\marker_swapchain.txt       # 应有 "wrapDevice=非0"
Get-Content nikki\marker_capture.txt         # 应有 "swapchain AddFrameCapturer dev=非0"

# 4. 进世界后捕获（两种方式任选）
# 方式 A：游戏中按 F10（捕获键，开始/结束各按一次）
# 方式 B：文件触发
New-Item -ItemType File nikki\nikkiproxy_capture_start.txt   # 开始
Start-Sleep 5
New-Item -ItemType File nikki\nikkiproxy_capture_end.txt     # 结束

# 5. 结果
Get-ChildItem nikki\captures\   # nikki_capture*.rdc，用 qrenderdoc 回放
```

## 8. 关键代码修改点

### renderdoc/driver/d3d12/d3d12_hooks.cpp
- `InstallInlineHooks()`：只 patch System32 的 d3d12.dll/D3D12Core.dll；CreateDevice + GetInterface 用 restore-call-repatch（保存原始 12 字节，hook 内 restore→调真实→repatch）
- `D3D12CreateDevice_hook`（inline 版）：restore → 调 `d3d12hooks.Create_Internal(...)`（wrap 设备，保存 g_wrappedDevice）→ **repatch**（关键！否则后续设备不 wrap）
- `D3D12GetInterface_hook`：rclsid==DRED(4a75bbc4) 且 riid==Settings2(61552388) 返回 DummyDREDSettings2（S_OK）；其他 riid 直接 E_NOINTERFACE（**不调真实**，防卡死）
- 文件尾部：`GetWrappedD3D12Device()` 导出（捕获触发用）

### renderdoc/os/win32/win32_libentry.cpp
- `CrashDumpHandler`：过滤良性异常（0x4001000A/0x406D1388/0x40000010/0x40010006）；dump 类型 MiniDumpNormal
- `add_hooks()` 的 skip 分支（仅 X6Game 进程 + skip_registerhooks 开关）：
  - 立即调 `InstallDXGIFactoryInlineHooks()`
  - 轮询线程：每 250ms 重试 dxgi 工厂 inline hooks + 2 秒后 `LibraryHooks::RegisterHooks()` + capture_start/end 文件触发（消费后 DeleteFileA）

### renderdoc/os/win32/sys_win32_hooks.cpp
- `InstallCreateProcessInlineHooks()`：kernel32 CreateProcessW/A 入口 restore-call-repatch；hook 内调 `Hooked_CreateProcess`（CREATE_SUSPENDED + 注入 + resume）；**X6Game 进程内跳过安装**（防 ACE 检测 kernel32 修改）；仅当 disable_hookall 开关存在时安装（避免与 IAT 版重复）

### renderdoc/driver/dxgi/dxgi_hooks.cpp
- `InstallDXGIFactoryInlineHooks()`（extern "C" 导出）：dxgi 加载后 patch CreateDXGIFactory/1/2 入口；hook 直调真实地址（**不用 HookedFunction——dxgi 延迟加载时 orig 为 NULL 会崩溃**）+ `RefCountDXGIObject::HandleWrap`

### renderdoc/driver/dxgi/dxgi_wrapped.cpp
- `WrappedIDXGIFactory::CreateSwapChain`：marker + GetD3DDevice 检查 + wrap swapchain（WrappedIDXGISwapChain4 构造时 AddFrameCapturer + 写 marker）

### renderdoc/core/core.cpp
- `m_CaptureKeys`：默认仅 F10（原 F12+PrtScrn）

### renderdoc/os/win32/win32_hook.cpp
- `InitHookData()`：disable_hookall 开关 → `hookAll=false`（禁 IAT patch/HookAllModules）

### renderdoc/renderdoc.vcxproj
- Release 配置 `WholeProgramOptimization=false`（LTCG 死路）

### nikki/version_proxy.c（VERSION.dll 代理源码）
- 加载 version_real.dll + 绝对路径加载同目录 rendertest.dll + 设 RENDERDOC_CAPFILE + 调 INTERNAL_SetCaptureOptions（hookIntoChildren=true）+ INTERNAL_SetCaptureFile

---

## 9. 诊断手段

- marker 文件（nikki\marker_*.txt）：dllmain/launch/loadlib/inlinehook/d3d12_create/d3d12_getinterface/d3d12_register/dxgi_factory/swapchain/capture
- heartbeat.txt：关键路径心跳（CreateProcess post-inject 等）
- crash_log.txt：VEH 捕获的异常（模块+偏移+32 帧栈）
- 游戏窗口状态：`Get-Process -Id <pid> | select Responding,CPU`（Responding=True 且 CPU 高=正常渲染）

## 10. 常见问题

| 现象 | 原因/处理 |
|---|---|
| 游戏进程无响应（Responding=False，线程全 Suspended） | hooks 在 DllMain 早期注册 → ACE 冻结。确认 skip_registerhooks 开关存在 |
| 游戏崩溃（EXCEPTION_ACCESS_VIOLATION 0x0） | dxgi inline hook 调 NULL HookedFunction。确认用直调真实版本 |
| 切分辨率不触发 CreateDevice | 正常（只重建 swapchain 不重建设备） |
| F10 无效 | 游戏暂停/黑屏（Tick 不跑）；用文件触发 |
| 捕获文件巨大（1GB+） | 正常（D3D12 全记录）；可用 CaptureOptions 限制 |
| 无限捕获循环 | 旧版 bug（触发文件未消费）；新版自动删除 |
| 构建 LNK1257 | LTCG 已关；若再遇清理 x64\Release 中间文件 |
| MSBuild 后台启动不干活 | 用前台 & 调用 + /nodeReuse:false |
| launcher 没起 | 点"开始游戏"才拉起 launcher；或重启 xstarter |
