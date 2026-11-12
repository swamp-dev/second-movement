---
description: Fix the most impactful open `upstream` bug here, merge it, then report it to joeycastillo/second-movement as an issue and a PR
argument-hint: "[ticket number]"
---

# /upstream-bug

Take **one** bug in code shared with `joeycastillo/second-movement` from
ticket to merged fix here, then to an issue and a pull request upstream.
`docs/workflow.md` is the source of truth. This command adds the triage
order, the upstream issue, and closing our ticket if the merge didn't.

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

If a ticket number was given, skip the ranking. If that ticket is open,
check it is labelled `upstream` and unclaimed. If it is closed, still
labelled `upstream`, and its fix is merged, go to step 3. Otherwise say why
and stop.

```bash
git remote get-url upstream || git remote add upstream https://github.com/joeycastillo/second-movement.git
git fetch origin && git fetch upstream
gh issue list -R swamp-dev/second-movement --state open --label upstream --limit 200 --json number,title,labels
```

Leave out tickets another session holds (`/start-ticket` § Ticket claims).
Read the body of every ticket that could plausibly rank first, and order
them by:

1. **Reach.** Code every build links (`movement.c`, `watch-library/`,
   `filesystem/`) or a face in upstream's default `movement_config.h` ranks
   above a face few people build. Break ties with the ticket's "In our
   build" line.
2. **Harm.** Wrong time or wrong alarm, lost or corrupted stored data, and
   memory errors rank above a wrong character on the display.
3. **How often.** Normal use ranks above a rare input.
4. `priority:low` goes last.

A ticket whose fix needs a change inside a submodule (`gossamer`, `littlefs`,
`tinyusb`, `utz`) is Blocked: say so and skip it.

Print the top five as a table with one line of reasoning each, name the pick,
and go on without waiting.

## 2. Fix it here and merge

Run steps 1–5 of the `/full-send` per-ticket loop for that ticket only
(this command's step 6 replaces its pause): `/start-ticket <N>`, implement,
`/finish-ticket`, hosted CI green, `/ship-ticket <P>`. Everything in
`docs/workflow.md` applies (failing test first, simulator check, reviewers,
`scripts/ci-local.sh`, merge authority).

Write the fix so it ports:

- Before designing it, run `git diff upstream/main origin/main -- <files>`
  for the files you expect to touch. Where the fork's copy has diverged,
  keep the fix clear of the fork's own lines.
- Keep new logic in its own `lib/<dir>`, and keep this fork's tickets,
  tests and process out of the code and comments that will go upstream.

If anything in `/full-send` § When to stop applies, stop there. If the PR
goes to the user to merge, wait; when they merge it in this session, go on
to step 3.

## 3. Port and build

Follow [`docs/workflow.md` § Upstream PRs](../../docs/workflow.md#upstream-prs)
steps 1–3: check the fix is merged here and that upstream has neither the fix
nor a PR for it, make `up/<N>-<slug>` off `upstream/main` with only the fix,
and build all 8 board × display combinations.

Host tests don't go upstream (it has no `test/`). If the fix has one, print
its compile line with `make -C test -n -B build/test_<name>`. Run that line
in the main checkout's `test/`, with every `../` source and `-I` path pointed
at the `up/` worktree and `-o` at a file outside both checkouts. Then run
that binary; it must exit 0.

## 4. Upstream issue, then upstream PR

Invoking this command is the user's request for at most one issue and one PR
on `joeycastillo/second-movement`, for this ticket. Look for an issue first,
in this ticket's comments (an earlier run may have opened one) and upstream:

```bash
gh issue list -R joeycastillo/second-movement --state all --search "<face or function> <symptom>"
```

If an open upstream issue already describes the bug, don't open another; the
PR says `Fixes #<that issue>`. If a closed one does, stop and tell the user;
leave the ticket and its label as they are.

Otherwise open the issue. Write it for a maintainer who has never seen this
fork: what goes wrong, steps to trigger it, observed and expected, where in
the code, and the root cause in two or three sentences. Claim only what was
checked, and say how (host test, simulator). Don't write this fork's
numbers as `#N` (upstream resolves them to its own issues); use full URLs.
Leave out local paths and process notes. End with a line saying it was
found and written up with Claude Code. Take the body from a file outside
any worktree:

```bash
gh issue create -R joeycastillo/second-movement --title "<summary>" --body-file <file>
```

Comment the issue's URL on our ticket straight away (`Upstream issue: <URL>`),
so a rerun after a failed push reuses it.

Then push the branch and open the PR with § Upstream PRs step 4's commands,
but not its body list: the body is `Fixes #<upstream issue>` on its own line, a short statement of the bug
and cause, what the change does, and how it was tested (builds, simulator
steps, the host test here with the full URL of this fork's merged PR). The
full bug report stays in the issue.

## 5. Record and close

Do § Upstream PRs step 5 (comment and label swap), with this comment:
`Sent upstream: issue <URL>, PR <URL>`. Then check the ticket is closed:

```bash
gh issue view <N> -R swamp-dev/second-movement --json state -q .state
```

The merge in step 2 normally closed it through `Closes #N`. If it prints
`OPEN` and the fix is merged, close it; the owner asked for this when
adding the command:
`gh issue close <N> -R swamp-dev/second-movement --reason completed`.

## 6. Status and stop

Report the ticket, the PR here, the upstream issue and PR, and the leftovers
for the user to remove (the `up/` worktree and branch stay until the
upstream PR is merged or closed). Then stop.

## Limits

- One ticket, at most one upstream issue and one upstream PR per
  invocation. Never loop over the queue.
- Nothing else on the upstream repo: no comments, no replies to review, no
  pushes to the `up/` branch after the PR is open, no closing. When the
  maintainers respond or upstream CI fails, tell the user and wait.
- If the fix can't go upstream (nothing left after dropping fork-only
  changes, or upstream already has it), follow § Upstream PRs for that
  case and open nothing upstream.
