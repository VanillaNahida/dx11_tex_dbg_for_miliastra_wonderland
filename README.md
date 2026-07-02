![:name](https://count.getloli.com/@dx11_tex_dbg_for_mw?name=dx11_tex_dbg_for_mw&theme=minecraft&padding=6&offset=0&align=top&scale=1&pixelated=1&darkmode=auto)

>[!TIP]
> 目前该项目已在6.7版本失效，拍照会直接提示头像有误，请静等作者更新程序修复。

# 警告：
* **严禁**将本项目的代码及其编译后的exe二进制文件发布在国内平台！（如抖音，B站，米游社，快手等）
* **因不当使用**导致的封号、法律纠纷与本项目无关，由使用者自行承担全部风险与责任。  

# 免责声明
本项目仅供图形学学习、底层API拦截技术研究以及自研D3D11程序的调试使用。  
**严禁**用于任何带有反作弊系统或最终用户许可协议（EULA）禁止修改的商业游戏。  
因不当使用导致的封号、法律纠纷与本项目无关，由使用者自行承担全部风险与责任。  

## 荣誉证书
<div align="center">
  <img width="480" alt="image" src="https://github.com/user-attachments/assets/b87d1007-2cf3-48fe-a48a-142f8f682278" />
  <br>
  <img width="480" alt="image" src="https://github.com/user-attachments/assets/06def154-8ec8-4aec-9004-55ffef85caed" />
</div>

## 补档的视频
完整视频：https://www.xcnahida.cn/archives/KrDu8yDD

https://github.com/user-attachments/assets/8cb76924-928b-41bc-9205-803301fe200b

<details>
<summary>项目原理分析：DirectX 11 Texture Debugger</summary>
<br>
<br>

## 项目原理分析：DirectX 11 Texture Debugger
这个项目是一个 **DirectX 11 纹理调试器**（`dx11_tex_dbg_for_mw`），其核心功能是 **通过 Hook（钩子）技术，将用户自定义的图片注入到目标进程的 D3D11 纹理读取流程中**，从而替换游戏中某些纹理（比如头像贴图）。

整个项目由两个二进制组件构成：

### 1. 加载器（Loader）— [loader.cpp](https://github.com/VanillaNahida/dx11_tex_dbg_for_mw/blob/main/src/loader.cpp)

这是一个可执行文件（`dx11_tex_dbg.exe`），负责 **准备数据** 和 **触发注入**，流程如下：

#### ① 加载并处理图片
- 用户通过文件选择对话框选择一张图片
- 使用 GDI+ 将图片缩放到多个尺寸（默认 512×512、256×256、128×128）
- 图片被裁剪为圆形（`AddEllipse` 裁剪路径），并进行 Y 轴翻转（`ScaleTransform(1, -1)`）
- 像素格式从 GDI+ 的 BGRA 转换为 D3D11 使用的 RGBA
- 参见 [load_texture](https://github.com/VanillaNahida/dx11_tex_dbg_for_mw/blob/main/src/loader.cpp#L24-L74)

#### ② 通过全局 Windows Hook 注入 DLL
- 加载 DLL 模块（`dx11_texture_debugger.dll`）
- 调用 DLL 导出的 `set_target()` 设定目标进程名
- 使用 `SetWindowsHookExW(WH_CBT, ...)` 安装一个 **全局 CBT 钩子**
- 参见 [main 函数中的 Hook 设置](https://github.com/VanillaNahida/dx11_tex_dbg_for_mw/blob/main/src/loader.cpp#L192-L202)

> **关键原理**：`SetWindowsHookExW` 的全局钩子会导致 Windows 自动将指定的 DLL 加载到 **所有进程** 中。当目标进程加载了这个 DLL 后，DLL 的 `DllMain` 就会被调用。

#### ③ 通过命名管道传输纹理数据
- 创建一个名为 `\\.\pipe\D3D11_TexDbg_SharedBuffer` 的命名管道
- 阻塞等待目标进程（DLL 端）连接
- 连接后依次写入：纹理数量 → 每个纹理的尺寸 + RGBA 像素数据
- 参见 [start_texdbg_pipe](https://github.com/VanillaNahida/dx11_tex_dbg_for_mw/blob/main/src/loader.cpp#L104-L124)

---

### 2. 注入 DLL — [dx11_tex_dbg.cpp](https://github.com/VanillaNahida/dx11_tex_dbg_for_mw/blob/main/src/dx11_tex_dbg.cpp)

这是一个动态链接库（`dx11_texture_debugger.dll`），被注入到目标进程后，负责 **拦截 D3D11 API 调用并替换纹理数据**。

#### ① 共享内存段跨进程通信

```cpp
#pragma data_seg(".shared")
wchar_t target_process_name[64] = {0};
int state = 0;
#pragma data_seg()
#pragma comment(linker, "/SECTION:.shared,RWS")
```

使用 PE 文件的 **共享数据段**（`.shared`），使得所有加载了此 DLL 的进程共享同一块内存。这样 Loader 进程设置的 `target_process_name` 和 `state` 变量，在目标进程中也能直接读取到。参见 [共享段定义](https://github.com/VanillaNahida/dx11_tex_dbg_for_mw/blob/main/src/dx11_tex_dbg.cpp#L10-L21)。

#### ② DllMain 的状态机控制

```
state=0 (Loader 首次加载 DLL)  →  state=1（标记已初始化）
state=1 (目标进程加载 DLL)     →  验证进程名匹配后 → state=2，开始 Hook
```

参见 [DllMain](https://github.com/VanillaNahida/dx11_tex_dbg_for_mw/blob/main/src/dx11_tex_dbg.cpp#L160-L183)。这个状态机确保只有 **Loader 先初始化** 之后，**目标进程才会执行 Hook 逻辑**。

#### ③ Hook `ID3D11DeviceContext::Map` 方法

这是整个项目最核心的部分：

1. 临时创建一个 D3D11 设备和交换链，获取 `ID3D11DeviceContext` 的 **虚函数表（vtable）**
2. 通过 [MinHook](https://github.com/TsudaKageworker/minhook) 库 Hook **vtable[14]**，即 `ID3D11DeviceContext::Map` 方法
3. 通过命名管道从 Loader 接收纹理数据

参见 [hook 函数](https://github.com/VanillaNahida/dx11_tex_dbg_for_mw/blob/main/src/dx11_tex_dbg.cpp#L110-L147)。

#### ④ 纹理替换逻辑

在 [hooked_Map](https://github.com/VanillaNahida/dx11_tex_dbg_for_mw/blob/main/src/dx11_tex_dbg.cpp#L76-L108) 函数中：

1. 先调用原始的 `Map` 方法，让 D3D11 正常映射资源
2. 检查映射类型是否为 `READ` 或 `READ_WRITE`
3. 通过 `QueryInterface` 检查资源是否为 `ID3D11Texture2D`
4. 如果纹理满足以下条件就替换：
   - `Usage == D3D11_USAGE_STAGING`（暂存纹理，用于 CPU 读取）
   - **宽度 == 高度**（正方形纹理）
   - 格式为 `R8G8B8A8` 系列
   - 尺寸匹配预加载的调试纹理（512/256/128）
5. 逐行拷贝自定义像素数据到映射的内存区域（注意处理 `RowPitch` 对齐）

---

### 整体数据流

```
用户图片 → [Loader] GDI+缩放/圆形裁剪/BGRA→RGBA
                ↓
        命名管道传输纹理数据
                ↓
 [DLL in 目标进程] 接收并缓存纹理
                ↓
   Hook ID3D11DeviceContext::Map
                ↓
  当游戏读取 Staging 纹理时 → 替换为自定义纹理数据
```

### 辅助模块

- [cmdline.cppm](https://github.com/VanillaNahida/dx11_tex_dbg_for_mw/blob/main/src/cmdline.cppm) — C++20 模块，解析命令行参数（支持 `--target`、`--sizes`、`--module`、`--multi` 等选项）
- [console.cppm](https://github.com/VanillaNahida/dx11_tex_dbg_for_mw/blob/main/src/console.cppm) — C++20 模块，提供带 ANSI 彩色输出的控制台日志工具

### 技术特色

| 技术点 | 说明 |
|---|---|
| **全局 Windows Hook 注入** | 利用 `SetWindowsHookExW(WH_CBT)` 使系统将 DLL 注入所有进程 |
| **PE 共享数据段** | `.shared` 段实现跨进程共享变量，避免复杂 IPC |
| **命名管道** | 用于传输大量纹理像素数据 |
| **COM vtable Hook** | 通过 MinHook 拦截 D3D11 COM 接口的虚函数 |
| **C++23 特性** | 使用模块（`import`/`export module`）、`std::format`、`std::views` 等现代 C++ 特性 |

总结来说，这个项目的核心原理就是：**通过全局 Hook 将 DLL 注入目标进程 → 拦截 D3D11 的 Map 调用 → 在 CPU 读取 Staging 纹理时替换为自定义图片数据**，从而实现在使用 D3D11 渲染的程序中替换特定纹理（如头像等正方形贴图）。

</details>