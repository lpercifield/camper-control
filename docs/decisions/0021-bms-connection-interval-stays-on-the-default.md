# 0021 - The BMS connection interval stays on NimBLE's default

- **Date:** 2026-10-18 (measured 2026-09-10)
- **Status:** Accepted.

## Context

The BMS link runs on NimBLE's default 50 ms connection interval - nothing ever
called `setConnectionParams` - while `bms_jbd` polls it twice a second
(`kRefreshMs` is 500). The radio therefore wakes roughly fifteen times more
often than the data needs, and that airtime is shared with the scan that feeds
the temperature sensors.

The obvious move was to ask for a longer interval and give the scanner the
airtime back.

## Decision

**Leave it on the default.** The tuning is implemented and kept behind
`CONN_PARAM_TUNING` in `BleSerialClient.cpp`, defaulting off.

Asking for 200 ms works: the BMS accepts, and the negotiated values come back
as 200 ms with a 4000 ms supervision timeout. But measured on hardware it took
advertisement reception **down**, from a mean 4.93/s across two runs to 4.03/s
across two. Loop rate and BMS function were unaffected either way.

The prediction was that waking the radio four times less often would free
airtime for scanning. It did not, and **the mechanism is not understood.**
That is the whole reason this record exists: the change looks obviously
correct, measured worse, and nobody should re-derive it from first principles
and ship it.

## Alternatives rejected

**Ship the longer interval anyway, on the theory that the measurement was
noisy.** Two runs each way is thin, but the direction was consistent and the
predicted benefit was absent. Shipping a change that measured worse because
the theory is appealing is the failure this project's debugging protocol
exists to prevent.

## Consequences

The code stays, off, so re-running the experiment costs a flag rather than a
reimplementation.

Worth re-running when a second connection actually contends for the radio -
the lighting plan in `decisions/0012` - because that is the case it was meant
for and the only one where the trade can be judged. `decisions/0016` has since
added a second radio user in the form of advertising, which is a weaker
version of the same contention and did not change reception measurably.
