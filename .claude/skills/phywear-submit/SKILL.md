---
name: phywear-submit
description: 把 PhyWear 的改动**严格按流程**提交到 427 参赛仓（contest2026_427_xinpingqihe）：回写快照、重生成清单、预检红线、按真实邮箱提交、推送工作分支并同步默认分支、校验远端 SHA、维护 PR。Use when the user says 提交 / push / 推送 427 / 更新仓库 / 提 PR / commit and push / 同步到默认分支, or when any code change must reach the contest repo. 任何模型（含小米 MiMo）执行提交动作前必须先读本 SKILL，禁止凭记忆自行 git push。
---

# 提交 427 参赛仓 —— 严格流程与铁律

> **一句话**：**唯一允许的提交入口是 `bash submit_427.sh`**。不要手敲 `git push`，不要自己拼提交、不要"顺手"改仓库结构。

## 0. 铁律（违反任意一条 = 停止并报告，不许"想办法绕过"）

| # | 铁律 |
|---|---|
| S1 | **只跑脚本**：提交一律 `bash submit_427.sh`（默认演练；确认后 `--execute`）。手敲 git 命令只允许用于**查看**（status/log/ls-remote）。 |
| S2 | **绝不 force-push 官方仓**（`open-vela/*`）。只有我们的 fork 默认分支允许 `--force-with-lease`，且必须先自动建备份分支。 |
| S3 | **绝不提交密钥**：`tp-…`/`sk-…`/`ghp_…`/`github_pat_…` 一律禁止入库；MiMo Key 只放 `~/.config/phywear/mimo.key`（600）。预检命中即中止。 |
| S4 | **绝不提交构建产物**：`cmake_out/`、`nuttx.bin`、`*.o/*.a`、迁移包 `.zip` 一律不提交。**唯一允许的二进制**是 `board/ftab_openvela.bin`（烧录必需）。 |
| S5 | **绝不出现 merge commit**：PR 只能用 **rebase** 合并；本地同步用 rebase，不 `git merge` 上游。 |
| S6 | **提交身份必须真实**：`XPQHyue <15770782523@163.com>`；不许改成别的名字/邮箱。 |
| S7 | **先回写再提交**：任何 `~/openvela` 里的改动都要先 `sync_back.py --execute`（脚本会自动做），否则评审 clone 看不到。 |
| S8 | **提交前 P2 必须全绿**：`check_progress.py` 的快照↔工作区一致性不允许 ❌；有 ❌ 就停下修，不许 `--force`。 |
| S9 | **改 Skill 必须重生成 blob**：动过 `app/phywear/skills/*.md` 就要 `gen_skill_blob.py`，且 `--check` 通过。 |
| S10 | **推送后必须核对远端**：脚本会比对本地 HEAD 与远端两个 ref 的 SHA，不一致就报失败；**不许口头声称"已推送"**。 |
| S11 | **默认分支必须同步**：每次推送都把 fork 的 `dev-ai-contest-2026` 指到同一次提交（用户要求"最新的提交到我的默认分支"）。 |
| S12 | **日志单独交付**：AI Coding 日志是独立 10 分维度，工作时段结束必须跑 `phywear-migrate/finish_session.sh`。 |
| S14 | **PR 合并后先 rebase 再继续**：rebase-merge 改写 SHA；本地带旧提交会产生「重复提交 + 冲突」（`dirty`）。先 `git fetch origin`，再 `git rebase --onto origin/dev-ai-contest-2026 <上次已合并的最后一个提交>` |
| S13 | **改动固件必须更新文档**：烧录后把新 md5/大小/复验结果写进 `docs/06` 与 `docs/07`，再提交。 |

## 1. 正常提交流程（照着做，不要跳步）

```bash
cd <参赛仓>                                   # 默认 ~/work/contest2026_427_xinpingqihe

bash .claude/skills/phywear-submit/submit_427.sh            # ① 演练：回写+清单+预检，全部只打印
#   看输出确认无误（应显示：回写 N 个、P2 全绿、无红线命中、将推送到哪两个 ref）

bash .claude/skills/phywear-submit/submit_427.sh --execute -m "feat(phywear): 你的改动说明"
```

脚本按顺序做这些事（每一步失败即中止）：

1. **回写快照**：`sync_back.py --execute`（工作区 → 参赛仓）
2. **重生成清单**：`gen_manifest.py --write`
3. **一致性核对**：`check_progress.py`，要求 **P2 全绿**（快照 ↔ 工作区）
4. **Skill blob 校验**：若 `app/phywear/skills/*.md` 有改动 → `gen_skill_blob.py` + `--check`
5. **红线预检**：密钥正则、禁止路径、单文件 >5 MB、二进制白名单
6. **提交**：`git add -A` + 指定作者/邮箱提交（无改动则跳过）
7. **推送工作分支**：`git push fork HEAD:<工作分支>`
8. **同步默认分支**：先 `backup-default-<日期>` 备份远端旧 tip，再 `--force-with-lease` 把 `dev-ai-contest-2026` 指到本次提交
9. **远端校验**：`git ls-remote fork` 两个 ref 必须等于本地 HEAD，否则**报失败**
10. **PR 提示**：打印 PR 链接、当前提交数，提醒 merge 用 rebase

## 2. 提交后必须人工确认的三件事

1. PR 页面显示最新提交数：<https://github.com/open-vela/contest2026_427_xinpingqihe/pull/11>
2. 我们的仓库首页（默认分支）显示今天：<https://github.com/XPQHyue/contest2026_427_xinpingqihe>
3. 官方分支何时拿到代码取决于 **merge**（只有维护者/队长能点）；未 merge 前"官方仓没有最新代码"是正常状态，**报告里必须如实说明**，不许说成"已提交到官方"。

## 3. 出问题时的正确反应

| 情况 | 正确做法 | 禁止 |
|---|---|---|
| 预检报"密钥命中" | 把该文件加入 `.gitignore`/移出暂存区，改放 `~/.config/phywear/` | 改正则绕过、直接推 |
| 预检报"文件过大" | 确认是否构建产物；是则删除并不提交 | `--force` 推上去 |
| P2 出现 ❌ | 先跑 `restore_code.py`/`sync_back.py` 修平 | 无视 ❌ 继续提交 |
| 远端 SHA 校验失败 | 重新 `git push`（网络抖动常见），仍失败则报告 | 声称成功 |
| PR 冲突 | `git fetch origin && git rebase origin/dev-ai-contest-2026`，解决后再推 | `git merge` 造 merge commit |
| 不确定该不该提交某个文件 | 停下来问队长 | 猜 |
