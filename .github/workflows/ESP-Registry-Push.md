# ESP Registry 组件发布说明

本文是 `candis-bsp` Monorepo 中独立组件发布与维护的操作手册。所有命令均以仓库根目录为当前目录。正式发布只能由维护者从 `main` 分支手动触发，不创建长期 Registry Token，也不允许 Tag Push 自动上传。

## 1. Monorepo 多组件发布架构

仓库保留单一 Git 仓库，组件源码分别位于：

| 组件 | 源码目录 | 发布标识 | 内部依赖 |
| --- | --- | --- | --- |
| `jlclab/candis` | `components/candis` | `candis/v<版本>` | `jlclab/tg28-esp` |
| `jlclab/candis-camera` | `components/candis-camera` | `candis-camera/v<版本>` | 无 Candis BSP 依赖 |
| `jlclab/candis-dock` | `components/candis-dock` | `candis-dock/v<版本>` | `jlclab/candis`（间接依赖 `jlclab/tg28-esp`） |
| `jlclab/tg28-esp` | `components/tg28-esp` | `tg28-esp/v<版本>` | 无 Candis BSP 依赖 |

三个主要工作流分工如下：

- `ci.yml`：按受影响源码范围选择组件和示例做增量构建。
- `full-build.yml`：手动或每周执行完整示例矩阵。
- `registry-release.yml`：手动选择单个组件，在 ESP-IDF 6.1/6.2 下验证；仅在 `publish=true` 时进入正式发布 Job。

发布包由 `tools/registry_release.py` 统一验证和打包。该脚本按组件依赖闭包只构建目标组件及必要依赖，并生成独立消费者工程。`tools/stage_registry_extension.py` 负责把 Monorepo 内部 `path` 依赖改写为 Registry Namespace 依赖，同时保留原有源码路径和仓库元数据。

## 2. Namespace、组件列表及内部依赖关系

固定配置：

| 配置项 | 值 |
| --- | --- |
| Registry Namespace | `jlclab` |
| GitHub Repository | `jlclab-io/candis-bsp` |
| 发布工作流 | `registry-release.yml` |
| 发布分支 | `main` |
| GitHub Environment | `registry` |
| 认证方式 | GitHub OIDC Trusted Uploaders |

内部依赖顺序为：

```text
tg28-esp
  └── candis
        └── candis-dock

candis-camera（独立）
```

禁止在正式发布包中保留 `path` 或 `override_path`。Monorepo 本地联调可以在消费者工程中使用 `override_path`，但 Registry 发布包必须使用 Namespace 限定依赖和精确版本。

## 3. ESP Registry Trusted Uploader 配置

在 ESP Component Registry 的 Permissions 页面进入 `jlclab` Namespace，并为每个组件分别配置 Trusted Uploader。四个组件都要配置相同的 Repository、Workflow、Branch 和 Environment 限制：

| 字段 | 值 |
| --- | --- |
| Repository | `jlclab-io/candis-bsp` |
| Workflow | `registry-release.yml` |
| Branch | `main` |
| Environment | `registry` |

Workflow 字段必须与发布 Job 所在工作流文件名完全一致。分支和 Environment 是双重限制，任一项不匹配都会使 OIDC 上传失败。不要用个人 API Token 替代 Trusted Uploader。

## 4. GitHub Environment `registry` 配置

确认仓库 Settings → Environments 中存在 `registry`，并设置部署分支策略为仅允许 `main`。`publish` Job 使用 `environment: registry`，因此 Environment 审批、分支保护或部署密钥策略都会在正式上传前生效。

该 Environment 不需要保存 Registry Secret 或 Variable。Namespace 固定写在发布工作流中，上传认证由 GitHub OIDC 自动生成短期令牌。

## 5. OIDC 认证及权限要求

发布工作流分为普通验证 Job 和正式发布 Job：

- `host-tests`、`validate`：只有 `contents: read`。
- `publish`：只有该 Job 声明 `id-token: write` 和 `contents: read`。
- `publish` 同时要求 `github.ref == 'refs/heads/main'`、`inputs.publish == true` 和 Environment 分支策略通过。

正式上传步骤固定使用经过版本核对的官方 Action Commit SHA：

```text
espressif/upload-components-ci-action@2ce17d73e35473317c1419dbd4f007aacb593040
```

该 SHA 对应官方 `v2.2.1`。调用时不传入 `api_token`；Action 从 GitHub OIDC 获取短期 Registry 令牌。不要在工作流、脚本、文档或仓库中新增 `IDF_COMPONENT_API_TOKEN`。

## 6. 独立组件 SemVer、Manifest、CHANGELOG 管理

每个组件独立维护：

```text
components/<component>/idf_component.yml
components/<component>/README.md
components/<component>/CHANGELOG.md
components/<component>/LICENSE
```

Manifest 必须满足：

