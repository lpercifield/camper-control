package com.campercontrol.monitor;

/**
 * The interface control document, app side.
 *
 * `docs/ICD.md` is the normative text and `include/icd.h` is the firmware's
 * half. <strong>The two are checked against each other by
 * {@code tools/check_icd.py}, which the pre-commit hook runs</strong>, so a
 * change on one side that is not mirrored here fails the commit.
 *
 * <p>{@link #VERSION} is a <strong>major</strong> version and bumps only on a
 * breaking change - an object id reused, a scale factor altered, a sign
 * convention flipped. Adding a reading does not bump it: BTHome object ids
 * ascend, so an older reader decodes up to the first id it does not know and
 * reports {@code complete = false}, which is a short frame rather than a wrong
 * one.
 *
 * <p>The consequence, and the reason this class exists: a version this app
 * does not know means something it cannot see has <em>changed meaning</em>, so
 * the readings must not be displayed. A newer field it simply cannot read is a
 * different and much milder case, already handled by {@code complete}.
 */
final class Icd {

    /** Must equal CC_ICD_VERSION in include/icd.h. */
    static final int VERSION = 1;

    /** The advertised local name is this followed by the version: "CC1". */
    static final String NAME_PREFIX = "CC";

    private Icd() {}

    /**
     * The protocol version a controller is advertising, from its local name.
     *
     * @return the version, or -1 when the name is missing or not ours. That is
     *         not treated as a failure: a scanner can deliver an advertisement
     *         without the name, and the frame is still structurally
     *         self-describing. Unknown means "fall back to what the payload
     *         says", not "refuse".
     */
    static int versionFromName(String name) {
        if (name == null) return -1;
        String n = name.trim();
        if (!n.startsWith(NAME_PREFIX)) return -1;
        String digits = n.substring(NAME_PREFIX.length());
        if (digits.isEmpty()) return -1;
        for (int i = 0; i < digits.length(); i++) {
            if (digits.charAt(i) < '0' || digits.charAt(i) > '9') return -1;
        }
        try {
            return Integer.parseInt(digits);
        } catch (NumberFormatException e) {
            return -1;
        }
    }

    /** True when a version we read off the wire is one we can safely decode. */
    static boolean compatible(int wireVersion) {
        return wireVersion == -1 || wireVersion == VERSION;
    }
}
