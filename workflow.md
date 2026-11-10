# Ticket workflow

**This doc is the source of truth for this repo's ticket workflow.** Where
`CLAUDE.md`, the skills (`/full-send`, `/start-ticket`, `/finish-ticket`,
`/ship-ticket`) or `~/.dotfiles/.claude/rules/` differ from it, this doc
wins.

Build, toolchain and face-registration details are in
[`../CLAUDE.md`](../CLAUDE.md).

---

## The steps

| # | Step | Automation |
|---|---|---|
| 1 | Worktree and branch off the latest `origin/main`, then read the ticket in full and claim it | `/start-ticket <N>` |
| 2 | Implement it (see [Testing](#testing)) | manual |
| 3 | Run the per-PR checklist below, then open the PR | `/finish-ticket` |
| 4 | Wait for hosted CI to go green: every `build` and `build-simulator` job of the `Build` workflow, for both the push run and the pull_request run | `gh pr checks <P>` |
| 5 | Merge and clean up, or hand the PR to the user (see [Merge authority](#merge-authority)) | `/ship-ticket <P>` |

Do the steps in order and don't skip any. `/full-send` loops steps 1–5
over the open backlog and pauses after each ticket.

---

## Per-PR checklist (mandatory)

This adapts `~/.dotfiles/.claude/rules/local-ci-and-admin-merge.md`; where they differ, this list wins.

1. **Code review.** Run the `code-reviewer` subagent on the diff. If the
   diff touches `*.md`, `docs/` or `.claude/`, also run `docs-reviewer`.
2. **Address findings.** Fix every critical and significant finding. List
   any minor ones you don't fix in the PR body.
3. **Run all CI jobs locally:** `scripts/ci-local.sh`, which builds the
   firmware and simulator for every board × display. Every combination
   must pass, and a single-combination run doesn't count.
4. **Apply fixes** for any local failure. If a fix touches behaviour the
   review didn't cover, restart from step 1.
5. **Re-review when the fixes were substantial**, meaning any of:
   - new files since the first review;
   - more than 50 net lines added since then;
   - a new non-`static` function, or a changed signature for
     `_face_setup`, `_activate`, `_loop` or `_resign`;
   - a fix in an area the first review flagged as critical or significant.

The PR body records what ran under a **Per-PR checklist** heading:
- which reviewers ran (`code-reviewer`, plus `docs-reviewer` when
  triggered) and which findings were addressed;
- `scripts/ci-local.sh` (full matrix): `all combinations passed` at
  `<head SHA>`.

Update that section every time you push a new head.

This repo has no stamp file. Instead of asking the user, `/ship-ticket`
step 3 checks it itself: merge only if **this session** ran every item above
and the recorded SHA equals the PR's `headRefOid`. If not (for example, a
new session after `/handoff` or `/resume`), re-run the checklist on the
current head or hand the PR to the user.

---

## Testing

- **Pure logic** (time, astronomy, zodiac, packing and unpacking): TDD with
  host-compiled unit tests once #15 adds the harness. Until then, write the
  logic so it's testable (a pure function with no `watch_*` calls) and say
  in the PR that tests are pending #15.
- **Display, buttons and face flow:** check in the simulator
  (`emmake make …`, see `CLAUDE.md`) and describe what you checked in the
  PR. If you can't drive a browser, say so in the Test plan and leave the
  check to the user in a `manual-task` issue.
- **On-watch behaviour** (power, sleep, sensors, real time passing): an
  agent can't check this. Before opening the PR, open a `manual-task` issue
  (group `devices`) saying what to flash and what to look for, and link it
  under Manual follow-ups.

---

## Merge authority

The global default (`~/.dotfiles/.claude/rules/collaboration.md`) is that
the user merges. **This repo relaxes that.** The agent may merge through
`/ship-ticket` when **all** of these hold:

- the per-PR checklist ran in **this session** on **this PR's current
  head**, and the PR body records it;
- hosted CI is green on the current head SHA, with every `Build` job
  completed and successful;
- the PR targets `main`;
- it's **one PR per `/ship-ticket` invocation**. Never merge a list, and
  never sweep "all green PRs".

These PRs always go to the user to merge, even when every condition holds:

- **Gate-introducing PRs:** changes to this doc, `CLAUDE.md`'s CI commands
  or Testing sections, `scripts/ci-local.sh`, anything under `.claude/`, or
  `.github/workflows/`.
- Submodule pointer bumps (`gossamer`, `littlefs`, `tinyusb`, `utz`).
- Upstream syncs from `joeycastillo/second-movement`.

For these, stop `/ship-ticket` before step 5 (merge). Print the PR URL and
its CI status, and leave the claim, worktree and branch in place. After the
user merges, post the closeout comment (`/ship-ticket` step 6 format) and
clean up (step 7).

**Admin merge** (bypassing hosted CI) is never the agent's call. It's
governed by `~/.dotfiles/.claude/rules/local-ci-and-admin-merge.md`, and the
user decides.

---

## Branches, worktrees, commits

- **Worktree:** `../sm-<N>-<slug>`. The `sm-` prefix avoids clashes with
  other repos in `~/dev/`. Use it wherever a skill writes or derives
  `../<N>-<slug>`: `/start-ticket` steps 5, 5b, 6, 7 and 8, and
  `/ship-ticket` steps 2, 3, 7 and 8.
- **Branch:** `<type>/<N>-<slug>`, where `<type>` is one of `feat`, `fix`,
  `chore`, `refactor`, `test` or `docs`. The `documentation` label maps to
  `docs`. Branch from `origin/main` and target PRs at `main`.
- **New worktrees need submodules:** run
  `git submodule update --init --recursive` right after
  `git worktree add`. This replaces `/start-ticket`'s "install
  dependencies" step.
- **Commits:** Conventional Commits, `<type>(<scope>): <summary>`, where
  scope is usually the face (`fix(zodiac_face): …`). Put failing-test
  commits before implementation commits.
- **PR:** Conventional-Commit title, `Closes #N` on its own line in the
  body, and the Per-PR checklist section.
- **`KnightConfig`** is the owner's personal build branch. Agents never
  push to it, rebase it, or open PRs against it.

---

## Manual tasks

Work only the user can do (flash the watch and check behaviour) goes in a
GitHub issue labelled `manual-task` plus a group label (`devices` for
anything on the watch). Open it before the PR that creates the need, and
link it in that PR's Manual follow-ups. Add `priority:high` when the
merged code is unverified until the task is done. Only the user closes
these issues. If a label is missing, create it first:
`gh label create manual-task`, `gh label create devices`,
`gh label create priority:high`.

---

## Where to find what

| Question | File |
|---|---|
| Build, toolchain, adding a face, gotchas | [`../CLAUDE.md`](../CLAUDE.md) |
| Local CI gate | [`../scripts/ci-local.sh`](../scripts/ci-local.sh) |
| Hosted CI | [`../.github/workflows/build.yml`](../.github/workflows/build.yml) |
| Per-PR checklist, admin merge (global default) | `~/.dotfiles/.claude/rules/local-ci-and-admin-merge.md` |
| Default merge authority, sub-agent limits | `~/.dotfiles/.claude/rules/collaboration.md` |
| Generic end-to-end process | `~/.dotfiles/.claude/rules/end-to-end-process.md` |

If the workflow ever seems to call for something risky (sweeping several
PRs, force-pushing, pushing to `KnightConfig`, skipping the checklist),
**stop and ask the user.**