- `version` 是无前缀的 SemVer，例如 `0.1.0`；发布标识使用组件名加 `v`，例如 `candis/v0.1.0`。
- `repository` 为 `https://github.com/jlclab-io/candis-bsp.git`。
- `repository_info.path` 为该组件的仓库相对路径。
- `url` 指向 `main` 分支中的组件目录。
- Candis 组件声明 `targets: [esp32s31]`，内部依赖使用当前仓库的本地 `path`，发布前由 staging 脚本改写。
- `idf` 约束保持为 `>=6.1,<6.3`。

版本只在准备正式发布时递增。任何已上传版本都不可覆盖；重复版本必须在发布前失败，而不是静默跳过。`CHANGELOG.md` 必须包含与 Manifest 完全相同的版本标题，例如：

```markdown
## 0.1.0 (Unreleased)
```

本仓库采用手动组件发布，不要求创建 Git Tag。发布身份由组件名、Manifest 版本和 `main` 上的 Commit SHA 共同确定。Git Tag 不能单独标识 Monorepo 子目录，因此不再把仓库级 Tag 作为发布前置条件。

## 7. 本地 CI、打包、编译及发布前验证命令

### 7.1 静态检查

```bash
python3 -m unittest discover -s tests -v
actionlint .github/workflows/*.yml
shellcheck tools/*.sh
git diff --check
```

可信的本地镜像环境还需要做容器内工作流验证：

```bash
act workflow_dispatch \
  -W .github/workflows/registry-release.yml \
  -e ./registry-event.json \
  -P ubuntu-24.04=catthehacker/ubuntu:act-24.04 \
  --matrix idf_version:6.2 \
  --pull=false --rm
```

`registry-event.json` 只用于本地测试，内容应包含 `component` 和布尔值 `publish: false`，不得放入仓库。act 对 GitHub OIDC、Environment 审批和真实 Registry 上传无法验证的部分，必须标记为“待正式发布验证”。全量示例构建使用：

```bash
python3 tools/ci_matrix.py build --group core
python3 tools/ci_matrix.py build --group camera
python3 tools/ci_matrix.py build --group camera-models-1
python3 tools/ci_matrix.py build --group camera-models-2
python3 tools/ci_matrix.py build --group expansion
python3 tools/ci_matrix.py build --group system
```

同一组命令要在 ESP-IDF 6.1 和 6.2 环境各执行一次。容器或本地环境无法支持 GitHub 专属缓存、Artifact 和 OIDC 时，要记录差异，并用等效的底层命令补齐其余验证。

### 7.2 四个组件的独立验证

先准备当前 ESP-IDF 环境，然后对目标组件执行：

```bash
python3 tools/registry_release.py validate \
  --component candis

python3 tools/registry_release.py prepare \
  --component candis \
  --output-dir .registry-release \
  --commit-sha "$(git rev-parse HEAD)"

python3 tools/registry_release.py dry-run \
  --component candis \
  --source-dir .registry-release/staging/candis \
  --commit-sha "$(git rev-parse HEAD)" \
  --output-dir .registry-release/dry-run

python3 tools/registry_release.py build-consumer \
  --component candis \
  --plan .registry-release/plan.json \
  --output-dir .registry-release/package-consumer \
  --mode package \
  --commit-sha "$(git rev-parse HEAD)"
```

`tg28-esp` 和 `candis-camera` 没有 staging 目录，`dry-run` 的 `--source-dir` 改用 `components/<component>`。`candis-dock` 使用 `.registry-release/staging/candis-dock`。

重复以上流程时，四个组件都要分别在 ESP-IDF 6.1 和 6.2 环境中通过。`prepare` 会检查归档内容、Manifest 依赖、Repository URL、源码路径和 Commit SHA；`build-consumer` 会用独立工程验证依赖解析和编译。`candis-camera` 的消费者依赖锁不得出现 `candis`。

若 `compote` 网络失败，先确认 Registry 可达，再重新执行同一 dry-run；不要在失败后跳过该组件。`dry-run` 不会创建 Registry 版本。

## 8. GitHub Actions 手动验证和正式发布操作

验证流程（默认 `publish=false`）：

```bash
gh workflow run registry-release.yml \
  --ref main \
  -f component=candis \
  -f publish=false

gh run list --workflow registry-release.yml --limit 5
gh run watch <RUN_ID>
```

对 `candis`、`candis-camera`、`candis-dock`、`tg28-esp` 分别执行一次。历史上 `publish=false` 的运行不应出现 Registry 上传步骤。

正式发布前必须：

1. 确认目标组件版本和 `CHANGELOG.md` 已提交并推送。
2. 确认运行分支为 `main`，并记录其 Commit SHA。
3. 从 `main` 手动运行目标组件，将 `publish` 设为 `true`。
4. 等待 `registry` Environment 审批和全部验证 Job 通过。

示例：

```bash
gh workflow run registry-release.yml \
  --ref main \
  -f component=tg28-esp \
  -f publish=true
```

