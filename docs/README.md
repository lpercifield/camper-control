# Documentation

| Document | Answers |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | How is the system put together, and what may I not break? |
| [HARDWARE.md](HARDWARE.md) | What is this board, which pin does what, which port do I flash? |
| [ICD.md](ICD.md) | **Normative.** What goes over the air between the van and a phone? |
| [ADDING_AN_INTEGRATION.md](ADDING_AN_INTEGRATION.md) | How do I add an accessory? |
| [TROUBLESHOOTING.md](TROUBLESHOOTING.md) | It is broken. Why? |
| [ROADMAP.md](ROADMAP.md) | What is wrong, and what is next? |
| [PORTING_NOTES.md](PORTING_NOTES.md) | Why does the vendored code look like that? |
| [decisions/](decisions/) | Why was it done this way rather than the obvious way? |

**How to work on this project** - where a new fact belongs, the review flow,
conventions, the decision-record process - is in
[CONTRIBUTING.md](../CONTRIBUTING.md) at the repository root.

[ICD.md](ICD.md) is the only document here marked normative, because it is a
contract with something outside this repository - a phone in somebody's pocket.
A wire-format change recorded anywhere else has drifted. `tools/check_icd.py`
enforces the machine-checkable half and the pre-commit hook runs it, including
when `SKIP_DOC_CHECK=1` is set.

The phone app has its own notes in [../android/README.md](../android/README.md),
including the two traps that cost time there.

`../README.md` is the front door: what this is, how to build it and how to
flash it.
