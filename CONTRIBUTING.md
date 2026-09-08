# Working on Camper Control

This is a van that has to work at 3am in a field. The process exists so that a
change made six months from now does not undo something learned the hard way -
nothing here is ceremony for its own sake, and every rule below is here because
something went wrong without it.

## How a change lands

**Trivial** - a typo, a comment, a documentation correction:

```
edit -> commit to main -> push
```

CI verifies the build independently.

**Everything else** - firmware, build configuration, anything touching
behaviour:

```
git checkout -b some-branch
  test             pio test -e native
  build            pio run
  flash            pio run -t upload --upload-port /dev/cu.usbserial-1110
  verify           read the log, confirm the thing actually works
  review           read the diff yourself before anyone else does
  push             git push -u origin some-branch
  PR               state what you verified on hardware, not just that it builds
  merge            once CI is green
```

The tests take two seconds and need no board, so they go first. They only cover
`src/core/` - see `docs/ARCHITECTURE.md`. Passing them is not verification, and
never substitutes for the log.

The branch is not bureaucracy - it is a cheap way back. The NimBLE port rewrote
the most fragile file in the project; having a branch meant that was a
reversible decision rather than a brave one.

### "Verified" means you looked at the log

The single most expensive habit in this project's history was reasoning about a
diff instead of instrumenting the board. A blank screen was attributed to two
different wrong causes over two days before a heartbeat counter found the real
one in minutes.

So a PR does not say "should work". It says what was observed:

> Connects, reaches `online` in 10 s, reads pack 9.60-14.60 V off the battery,
> no loop stalls, heap 97 KB.

If you did not flash it, say that too. An honest "compiles, not yet on
hardware" is useful; an implied verification is not.

## Continuous integration

`.github/workflows/build.yml` runs `pio test -e native` and then `pio run` on
every push and pull request. It catches the "works on my machine" class of
problem, which until 2026-09-08 was entirely uncovered - every build had been
verified on exactly one laptop.

Tests run first because they are 15 s against the firmware build's several
minutes; there is no reason to wait for a toolchain download to be told the
core is broken.

It does not flash anything. Hardware verification is still a person with the
board in front of them.

## Where does a new fact go?

The single most useful rule here. Ask, in order:

1. **Is it a fact about the physical board?** -> `HARDWARE.md`. Pin numbers,
   USB identifiers, baud rates, panel quirks.
2. **Is it a rule that code must obey?** -> `ARCHITECTURE.md` if it applies to
   the whole system, `ADDING_AN_INTEGRATION.md` if it only binds integrations.
3. **Did something break, and would the next person be stuck too?** ->
   `TROUBLESHOOTING.md`, as a symptom, not as a cause. People search for what
   they can see, not for what is actually wrong.
4. **Was a real alternative rejected?** -> a decision record in `decisions/`.
5. **Is it a patch to somebody else's code?** -> `PORTING_NOTES.md`, numbered,
   with the reason. Unexplained patches get reverted by the next upgrade.
6. **Is it work not yet done?** -> `ROADMAP.md`. Not a code comment, and not a
   `TODO` that nobody greps for.

If a fact fits two places, put it in one and link to it. Duplicated
documentation does not stay duplicated - it stays in one place and rots in the
other.

## Agent context

`CLAUDE.md` at the repository root is the operating manual for Claude Code:
commands, the traps that have already cost time, the debugging protocol, and the
git conventions. It is loaded automatically at the start of a session.

It is not a substitute for these documents and holds no facts of its own - where
the two overlap, `CLAUDE.md` points here. Keep it short; a long one gets skimmed.

## Conventions

- **Wrap at 80 columns.** These files get read in a terminal beside the code.
- **Say why, not what.** The code already says what it does. Documentation earns
  its place by recording the reasoning, the alternative that was rejected, and
  the thing that bit us.
- **Reference code as `path:symbol`**, e.g. `src/bsp/display.cpp:flushCb`.
  Line numbers go stale within a week; symbol names survive.
- **Mark unverified claims.** Anything believed but not observed on hardware is
  written as **UNVERIFIED** in bold. This project shipped a 2 MB firmware that
  had never run once; the habit is worth keeping.
- **Absolute dates**, `2026-09-06`, never "last week".
- **No screenshots as the only record of a value.** Put the number in text.

## Decision records

A decision record is for a choice where a reasonable person would have done
something else. Not every choice needs one - only the ones where the obvious
path was rejected for a reason that is not visible in the code.

They are immutable. When a decision is reversed, write a new record and mark the
old one `Superseded by NNNN`. Rewriting history is how a codebase ends up with
rules nobody can justify.

Copy `decisions/TEMPLATE.md`, take the next number, keep it to one page.

## When a change is done

A change is not finished until:

- Any hardware fact it revealed is in `HARDWARE.md`.
- Any failure it fixed is in `TROUBLESHOOTING.md` under the symptom you saw.
- Any patch to vendored code is numbered in `PORTING_NOTES.md`.
- Any contract it changed is in `ARCHITECTURE.md`.
- `ROADMAP.md` reflects what is now true.

The test: could someone who has never seen this project reproduce your reasoning
from the documents alone? If not, the change is not done.

### Enforced at the commit

`.githooks/pre-commit` refuses a commit that touches `src/`, `include/`, `lib/`,
`tools/` or `platformio.ini` without touching any document. It prints the
routing rule above and stops.

It is a block rather than a warning on purpose. A warning is ignored; a block
forces a decision. Plenty of commits genuinely need no documentation - a
rename, a formatting pass - and those say so out loud:

```
SKIP_DOC_CHECK=1 git commit ...
```

Git hooks are not cloned. **After a fresh clone, run this once** or the gate
silently does nothing:

```
git config core.hooksPath .githooks
```

The hook cannot tell whether the roadmap is *true*, only whether you looked. It
catches the failure that actually happened here - `ROADMAP.md` claiming a
blocker was open a day after it was closed - and nothing more.
