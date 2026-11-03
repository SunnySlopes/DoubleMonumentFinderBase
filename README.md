# DoubleMonumentFinderBase

`DoubleMonumentForFixedSeed` 的核心计算库 + 独立命令行程序。

本仓库从 [DoubleMonumentForFixedSeed](https://github.com/SunnySlopes/DoubleMonumentForFixedSeed) 拆出：
所有 C/C++ 搜索逻辑、cubiomes 依赖和结果计算都在这里；GUI 主库只保留 Java 界面、
JNI 薄桥和构建脚本。

## 与主库的契约

| 项目 | 位置 | 说明 |
| --- | --- | --- |
| 公共 C API | `include/doublemonumentfinder/base.h` | 主库 JNI 薄桥唯一包含的头文件；API 版本 1.0.0 |
| 静态库 | `build/lib/libdoublemonumentfinder_base.a` | `jni/project_MonumentFinderBridge.cpp` 链接它生成 DLL |
| 独立 CLI | `build/bin/DoubleMonumentFinderBase-cli.exe` | 可单独分发，也随主库 JAR 一起打包 |
| 数据结构 | `DmfSearchParams` / `DmfPairHit` | 字段顺序属于契约的一部分 |
| 版本解析 | `dmf_parse_mc` / `dmf_mc_name` / `dmf_newest_mc` | 主库不再直接依赖 cubiomes |

主库侧的顺序是：

```text
gradlew DoubleMonumentForFixedSeedJar
  -> verifyBaseSubmodule      检查本子模块与嵌套 cubiomes 是否就绪（缺失则失败）
  -> cmakeConfigure/buildNative
       编译本库静态库 + CLI
       编译 thin JNI shim，链接静态库生成 libmonumentFinderLibJ.dll
  -> prepareNativeResources
       把 libmonumentFinderLibJ.dll 和 DoubleMonumentFinderBase-cli.exe 打进 JAR 的
       /native/windows/ 资源目录
```

主库固定把本仓库放在 `DoubleMonumentForFixedSeed/DoubleMonumentFinderBase` 作为 git 子模块。

### C API 摘要

```c
int  dmf_run_search(const DmfSearchParams *params);  /* 同步执行 */
int  dmf_pause(void);
int  dmf_resume(void);
int  dmf_stop(void);
void dmf_reset(void);
void dmf_get_progress(int64_t out[3]);              /* processed, total, status */
int32_t dmf_result_count(void);
int32_t dmf_get_results(DmfPairHit *out, int32_t capacity);
int  dmf_classify_ocean_type(...);                  /* 0=others, 1=cold, 2=frozen */
int  dmf_classify_ocean_types(...);                 /* DMF_OCEAN_MASK_* 位掩码 */
const char *dmf_last_error(void);
```

搜索区域使用 **region 坐标**（每个 region = 512 方块），区间为
`[min_x, max_x) x [min_z, max_z)`；结果坐标 `hang_x/hang_z/dx/dz` 是方块坐标。

## 子模块

| 子模块 | 地址 | 用途 |
| --- | --- | --- |
| `cubiomes/` | <https://github.com/xpple/cubiomes> | `getStructurePos()` / `isViableStructurePos()` / 海温采样 |

首次克隆后：

```powershell
git submodule update --init --recursive
```

## 构建

需要 CMake 3.20+、Ninja 和支持 C++20 的 MinGW GCC（已在 MSYS2 UCRT64 GCC 16 验证）。
MSVC 不受支持（cubiomes 的 C 源码依赖 MinGW/GCC 语义）。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\bin\DoubleMonumentFinderBase-cli.exe --help
```

产物：

```text
build\lib\libdoublemonumentfinder_base.a
build\bin\DoubleMonumentFinderBase-cli.exe
```

MinGW 下 CLI 会静态链接 `libgcc` / `libstdc++` / `libwinpthread`，只依赖 Windows 自带的
UCRT，可直接复制 exe 到其他 Windows 机器运行。

### 独立打包 exe

```powershell
cmake --install build --prefix dist
```

产物：`dist\bin\DoubleMonumentFinderBase-cli.exe`。

## CLI 用法

```text
DoubleMonumentFinderBase-cli --seed <int64> [options]
```

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `--seed <int64>` | 必填 | 世界种子，可为负数 |
| `--min-x` / `--max-x` | `-128` / `128` | region X 范围（含 min，不含 max） |
| `--min-z` / `--max-z` | `-128` / `128` | region Z 范围（含 min，不含 max） |
| `--square <n>` | 未设置 | 以 `(0,0)` 为中心、边长为 `n` 个 region 的正方形；不能与显式 min/max 混用 |
| `--threads <n>` | CPU 核数 | 工作线程数，`0` = 自动，最大 256 |
| `--mc <version\|id>` | `26.3` | Minecraft 版本，也接受 cubiomes `MCVersion` 数字 |
| `--limit <n>` | `0` | 最多输出行数；`0` = 全部 |
| `--out <file>` | 未设置 | 把同一份文本报告写入 txt 文件（同时仍打印到 stdout） |
| `--quiet` | 关闭 | 不显示实时进度行 |
| `--help` / `-h` | — | 帮助 |
| `--version` | — | CLI 与 API 版本 |

示例：

```powershell
# 默认 256x256 region（与 GUI 默认正方形区域一致），写入 txt
DoubleMonumentFinderBase-cli --seed 7 --out result.txt

# 指定 region 范围、线程数和版本
DoubleMonumentFinderBase-cli --seed -12345 --min-x -64 --max-x 64 `
    --min-z -64 --max-z 64 --threads 8 --mc 26.3 --limit 50 --out pair.txt
```

## 输出格式

纯文本，`#` 开头的是参数摘要，随后是 tab 分隔的表格：

```text
# DoubleMonumentFinderBase CLI 1.0.0
# seed=7 region=[-48,48)x[-48,48) threads=8 mc=26.3(35) found=2 output=2 elapsed=1.234s
# columns: hangX hangZ dx dz lowEfficiency oceanType tp
hangX	hangZ	dx	dz	low	ocean	tp
20928	-6928	160	32	0	others	/tp 20928 50 -6928
-22104	-3832	176	48	1	frozen	/tp -22104 50 -3832
```

- `low=1` 表示该二联可用但效率会略微损失。
- `ocean` 是两殿中点的主海温类型（`others` / `cold` / `frozen`）。
- 结果先按 `hangX, hangZ, dx, dz, low` 排序，再应用 `--limit`。

## 退出码

| 码 | 含义 |
| --- | --- |
| `0` | 成功 |
| `1` | 运行错误（参数以外的校验失败、文件写入失败） |
| `2` | 命令行用法错误 |
| `4` | 被 Ctrl+C 取消 |

## all-in-one-base 分支

本仓库另有 `all-in-one-base` 分支，供 [All-In-One Finder](https://github.com/SunnySlopes/All-In-One-Finder)
的 `tools/doublemonument` 子模块使用。该分支：

- 文件内容与旧仓库 `DoubleMonumentForFixedSeed` 的 `all-in-one-base` 分支一致；
- 额外带有 `cubiomes/` 子模块，便于独立构建 / 自测；
- git 历史基于本仓库 `main` 分支创建，而不是原仓库的分支。

该分支保留原来的 `Double*` / `dm_*` C++ 接口，不定义 `main`，由 AIO 的
`native/double_monument_host.cpp` 直接编译；`monument_search.h`、`monument_search.cpp`、
`monument_search_core.c/.h`、`Thread.h`、`test/selftest.cpp` 均位于仓库根目录。

## 许可证

本项目采用 MIT 许可证，详见 [LICENSE](LICENSE)。

依赖的 [cubiomes](https://github.com/xpple/cubiomes) 为第三方项目，同样采用 MIT 许可证
（Copyright © 2020 Cubitect）。