`publish` Job 会再次验证 Manifest 和当前 Commit；然后用 `compote --check-only` 拒绝已存在版本；再通过固定 SHA 的官方 Action 上传。普通 Push、PR、Tag Push 和非 `main` 分支均不会进入正式上传 Job。

## 9. 发布标识和 GitHub Release 管理

每次正式发布记录以下标识，但本次流程不要求创建 Git Tag：

```text
candis/v0.1.0
candis-camera/v0.1.0
candis-dock/v0.1.0
tg28-esp/v0.1.0
```

发布前确认 `git rev-parse HEAD` 与远端 `main` 一致。若需要额外创建 GitHub Release，应只在 Registry 上传和下载回归都成功后创建，并记录组件名、版本与 Commit SHA。Git Tag 若被使用，仍不会触发 Registry 上传，也不得移动或强制覆盖已发布版本对应的 Tag。

## 10. 首次发布顺序

首次发布按内部依赖顺序执行：

1. `tg28-esp`
2. `candis`
3. `candis-camera` 和 `candis-dock`（两者可独立安排）

`candis` 发布前必须已有目标版本的 `jlclab/tg28-esp`。`candis-dock` 发布前必须已有目标版本的 `jlclab/candis`。`candis-camera` 不依赖 Candis BSP，但仍要先完成自身验证。每个组件单独手动触发 `publish=true`，不要求创建共享 Tag。

## 11. Registry 安装及发布结果验证

发布 Job 在上传完成后会把根依赖改为精确 Registry 版本，重新解析依赖并编译：

```bash
python3 tools/registry_release.py build-consumer \
  --component candis-dock \
  --plan .registry-release/plan.json \
  --output-dir .registry-release/registry-consumer \
  --mode registry \
  --commit-sha "$(git rev-parse HEAD)"
```

成功后检查消费者工程的 `dependencies.lock` 和下载目录中的 Manifest。`managed_components/jlclab__<component>/idf_component.yml` 的 `version` 与 `repository_info.commit_sha` 必须和发布运行使用的 Manifest、Commit 一致。还应确认依赖闭包从 Registry 下载，而不是使用本地 `override_path`。

发布结果记录建议：

```text
Component:
Version:
Commit SHA:
发布标识:
Workflow run:
Registry install/build result:
```

## 12. 重复版本、OIDC 认证失败、依赖解析失败等故障处理

### 重复版本

`compote --check-only` 返回版本已存在时，发布 Job 会失败。不要删除或覆盖已发布版本；应递增 Manifest 和 CHANGELOG 版本，重新验证后发布新版本。

### OIDC 认证失败

核对 Trusted Uploader 的 Repository、Workflow、Branch、Environment，确认只有发布 Job 具有 `id-token: write`，并确认运行来自 `main`。还要确认 GitHub Environment `registry` 的部署分支策略允许 `main`。不要把长期 API Token 作为规避手段。

### 依赖解析失败

检查内部依赖是否已经按顺序发布、Manifest 是否使用 Namespace 限定名和精确版本置换结果，以及发布标识、Manifest 版本与 Commit SHA 是否一致。扩展组件必须经过 `tools/stage_registry_extension.py`，禁止直接上传含 `../candis` 或 `../tg28-esp` 的本地 path 包。

### 打包失败

确认归档中没有 `build`、`dist`、`__pycache__`、`managed_components`、`.bin`、`.elf`、`.log`，且包含 `idf_component.yml`、`CMakeLists.txt`、`README.md`、`LICENSE`。检查 `repository_info.path` 和 Commit SHA 是否正确。

### 发布后下载失败

先确认 Registry 上传任务已完成，再检查消费者是否使用 `jlclab/<component>==<version>`。若刚发布的服务端索引存在短暂延迟，保留同一版本和 Commit SHA 重跑消费者构建；不得用 `publish=true` 重传同一版本。

## 13. 后续新增组件的接入流程

新增组件时按以下顺序完成：

1. 在 `components/<component>` 建立独立的 Manifest、README、CHANGELOG、LICENSE 和 `targets: [esp32s31]`。
2. 在 `tools/registry_release.py` 的组件依赖表和顺序中加入新组件。
3. 如需 Monorepo 内部依赖，扩展 `tools/stage_registry_extension.py`，验证发布包不再包含文件系统 `path`。
4. 在 `tests/test_registry_release.py` 和 `tests/test_ci_matrix.py` 增加组件边界、路径选择和发布元数据测试。
5. 在 `registry-release.yml` 的 `component` 下拉选项中增加组件名。
6. 在 Registry 中创建组件并配置相同的 Trusted Uploader 的 Repository、Workflow、Branch、Environment。
7. 更新 `full-build.yml` 或 `ci_matrix.py` 的示例覆盖，确保新增组件有独立消费者构建。
8. 先运行四个现有组件的 publish=false 验证，再运行新组件的两个 ESP-IDF 版本验证。
9. 首次正式发布遵循依赖顺序，逐个手动触发 `publish=true`；不要求创建 Git Tag。
