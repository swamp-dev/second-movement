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
clean up ([Cleanup after a merge](#cleanup-after-a-merge)).

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

### Cleanup after a merge

Every worktree here has submodules, so `git worktree remove` always
refuses, and after a squash merge `git branch -d` usually does too.
`/ship-ticket` step 7 forbids `--force` and leaves `-D` to the user. **This
repo relaxes that** for these two commands: the owner has given a standing
go-ahead (`~/.dotfiles/.claude/rules/collaboration.md` § Standing grant:
post-merge cleanup), and this section is how to use it here. In step 7, run
the block below from the main checkout in place of `git worktree remove`,
`git branch -d` and the patch-id check, and don't hand the commands back to
the user. The block is stricter than the patch-id check: the local branch
must be exactly the PR's merged head.

Run it only for a worktree this session created, and only for the PR this
`/ship-ticket` run merged or one the user said in this conversation they
merged; `<P>` is that PR's number. Put these setter lines and the block in
one Bash call:

```bash
pr() { gh pr view <P> -R swamp-dev/second-movement --json "$1" -q ".$1"; }
[ "$(pr state)" = MERGED ] && [ "$(pr baseRefName)" = main ] && fin=yes || fin=no
b=$(pr headRefName); safe=$(pr headRefOid)
```

```bash
main=$(git worktree list --porcelain | sed -n '1s/^worktree //p')
w=$(git worktree list --porcelain | awk -v r="branch refs/heads/$b" '/^worktree /{p=substr($0,10)} $0==r{print p}')
tip=$(git rev-parse --verify -q "refs/heads/$b")
dirty=$(git -C "${w:-/dev/null}" status --short --ignored --ignore-submodules=none 2>&1 \
  | grep -vE '^!! (build|build-sim|test/build|\.emscripten_cache|\.direnv)/$')
sub=$(git -C "${w:-/dev/null}" submodule foreach --quiet --recursive \
  'git stash list; git log --oneline --all --not --remotes' 2>&1)
case "$b" in ""|main|KnightConfig) fin=no ;; esac
if [ "$fin" = yes ] && [ "$(git rev-parse --show-toplevel)" = "$main" ] \
   && [ -n "$w" ] && [ "$w" != "$main" ] && [ -n "$tip" ] && [ "$tip" = "$safe" ] \
   && [ -z "$dirty" ] && [ -z "$sub" ]; then
  git worktree remove --force "$w" && git branch -D "$b" && echo "REMOVED $w $b"
else
  echo "LEFT: fin=$fin branch=$b worktree=$w tip=$tip safe=$safe"
  printf '%s\n' "$dirty" "$sub"
fi
```

It prints `REMOVED` or `LEFT`. On `LEFT`, leave both and tell the user what
it printed; don't work around it. `--force` deletes everything in the
worktree, including ignored files and the submodules' own git directories,
so the block refuses if anything but build output is ignored there, or if
a submodule holds a stash or a commit that isn't on one of its remotes.
Never run `git submodule deinit` to get around git's refusal: it strips the
submodule entries from the config every worktree shares.

This doesn't cover anything else: a worktree or branch this session didn't
create, `KnightConfig` and its worktree, a worktree whose PR is still open,
or a remote branch (`/ship-ticket` already deletes the merged PR's own).

---

## Upstream PRs

The `upstream` label marks a bug in code this fork shares with
`joeycastillo/second-movement`. It's filed here only. Once the fix is
merged to `main` here (steps 1–5), never before, send it upstream as a pull
request. An upstream PR is not an upstream sync: it never touches `main`
here.

Each Bash call is a new shell, and the `../` paths below assume it starts
in the main checkout. Every command run in the upstream worktree, including
the ones given in prose, starts with this guard, so that a failed `cd` can't
reset or delete anything in the main checkout:

```bash
cd ../sm-up-<N>-<slug> && [ "$(git branch --show-current)" = up/<N>-<slug> ] || exit 1
```

### 1. Check that it's merged here and still needed upstream

```bash
git remote get-url upstream || git remote add upstream https://github.com/joeycastillo/second-movement.git
git fetch origin && git fetch upstream
gh pr view <P> -R swamp-dev/second-movement --json state,mergeCommit -q '.state + " " + .mergeCommit.oid'
git log --oneline -n 10 upstream/main -- <files the fix touched>
gh pr list -R joeycastillo/second-movement --state all --search "<face or function name>"
```

`<P>` is the PR that fixed the ticket here. The third command must print
`MERGED` and a SHA: that SHA is the squash commit used in step 2. If it
prints anything else, stop.

If `upstream/main` already has a fix, or an upstream PR (open or closed)
makes the same change, don't open another. Say so in a comment on the
ticket, remove the `upstream` label, and stop. Don't reopen the ticket.

### 2. Make the branch

```bash
git worktree add ../sm-up-<N>-<slug> -b up/<N>-<slug> refs/remotes/upstream/main
cd ../sm-up-<N>-<slug> && [ "$(git branch --show-current)" = up/<N>-<slug> ] || exit 1
git submodule update --init --recursive
git cherry-pick -n <squash SHA from step 1>
```

The cherry-pick usually stops with conflicts, because the squash commit
also touches files upstream doesn't have. That's expected. `-n` makes no
commit and leaves no cherry-pick in progress, so `git cherry-pick --abort`
won't work; to start over, run `git reset --hard HEAD && git clean -fd`
(after the guard) and pick again.

Cut the change down to the fix:

- **Keep** the fix itself. If the fix moved logic into a new `lib/<dir>`,
  keep those files and their lines in the root `Makefile`. Resolve a root
  `Makefile` conflict by hand: take upstream's side and re-add only the
  `-I` and `.c` lines for that `lib/<dir>`.
- **Drop** every other change. For a path upstream lacks, run
  `git rm -rf -- <path>`. For a file upstream has, run
  `git checkout HEAD -- <path>`. The fork changed many shared files
  (`git diff --stat upstream/main origin/main` lists them), so expect to
  drop `test/`, `scripts/`, `docs/`, `CLAUDE.md`, `.claude/`, `.github/`,
  `.gitignore`, `movement_config.h`, and the fork's own lines in
  `movement_faces.h`, `watch-faces.mk` and the root `Makefile`.
- **Port by hand** where the fork's version of a file the fix touches has
  diverged (`git diff upstream/main origin/main -- <file>` shows unrelated
  differences, as for `sunrise_sunset_face.c` and
  `filesystem/filesystem.c`). Take upstream's version with
  `git checkout HEAD -- <file>` and make the same fix in it.

If nothing is left staged after dropping, or the fix only makes sense on
top of fork-only code, it can't go upstream.
Reset as above, say why in a comment on the ticket, remove the `upstream`
label, and stop.

Before committing, check what is staged:

```bash
cd ../sm-up-<N>-<slug> && [ "$(git branch --show-current)" = up/<N>-<slug> ] || exit 1
git add -A && git status --short && git diff --cached --stat
git diff --cached | grep -nE '^\+.*(#[0-9]+|swamp-dev|KnightConfig)'
```

`git add -A` stages hand-ported edits, which `git checkout HEAD --` left
unstaged. The stat may list only files the fix needs, and the `grep` should
print nothing you can't account for.

Commit it as one commit whose message describes the bug and the fix.
Don't write `#<N>` in the message or the PR body: on upstream that number
points at a different issue. Refer to the ticket and the PR here by full
URL.

### 3. Check it

Upstream has no host tests and no `scripts/ci-local.sh`, so build every
board and display by hand on the upstream branch. It can take longer than
one foreground Bash call allows; run it in the background and wait for the
last line.

```bash
cd ../sm-up-<N>-<slug> && [ "$(git branch --show-current)" = up/<N>-<slug> ] || exit 1
nix=(nix --extra-experimental-features 'nix-command flakes' develop -c)   # drop if the toolchain is on PATH
ok=0
for board in sensorwatch_red sensorwatch_green sensorwatch_blue sensorwatch_pro; do
  for display in classic custom; do
    command rm -rf build build-sim
    if "${nix[@]}" make BOARD=$board DISPLAY=$display \
       && "${nix[@]}" emmake make BOARD=$board DISPLAY=$display \
       && [ -f build/firmware.uf2 ] && [ -f build-sim/firmware.html ]; then
      ok=$((ok + 1)); echo "OK $board $display"
    else
      echo "FAILED $board $display"; break 2
    fi
  done
done
echo "combinations passed: $ok of 8"
```

It must end with `combinations passed: 8 of 8` (each is a firmware build
and a simulator build). If one fails, fix the port. If the same build also
fails on `upstream/main` without the change, stop and tell the user.

If step 2 needed a hand port, the code is no longer what was reviewed and
checked here: run `code-reviewer` on `git diff upstream/main`, and redo the
simulator check for the affected face on this branch.

### 4. Open the PR

Write the body to a file outside the worktree, so it can't be committed.

```bash
cd ../sm-up-<N>-<slug> && [ "$(git branch --show-current)" = up/<N>-<slug> ] || exit 1
git push -u origin up/<N>-<slug>
gh pr create -R joeycastillo/second-movement --base main \
  --head swamp-dev:up/<N>-<slug> --title "<summary>" --body-file <file outside the worktree>
```

Don't use `--fill` or `--web`. The push starts the fork's own `Build` run
on the `up/` branch; its `host-tests` job fails there because the branch
has no `test/`. Ignore it.

The body is the bug report, written for a maintainer who has never seen
this fork:

- what goes wrong and how to trigger it (steps, observed, expected);
- the root cause, in two or three sentences;
- what the change does;
- how it was tested: the builds, the simulator steps, and the host test in
  this fork, with the full URL of the merged PR here;
- a line saying the change was prepared with Claude Code.

Leave out local paths and notes about this fork's process.

### 5. Record it

Comment on the ticket here with the upstream PR's URL, and swap the label.
If `gh label list -R swamp-dev/second-movement` doesn't show
`upstream-sent`, create it first with
`gh label create upstream-sent -R swamp-dev/second-movement`.

```bash
gh issue comment <N> -R swamp-dev/second-movement --body "Sent upstream: <upstream PR URL>"
gh issue edit <N> -R swamp-dev/second-movement --remove-label upstream --add-label upstream-sent
```

Closed tickets still labelled `upstream` are the queue of fixes waiting to
go upstream:
`gh issue list -R swamp-dev/second-movement --state closed --label upstream`.

### What the agent may do

This section authorises the agent to push the `up/` branch to the fork, open
the upstream PR in step 4, and comment on and relabel the ticket here, under
these limits:

- only for a ticket labelled `upstream` whose fix step 1 showed as merged;
- only for the ticket whose fix was merged in this session, by the agent or
  by the user. Work through
  the queue of older ones only when the user names the tickets, and a
  listing of that queue is never an instruction to work through it;
- one upstream PR per ticket, one at a time, and never a second PR for a
  change the maintainers closed;
- nothing else on the upstream repo unless the user asks: no issues, no
  comments or replies to review, no further pushes to the `up/` branch
  once the PR is open, and no closing or merging.

When the upstream maintainers respond, or upstream's CI fails on the PR,
tell the user and wait. The `up/` branch and its worktree stay until the
upstream PR is merged or closed. If that happens in the session that
created the worktree, remove both with the block in
[Cleanup after a merge](#cleanup-after-a-merge), using these setter lines
(in one Bash call with the block, from the main checkout). Here the branch
must be at the tip `origin` has, so nothing is lost; if `origin` no longer
has it, `safe` is empty and the block prints `LEFT`. In any other session,
list the worktree and branch as leftovers for the user to remove.

```bash
b=up/<N>-<slug>
u=$(gh pr view <upstream PR URL> -R joeycastillo/second-movement \
  --json state,headRefName,headRepositoryOwner -q '"\(.state) \(.headRefName) \(.headRepositoryOwner.login)"')
case "$u" in "MERGED $b swamp-dev"|"CLOSED $b swamp-dev") fin=yes ;; *) fin=no ;; esac
safe=$(git ls-remote --heads origin "refs/heads/$b" | cut -f1)
```

The `up/` branch on `origin` stays; deleting it is the user's call.

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
