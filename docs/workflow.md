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
| 3 | Run the per-PR checklist below (`/finish-ticket` doesn't cover checklist items 1–2), then open the PR | `/finish-ticket` |
| 4 | Wait for hosted CI to go green: the `host-tests` job and every `build` and `build-simulator` job of the `Build` workflow, for both the push run and the pull_request run | `gh pr checks <P>` |
| 5 | Merge and clean up, or hand the PR to the user (see [Merge authority](#merge-authority)) | `/ship-ticket <P>` |
| 6 | If the ticket is labelled `upstream`, send the fix upstream once the PR is merged (see [Upstream PRs](#upstream-prs)) | manual |

Do the steps in order and don't skip any. `/full-send` loops steps 1–6
over the open backlog and pauses after each ticket.

---

## Per-PR checklist (mandatory)

This adapts `~/.dotfiles/.claude/rules/local-ci-and-admin-merge.md`; where they differ, this list wins.

1. **Tests for the logic.** Every change to non-trivial logic (arithmetic,
   time and astronomy, branching on data, packing and unpacking, when a face
   refreshes) comes with host unit tests in the same PR (see
   [Testing](#testing)). If that logic sits in a face, move it into a pure
   function in `lib/` first. A fix for a bug in that logic starts with a
   test that fails, committed before the fix; push it together with the fix,
   since CI judges only the head. Display text and button-to-screen flow
   need no host test: item 2 covers them, and the Test plan says so. Changes
   with no logic change (docs, CI, comments, renames) record "Tests: N/A"
   and why. When unsure whether logic is trivial, write the test.
2. **Simulator check.** Every change under `watch-faces/`, to
   `movement*.{c,h}`, or to `lib/`, `filesystem/`, `location/` or
   `watch-library/` code a built face calls is checked in the simulator
   before the PR opens. Drive
   the screens and buttons the change affects (for shared code, a face
   that calls it, named), and record the steps and what you saw in the Test
   plan. Build-system, CI, docs and test-only changes record
   "Simulator: N/A". An agent should always be able to do this check; if it
   can't, open a `manual-task` issue for it, labelled `priority:high` since
   the merged code is unchecked until it's done, and link it under Manual
   follow-ups. That issue satisfies this item for the merge gate.
3. **Code review.** Run the `code-reviewer` subagent on the diff. If the
   diff touches `*.md`, `docs/` or `.claude/`, also run `docs-reviewer`.
4. **Address findings.** Fix every critical and significant finding. If a
   fix changes behaviour, redo steps 1 and 2 for it. List any minor ones you
   don't fix in the PR body.
5. **Run all CI jobs locally:** `scripts/ci-local.sh`, which runs the host
   unit tests and builds the firmware and simulator for every board ×
   display. Every combination must pass, and a single-combination run
   doesn't count.
6. **Apply fixes** for any local failure. If a fix changes behaviour, redo
   steps 1 and 2 for it. If it touches code the review didn't cover, restart
   from step 3.
7. **Re-review when the fixes were substantial**, meaning any of:
   - new files since the first review;
   - more than 50 net lines added since then;
   - a new non-`static` function, or a changed signature for
     `_face_setup`, `_activate`, `_loop` or `_resign`;
   - a fix in an area the first review flagged as critical or significant.

The PR body records what ran under a **Per-PR checklist** heading:
- the tests added (or N/A), and the SHA of the failing-test commit for a
  bug fix (what was left untested goes in the Test plan);
- the simulator check: done (steps in the Test plan), N/A, or the
  `manual-task` issue;
- which reviewers ran (`code-reviewer`, plus `docs-reviewer` when
  triggered) and which findings were addressed;
- `scripts/ci-local.sh` (full matrix): `all combinations passed` at
  `<head SHA>`.

Update that section every time you push a new head.

This repo has no stamp file. Instead of asking the user, `/ship-ticket`
step 3 checks it itself: merge only if **this session** confirmed item 1
(the tests exist or N/A is recorded, and the failing-test SHA is recorded
for a bug fix), ran items 2–7, and the recorded SHA equals the PR's
`headRefOid`. If not (for example, a new session after `/handoff` or
`/resume`), re-run the checklist on the current head, including a fresh
simulator check (reading the old Test plan doesn't count), or hand the PR
to the user.

---

## Testing

- **Logic** ([checklist item 1](#per-pr-checklist-mandatory) says which):
  TDD with host unit tests (`make -C test`; how to add one is in
  [`CLAUDE.md` § Testing](../CLAUDE.md#testing)). Put the logic in a pure
  function with no `watch_*` or `movement_*` calls.
- **Display, buttons and face flow:** check in the simulator
  (`emmake make …`, see `CLAUDE.md`), as the
  [Per-PR checklist](#per-pr-checklist-mandatory) requires.
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
  or Testing sections, `scripts/ci-local.sh`, `test/check.h`,
  `test/Makefile`, anything under `.claude/`, or `.github/workflows/`.
  Routine `test/Makefile` edits don't count: adding a `test_<name>` to
  `TESTS` with its `test_<name>_SRCS`, or a `-I../lib/<dir>` to
  `CPPFLAGS`, nor do the root `Makefile` lines for a new `lib/<dir>`.
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
  body, a **Test plan** section (tests, simulator steps, anything left
  untested), the Per-PR checklist section, and Manual follow-ups.
- **`KnightConfig`** is the owner's personal build branch. Agents never
  push to it, rebase it, or open PRs against it.
- **Upstream branches** (`up/<N>-<slug>`, off `upstream/main`) are the one
  exception to "branch from `origin/main`". See
  [Upstream PRs](#upstream-prs).

---

## Upstream PRs

The `upstream` label marks a bug in code this fork shares with
`joeycastillo/second-movement`. These bugs are filed here only. The fix
lands here first, through steps 1–5, and then goes upstream as a pull
request. Tickets without the label skip this section.

**When:** after the fix is merged to `main` here, never before. One
upstream PR per ticket, and never a batch.

### 1. Check that upstream still needs it

```bash
git fetch upstream
git diff upstream/main main -- <files the fix touched>
gh pr list -R joeycastillo/second-movement --state all --search "<face or function name>"
```

If `upstream/main` already has a fix, or an open upstream PR makes the same
change, don't open another. Say so in a comment on the ticket, remove the
`upstream` label, and stop.

### 2. Make the branch

```bash
git worktree add ../sm-up-<N>-<slug> -b up/<N>-<slug> refs/remotes/upstream/main
cd ../sm-up-<N>-<slug>
git submodule update --init --recursive
git cherry-pick -n <SHA of the squash-merge commit on main>
```

Then cut the change down to what upstream can take:

- **Keep** the fix itself. If the fix moved logic into a new `lib/<dir>`,
  keep those files and their lines in the root `Makefile`.
- **Drop** everything upstream doesn't have: `test/`, `scripts/`, `docs/`,
  `CLAUDE.md`, `.claude/`, `.github/`, `movement_config.h`, and any
  fork-only face or library (`pet`, `planetary`, `zodiac`, `sun_events`,
  `location/`).
- **Port by hand** where the fork's file has diverged
  (`git diff upstream/main main -- <file>` shows unrelated differences, as
  for `sunrise_sunset_face.c` and `filesystem/filesystem.c`). Apply the same
  fix to upstream's version of the code.

If the fix only makes sense on top of fork-only code, it can't go upstream.
Say why in a comment on the ticket, remove the `upstream` label, and stop.

Commit it as one commit whose message describes the bug and the fix.
Don't write `#<N>` in the message or the PR: on upstream that number points
at a different issue. Refer to the ticket as
`swamp-dev/second-movement#<N>`.

### 3. Check it

Upstream has no host tests and no `scripts/ci-local.sh`, so build the full
matrix by hand on the upstream branch:

```bash
nix=(nix --extra-experimental-features 'nix-command flakes' develop -c)   # drop if the toolchain is on PATH
for board in sensorwatch_red sensorwatch_green sensorwatch_blue sensorwatch_pro; do
  for display in classic custom; do
    rm -rf build build-sim
    "${nix[@]}" make BOARD=$board DISPLAY=$display || break 2
    "${nix[@]}" emmake make BOARD=$board DISPLAY=$display || break 2
  done
done
```

All 16 builds must pass. If step 2 needed a hand port, redo the simulator
check on this branch for the affected face, since the code is no longer
what was checked here.

### 4. Open the PR

```bash
git push -u origin up/<N>-<slug>
gh pr create -R joeycastillo/second-movement --base main \
  --head swamp-dev:up/<N>-<slug> --title "<summary>" --body-file <file>
```

The body is the bug report, written for a maintainer who has never seen
this fork:

- what goes wrong and how to trigger it (steps, observed, expected);
- the root cause, in two or three sentences;
- what the change does;
- how it was tested: the 16 builds, the simulator steps, and the host test
  in this fork with a link to the merged PR here;
- a line saying the change was prepared with Claude Code.

### 5. Record it

Comment on the ticket here with the upstream PR's URL, and swap the label:

```bash
gh issue edit <N> --remove-label upstream --add-label upstream-sent
```

(`gh label create upstream-sent` if it doesn't exist yet.) Closed tickets
still labelled `upstream` are the queue of fixes waiting to go upstream:
`gh issue list --state closed --label upstream`.

### What the agent may do upstream

Opening the upstream PR in step 4 is authorised by this section, under
these limits:

- only for a ticket labelled `upstream` whose fix is already merged here;
- one upstream PR per ticket, opened one at a time;
- nothing else on the upstream repo without the user asking: no issues, no
  comments or replies to review, no further pushes to the `up/` branch
  after the PR is open, and no closing or merging.

When the upstream maintainers respond, tell the user and wait. The `up/`
branch and its worktree stay until the upstream PR is merged or closed.

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
