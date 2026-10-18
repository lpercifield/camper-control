#pragma once

// -----------------------------------------------------------------------------
// The interface control document, in code.
//
// `docs/ICD.md` is the normative text; this file is the half of it the firmware
// compiles, and `android/src/com/campercontrol/monitor/Icd.java` is the half
// the app compiles. **They are checked against each other by
// `tools/check_icd.py`, which the pre-commit hook runs.** A change here that is
// not mirrored there fails the commit.
//
// ---- What the version means -------------------------------------------------
//
// `CC_ICD_VERSION` is a **major** version and bumps **only on a breaking
// change**: an object id reused for a different meaning, a scale factor
// changed, a field's sign convention changed, or anything else that makes an
// older reader decode a wrong number rather than no number.
//
// **Adding a reading does not bump it.** BTHome is self-describing and object
// ids ascend, so an older reader decodes everything up to the first id it does
// not know and reports `complete = false` - an honest short frame, not a wrong
// one. That is the compatibility mechanism, and it only holds while new ids are
// strictly greater than every id already in the frame. See `docs/ICD.md`.
//
// So: a reader that sees a version it does not know must **stop trusting the
// readings**, because by definition something it cannot see has changed
// meaning.
//
// ---- Where the version travels ----------------------------------------------
//
// In the advertised local name, which is `CC` followed by the version - `CC1`
// today. Not in the payload, because the advertisement is already at its
// 31-byte limit and a payload object would cost two of the three bytes left.
// The name is derived from the constant below rather than written out, so the
// two cannot drift: bump the version and the name follows.
//
// **The name is therefore protocol, not decoration.** That is why it lives here
// and not in `config.h` with the tunable knobs.
// -----------------------------------------------------------------------------

#define CC_ICD_VERSION 1

#define CC_ICD_STRINGIFY2(x) #x
#define CC_ICD_STRINGIFY(x) CC_ICD_STRINGIFY2(x)

// "CC1". Three characters, which is 5 bytes of advertisement once the AD
// header is counted - see docs/ICD.md for the byte budget this has to fit.
#define CC_ICD_NAME "CC" CC_ICD_STRINGIFY(CC_ICD_VERSION)
