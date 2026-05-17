# 灵犀智空 BL 感知模组 — 版本管理规范

> **版本**: v3.2  
> **日期**: 2026-05-15  
> **适用范围**: 全工程 (stm32/ esp32/ shared/ tests/ tools/)

---

## 目录

1. [Git 分支策略](#git-分支策略)
2. [Commit 规范](#commit-规范)
3. [Release 流程](#release-流程)
4. [版本号规则](#版本号规则)
5. [标签管理](#标签管理)
6. [变更日志](#变更日志)

---

## Git 分支策略

采用 **Git Flow 简化版**，适合嵌入式固件开发节奏。

```
main  ──────────────────────────────────────────►  生产分支
  │                                                    (永远可构建)
  ├── develop ────────────────────────────────────►  开发主干
  │     │                                              (日常集成)
  │     ├── feature/stm32-sdio ◄──► 特性分支
  │     ├── feature/esp32-ble
  │     ├── feature/protocol-v3.2
  │     │
  │     ├── hotfix/crc-bug ◄──────► 热修复分支
  │     │
  │     └── release/v3.2.0 ◄──────► 发布分支
  │
  └── hotfix/v3.1.1 (紧急修复从 main 切出)
```

### 分支说明

| 分支 | 命名规则 | 生命周期 | 合并目标 |
|-----|---------|---------|---------|
| `main` | `main` | 永久 | — |
| `develop` | `develop` | 永久 | `main` |
| `feature/*` | `feature/<模块>-<简述>` | 开发完成即删 | `develop` |
| `release/*` | `release/v<主>.<次>.<修订>` | 发布完成即删 | `main` + `develop` |
| `hotfix/*` | `hotfix/v<版本>` | 修复完成即删 | `main` + `develop` |

### 分支保护规则

```bash
# main 分支保护（通过 GitHub/GitLab 设置）
- 禁止直接 push
- 需 PR/MR + 1 人 Code Review
- CI 构建通过
- 静态分析通过

# develop 分支保护
- 禁止直接 push
- 需 PR/MR
- CI 构建通过
```

---

## Commit 规范

采用 **Conventional Commits** 规范，便于自动生成 CHANGELOG。

### 格式

```
<type>(<scope>): <subject>

<body>

<footer>
```

### Type 定义

| Type | 说明 | 对应 SemVer |
|-----|-----|-----------|
| `feat` | 新功能 | `MINOR` |
| `fix` | Bug 修复 | `PATCH` |
| `docs` | 文档变更 | — |
| `style` | 代码格式（不影响功能） | — |
| `refactor` | 重构 | — |
| `perf` | 性能优化 | — |
| `test` | 测试相关 | — |
| `chore` | 构建/工具链/依赖 | — |
| `build` | 构建系统变更 | — |
| `ci` | CI/CD 配置 | — |
| `revert` | 回滚 | — |

### Scope 定义

| Scope | 说明 |
|-------|------|
| `stm32` | STM32 主控代码 |
| `esp32` | ESP32-C6 通信代码 |
| `protocol` | 通信协议 |
| `shared` | 共享代码 |
| `test` | 测试代码 |
| `docs` | 文档 |
| `build` | 构建系统 |
| `ci` | CI/CD |

### 示例

```bash
# 新功能
feat(stm32): 添加 SDIO DMA 双缓冲传输

# Bug 修复
fix(protocol): 修复 CRC 校验在大端模式下的错误

# 重构
refactor(shared): 将 ring_buffer 改为无锁设计

# 文档
docs(dev): 更新 Linux 开发环境搭建指南

# 带 BREAKING CHANGE
feat(protocol)!: 修改帧格式，增加版本字段

BREAKING CHANGE: 帧头增加 1 字节版本字段，旧协议不兼容。

# 关联 Issue
fix(stm32): 修复 NPU 推理超时问题

Closes #42
```

### Commit Message 检查

```bash
# 安装 commitlint（可选）
npm install -g @commitlint/cli @commitlint/config-conventional

# 配置 .commitlintrc.json
echo '{ "extends": ["@commitlint/config-conventional"] }' > .commitlintrc.json

# Git hook（可选）
cat > .git/hooks/commit-msg << 'EOF'
#!/bin/bash
commitlint -e $1
EOF
chmod +x .git/hooks/commit-msg
```

---

## Release 流程

### 发布前检查清单

- [ ] 所有 `feature/*` 分支已合并到 `develop`
- [ ] `develop` 分支 CI 通过
- [ ] 版本号已更新
- [ ] CHANGELOG.md 已更新
- [ ] 静态分析无新增警告
- [ ] 单元测试全部通过
- [ ] 集成测试通过（STM32 + ESP32 联调）
- [ ] OTA 回滚测试通过

### 发布步骤

```bash
# 1. 从 develop 切出 release 分支
git checkout -b release/v3.2.0 develop

# 2. 更新版本号（各平台）
# - stm32/CMakeLists.txt: project(... VERSION 3.2.0)
# - esp32/CMakeLists.txt: project(... VERSION 3.2.0)
# - shared/protocol/lingxi_protocol.h: #define LX_PROTO_VERSION 0x32

# 3. 更新 CHANGELOG.md
# 参考 "变更日志" 章节格式

# 4. 提交版本更新
git add -A
git commit -m "chore(release): bump version to v3.2.0"

# 5. 合并到 main
git checkout main
git merge --no-ff release/v3.2.0 -m "release: v3.2.0"

# 6. 打标签
git tag -a v3.2.0 -m "Release v3.2.0 - 灵犀智空 BL 感知模组"

# 7. 合并回 develop
git checkout develop
git merge --no-ff main -m "chore: merge release v3.2.0 back to develop"

# 8. 删除 release 分支
git branch -d release/v3.2.0

# 9. 推送
git push origin main develop --tags
```

### 热修复流程

```bash
# 1. 从 main 切出 hotfix 分支
git checkout -b hotfix/v3.2.1 main

# 2. 修复 Bug
# ... 修改代码 ...

# 3. 更新版本号（PATCH 级别）
# v3.2.0 -> v3.2.1

# 4. 提交修复
git commit -m "fix(stm32): 修复 SDIO 传输偶发卡死问题"

# 5. 合并到 main 和 develop
git checkout main
git merge --no-ff hotfix/v3.2.1
git tag -a v3.2.1 -m "Hotfix v3.2.1"

git checkout develop
git merge --no-ff hotfix/v3.2.1

# 6. 清理
git branch -d hotfix/v3.2.1
git push origin main develop --tags
```

---

## 版本号规则

采用 **语义化版本 (SemVer 2.0.0)**：

```
MAJOR.MINOR.PATCH[-PRERELEASE][+BUILD]
```

| 字段 | 说明 | 递增条件 |
|-----|------|---------|
| `MAJOR` | 主版本 | 不兼容的 API 变更 |
| `MINOR` | 次版本 | 向后兼容的功能新增 |
| `PATCH` | 修订号 | 向后兼容的 Bug 修复 |
| `PRERELEASE` | 预发布 | `alpha`, `beta`, `rc1` |
| `BUILD` | 构建元数据 | 构建编号、Git SHA |

### 版本示例

| 版本 | 说明 |
|-----|------|
| `3.2.0` | 正式版 |
| `3.2.0-alpha.1` | 内测版 |
| `3.2.0-beta.2` | 公测版 |
| `3.2.0-rc.1` | 候选版 |
| `3.2.0+build.20260515` | 带构建信息 |

### 固件版本映射

```c
/* lingxi_protocol.h */
#define LX_PROTO_VERSION_MAJOR  3
#define LX_PROTO_VERSION_MINOR  2
#define LX_PROTO_VERSION_PATCH  0
#define LX_PROTO_VERSION        0x32  /* 协议版本字节 */
```

---

## 标签管理

### 标签命名

```bash
# 正式版本
git tag -a v3.2.0 -m "Release v3.2.0"

# 预发布版本
git tag -a v3.2.0-beta.1 -m "Beta v3.2.0-beta.1"

# 带构建信息（轻量标签）
git tag v3.2.0+build.20260515
```

### 标签推送

```bash
# 推送单个标签
git push origin v3.2.0

# 推送所有标签
git push origin --tags

# 删除远程标签
git push origin --delete v3.2.0
```

### 查看版本历史

```bash
# 列出所有标签
git tag -l 'v3.*'

# 查看标签详情
git show v3.2.0

# 按时间排序
git tag --sort=-creatordate
```

---

## 变更日志

### CHANGELOG.md 格式

```markdown
# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- 待发布的新功能

### Changed
- 待发布的变更

### Fixed
- 待发布的修复

## [3.2.0] - 2026-05-15

### Added
- feat(stm32): 添加 Neural-ART NPU 推理支持
- feat(protocol): 定义 v3.2 通信协议（SDIO 帧格式）
- feat(esp32): 添加 WiFi 6 + BLE 5 + Zigbee 并发支持
- feat(shared): 实现 ring_buffer、crc16、byteorder、debug_log 工具库
- feat(test): 添加 Unity 单元测试框架

### Changed
- refactor(stm32): 优化 SDIO DMA 传输效率
- docs(dev): 重写开发环境搭建指南

### Fixed
- fix(protocol): 修复帧长度字段在大端模式下的解析错误
- fix(stm32): 修复 FreeRTOS 任务栈溢出检测

## [3.1.1] - 2026-04-20

### Fixed
- fix(esp32): 修复 BLE 连接断开后内存泄漏

## [3.1.0] - 2026-04-01

### Added
- feat(stm32): 添加 ToF 传感器驱动

[Unreleased]: https://github.com/lingxi/bl-firmware/compare/v3.2.0...HEAD
[3.2.0]: https://github.com/lingxi/bl-firmware/compare/v3.1.1...v3.2.0
[3.1.1]: https://github.com/lingxi/bl-firmware/compare/v3.1.0...v3.1.1
[3.1.0]: https://github.com/lingxi/bl-firmware/releases/tag/v3.1.0
```

### 自动生成 CHANGELOG

```bash
# 使用 git-chglog（推荐）
# 安装
go install github.com/git-chglog/git-chglog/cmd/git-chglog@latest

# 生成
git-chglog -o CHANGELOG.md

# 或生成指定范围
git-chglog v3.1.0..v3.2.0 -o CHANGELOG_v3.2.0.md
```

---

## CI/CD 集成

### GitHub Actions 示例

```yaml
# .github/workflows/ci.yml
name: CI

on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main, develop ]

jobs:
  build-stm32:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install ARM toolchain
        run: |
          wget -q https://developer.arm.com/-/media/Files/downloads/gnu/13.2.rel1/binrel/arm-gnu-toolchain-13.2.rel1-x86_64-arm-none-eabi.tar.xz
          sudo tar -xJf arm-gnu-toolchain-13.2.rel1-x86_64-arm-none-eabi.tar.xz -C /opt/
          echo "/opt/arm-gnu-toolchain-13.2.rel1-x86_64-arm-none-eabi/bin" >> $GITHUB_PATH
      - name: Build STM32
        run: |
          cd stm32
          cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake -G Ninja
          cmake --build build

  build-esp32:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install ESP-IDF
        run: |
          git clone -b v5.2 --recursive https://github.com/espressif/esp-idf.git ~/esp-idf
          ~/esp-idf/install.sh esp32c6
      - name: Build ESP32
        run: |
          . ~/esp-idf/export.sh
          cd esp32
          idf.py set-target esp32c6
          idf.py build

  test-shared:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Run Unit Tests
        run: |
          cd tests
          make
          ./test_runner
```

---

*文档结束*
