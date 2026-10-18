#!/usr/bin/env python3
"""Check that the firmware and the Android app still speak the same protocol.

`docs/ICD.md` is the contract; this is the part of it a machine can enforce.
The pre-commit hook runs it, so the two ends cannot drift apart in a commit.

Four things are checked, and the second is the one that matters most:

1. **The ICD version matches** between `include/icd.h` and `Icd.java`.
2. **Everything the firmware encodes, the app can decode.** This is the real
   drift: a reading is added to the broadcast and the app silently never shows
   it. The check is directional on purpose - the app's decoder may know ids the
   firmware never sends, because it also reads other vendors' sensors.
3. **The service UUIDs and company id match**, since a wrong constant means
   the two simply never meet.
4. **The advertisement still fits 31 bytes**, recomputed from the ids the
   encoder actually emits rather than from a number written down once.

Run it directly, or let the commit hook do it:

    python3 tools/check_icd.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "android/src/com/campercontrol/monitor"

# Object id -> payload width in bytes, from the BTHome v2 table. Used only for
# the budget arithmetic in check 4.
WIDTH = {0x00: 1, 0x01: 1, 0x02: 2, 0x03: 2, 0x0C: 2, 0x2E: 1, 0x45: 2,
         0x5C: 4, 0x5D: 2}

problems = []


def read(rel):
    p = ROOT / rel
    if not p.exists():
        problems.append(f"missing file: {rel}")
        return ""
    return p.read_text(encoding="utf-8")


def ids_from(text, pattern):
    return {int(m, 16) for m in re.findall(pattern, text)}


def main():
    icd_h = read("include/icd.h")
    icd_java = read(APP.relative_to(ROOT) / "Icd.java")
    enc = read("src/core/bthome_encode.cpp")
    dec_java = read(APP.relative_to(ROOT) / "BtHome.java")
    dec_c = read("src/core/bthome.cpp")
    bthome_h = read("src/core/bthome.h")
    sb_h = read("src/core/switchbot.h")
    sb_java = read(APP.relative_to(ROOT) / "SwitchBot.java")

    # ---- 1. version -------------------------------------------------------
    m = re.search(r"#define\s+CC_ICD_VERSION\s+(\d+)", icd_h)
    n = re.search(r"static\s+final\s+int\s+VERSION\s*=\s*(\d+)", icd_java)
    fw_ver = int(m.group(1)) if m else None
    app_ver = int(n.group(1)) if n else None
    if fw_ver is None:
        problems.append("include/icd.h: no CC_ICD_VERSION")
    if app_ver is None:
        problems.append("Icd.java: no VERSION")
    if fw_ver is not None and app_ver is not None and fw_ver != app_ver:
        problems.append(
            f"ICD version mismatch: firmware {fw_ver}, app {app_ver}. "
            "Bump both, or neither - see docs/ICD.md.")

    # ---- 2. everything encoded can be decoded by the app ------------------
    # What the frame actually contains, not what the builder is capable of:
    # the add*() calls ble_broadcast.cpp:publish() makes, resolved to object
    # ids through the header's own comments. Measuring the real frame keeps
    # the budget in check 4 honest - an earlier version counted every method
    # the builder offers and reported 31 bytes by coincidence.
    header = read("src/core/bthome_encode.h")
    method_id = {m: int(h, 16) for m, h in re.findall(
        r"bool\s+add(\w+)\([^)]*\);\s*//\s*0x([0-9A-Fa-f]{2})", header)}
    if not method_id:
        problems.append(
            "bthome_encode.h: could not map add*() methods to object ids. "
            "Each declaration needs a trailing `// 0xNN` comment.")

    bcast = read("src/integrations/ble_broadcast.cpp")
    publish = bcast.split("uint8_t BleBroadcast::publish()", 1)
    called = set()
    if len(publish) == 2:
        called = set(re.findall(r"builder_\.add(\w+)\(", publish[1]))
    else:
        problems.append("ble_broadcast.cpp: could not find publish()")

    unmapped = sorted(called - set(method_id))
    if unmapped:
        problems.append(
            "publish() calls add" + ", add".join(unmapped)
            + ", which bthome_encode.h does not map to an object id.")

    encoded = {method_id[m] for m in called if m in method_id}
    # The app's constants, e.g. "OBJ_VOLTAGE     = 0x0C;"
    app_decodes = ids_from(dec_java, r"OBJ_\w+\s*=\s*0x([0-9A-Fa-f]{2})\s*;")
    # The firmware decoder, for information - it legitimately knows more.
    fw_decodes = ids_from(dec_c, r"kObj\w+\s*=\s*0x([0-9A-Fa-f]{2})\s*,")

    if not encoded and not problems:
        problems.append("publish() appears to broadcast nothing")
    if not app_decodes:
        problems.append("could not find any object ids in BtHome.java")

    missing = sorted(encoded - app_decodes)
    if missing:
        problems.append(
            "the firmware broadcasts object ids the app cannot decode: "
            + ", ".join(f"0x{i:02X}" for i in missing)
            + ". Add them to BtHome.java, or stop sending them.")

    unknown_width = sorted(encoded - set(WIDTH))  # noqa: E501
    if unknown_width:
        problems.append(
            "tools/check_icd.py has no width for "
            + ", ".join(f"0x{i:02X}" for i in unknown_width)
            + "; add it to WIDTH so the byte budget stays honest.")

    # ---- 3. the constants that have to agree ------------------------------
    def const(text, pattern, label, store):
        mm = re.search(pattern, text)
        if not mm:
            problems.append(f"{label}: constant not found")
            return
        store.append(int(mm.group(1), 16))

    pairs = [
        ("BTHome service UUID",
         (bthome_h, r"kBtHomeServiceUuid\s*=\s*0x([0-9A-Fa-f]+)"),
         (dec_java, r'SERVICE_UUID\s*=\s*"0000([0-9a-fA-F]{4})-')),
        ("SwitchBot company id",
         (sb_h, r"kSwitchBotCompanyId\s*=\s*0x([0-9A-Fa-f]+)"),
         (sb_java, r"COMPANY_ID\s*=\s*0x([0-9A-Fa-f]+)")),
        ("SwitchBot service UUID",
         (sb_h, r"kSwitchBotServiceUuid\s*=\s*0x([0-9A-Fa-f]+)"),
         (sb_java, r'SERVICE_UUID\s*=\s*"0000([0-9a-fA-F]{4})-')),
    ]
    for label, (t1, p1), (t2, p2) in pairs:
        a, b = [], []
        const(t1, p1, f"{label} (firmware)", a)
        const(t2, p2, f"{label} (app)", b)
        if a and b and a[0] != b[0]:
            problems.append(
                f"{label} mismatch: firmware 0x{a[0]:04X}, app 0x{b[0]:04X}")

    # ---- 4. the advertisement still fits ----------------------------------
    name_m = re.search(r'#define\s+CC_ICD_NAME\s+"(\w+)"', icd_h)
    name_len = len(name_m.group(1)) if name_m else None
    if name_len is None and fw_ver is not None:
        # Built by token pasting, so reconstruct it the way the compiler does.
        pre = re.search(r'#define\s+CC_ICD_NAME\s+"(\w*)"', icd_h)
        name_len = (len(pre.group(1)) if pre else 2) + len(str(fw_ver))

    payload = total = 0
    if name_len is not None and encoded and not unknown_width:
        payload = 1 + sum(1 + WIDTH[i] for i in encoded)  # info byte + objects
        service_ad = 4 + payload       # len, type 0x16, 2 UUID bytes
        name_ad = 2 + name_len         # len, type 0x09
        total = service_ad + name_ad   # no flags: non-connectable
        if total > 31:
            problems.append(
                f"the advertisement no longer fits: {total} bytes "
                f"(payload {payload}, name {name_len} chars). The legacy limit "
                "is 31. Drop a reading or shorten the name - docs/ICD.md.")

    if problems:
        print("ICD check failed:\n", file=sys.stderr)
        for p in problems:
            print(f"  - {p}\n", file=sys.stderr)
        print("  docs/ICD.md explains the contract.\n", file=sys.stderr)
        return 1

    ids = ", ".join(f"0x{i:02X}" for i in sorted(encoded))
    print(f"ICD v{fw_ver} ok. Frame: {len(encoded)} objects ({ids}), "
          f"{payload} payload bytes, {total}/31 on the wire.")
    print("  every one of them is decodable by the app.")
    print(f"  firmware decodes {len(fw_decodes)} ids, app decodes "
          f"{len(app_decodes)} (the firmware also reads other sensors).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
