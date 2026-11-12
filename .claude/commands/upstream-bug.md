---
description: Fix the most impactful open `upstream` bug here, merge it, then report it to joeycastillo/second-movement as an issue and a PR
argument-hint: "[ticket number]"
---

# /upstream-bug

Take **one** bug in code shared with `joeycastillo/second-movement` from
ticket to merged fix here, then to an issue and a pull request upstream.
`docs/workflow.md` is the source of truth for every step; this command only
fixes the order and adds the upstream issue.

Ticket: `$ARGUMENTS` (if empty, triage and pick one).

```
Progress:
- [ ] 1. Triage the `upstream` tickets and pick one
- [ ] 2. Fix it here and merge (the /full-send loop, one ticket)
- [ ] 3. Port the fix onto upstream/main and build it
- [ ] 4. Open the upstream issue, then the upstream PR
- [ ] 5. Record it on our ticket and make sure the ticket is closed
- [ ] 6. Status and stop
```

## 1. Triage and pick

Skip this step if a ticket number was given, but still check that it is
open, labelled `upstream` and unclaimed.

```bash
gh issue list -R swamp-dev/second-movement --state open --label upstream --limit 200 --json number,title,labels
```

Leave out tickets another session holds (`/start-ticket` § Ticket claims).
Read the body of every ticket that could plausibly rank first, and order
them by:

1. **Reach.** Code every build links (`movement.c`, `watch-library/`,
   `filesystem/`) or a face in upstream's default `movement_config.h` ranks
   above a face few people build. The ticket's "In our build" line says
   whether this fork runs the code.
2. **Harm.** Wrong time or wrong alarm, lost or corrupted stored data, and
   memory errors rank above a wrong character on the display.
3. **How often.** Normal use ranks above a rare input.
4. `priority:low` goes last.

A ticket whose fix needs a change inside a submodule (`gossamer`, `littlefs`,
`tinyusb`, `utz`) is Blocked: say so and skip it.

Print the top five as a table with one line of reasoning each, name the pick,
and go on without waiting.

## 2. Fix it here and merge

Run the `/full-send` per-ticket loop for that ticket only:
`/start-ticket <N>`, implement, `/finish-ticket`, hosted CI green,
`/ship-ticket <P>`. Everything in `docs/workflow.md` applies (failing test
first, simulator check, reviewers, `scripts/ci-local.sh`, merge authority).

Write the fix so it ports:

- Before designing it, run `git diff upstream/main origin/main -- <files>`
  for the files you expect to touch. Where the fork's copy has diverged,
  keep the fix clear of the fork's own lines.
- Keep new logic in its own `lib/<dir>` with no reference to this fork's
  tickets, tests or process in the code that will go upstream.

If the PR goes to the user to merge, or anything in `/full-send` § When to
stop applies, stop there. Steps 3–5 need the fix merged to `main`.

## 3. Port and build

Follow [`docs/workflow.md` § Upstream PRs](../../docs/workflow.md#upstream-prs)
steps 1–3: check the fix is merged here and that upstream has neither the fix
nor a PR for it, make `up/<N>-<slug>` off `upstream/main` with only the fix,
and build all 8 board × display combinations. Host tests don't go upstream
(it has no `test/`), so compile this fork's test file against the `up/`
branch's sources once, outside the worktree, and say so in the PR.

## 4. Upstream issue, then upstream PR

Invoking this command is the user's request for one issue and one PR on
`joeycastillo/second-movement`, for this ticket. Search first:

```bash
gh issue list -R joeycastillo/second-movement --state all --search "<face or function> <symptom>"
```

If an upstream issue already describes the bug, don't open another; the PR
refers to that one.

Otherwise open the issue. Write it for a maintainer who has never seen this
fork: what goes wrong, steps to trigger it, observed and expected, where in
the code, and the root cause in two or three sentences. Leave out this
fork's ticket numbers as `#N` (on upstream they point elsewhere), local
paths and process notes. End with a line saying it was found and written up
with Claude Code. Body from a file outside any worktree:

```bash
gh issue create -R joeycastillo/second-movement --title "<summary>" --body-file <file>
```

Then push the branch and open the PR as in § Upstream PRs step 4. Its body
adds `Fixes #<upstream issue>` on its own line, what the change does, and
how it was tested (builds, simulator steps, the host test here with the full
URL of this fork's merged PR).

## 5. Record and close

```bash
gh issue comment <N> -R swamp-dev/second-movement --body "Sent upstream: issue <URL>, PR <URL>"
gh issue edit <N> -R swamp-dev/second-movement --remove-label upstream --add-label upstream-sent
gh issue view <N> -R swamp-dev/second-movement --json state -q .state
```

Create the `upstream-sent` label first if it's missing. The merge in step 2
normally closed the ticket through `Closes #N`. If it still prints `OPEN`
and the fix is merged, close it:
`gh issue close <N> -R swamp-dev/second-movement --reason completed`.

## 6. Status and stop

Report the ticket, the PR here, the upstream issue and PR, and the leftovers
for the user to remove (the `up/` worktree and branch stay until the
upstream PR is merged or closed). Then stop.

## Limits

- One ticket, one upstream issue and one upstream PR per invocation. Never
  loop over the queue.
- Nothing else on the upstream repo: no comments, no replies to review, no
  pushes to the `up/` branch after the PR is open, no closing. When the
  maintainers respond or upstream CI fails, tell the user and wait.
- If the fix can't go upstream (nothing left after dropping fork-only
  changes, or upstream already has it), follow § Upstream PRs for that
  case and open nothing upstream.
