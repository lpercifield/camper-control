# Documentation standard

This project is a van that has to work at 3am in a field. The documentation
exists so that a change made six months from now does not undo something that
was learned the hard way. That is the whole goal - everything below serves it.

## The map

| Document | Answers | Changes when |
|---|---|---|
| `../README.md` | What is this, how do I build and flash it? | The build, the toolchain or the flashing procedure changes |
| `ARCHITECTURE.md` | How is the system put together, and what may I not break? | A core contract changes - entities, registry, integrations, the loop |
| `HARDWARE.md` | What is this board, which pin does what, which port do I flash? | A hardware fact is confirmed, corrected or newly discovered |
| `ADDING_AN_INTEGRATION.md` | How do I add an accessory? | The integration contract changes |
| `TROUBLESHOOTING.md` | It is broken. Why? | A failure is diagnosed - **write it down the day you fix it** |
| `ROADMAP.md` | What is wrong, and what is next? | Something is finished, or a new problem is found |
| `PORTING_NOTES.md` | Why does the vendored code look like that? | A vendored library is patched |
| `decisions/` | Why was it done this way rather than the obvious way? | A choice is made that a reasonable person would question |

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
