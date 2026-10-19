# 0019 - ESP-NOW and LoRa as escapes from the BLE connection ceiling

- **Date:** 2026-10-18 (measurements dated 2026-09-10)
- **Status:** Accepted as a record of two evaluations. **Neither is chosen.**
  BLE at the fixture (`decisions/0012`) still stands.

## Context

NimBLE allows three connections. The BMS holds one and `decisions/0012` has
promised the rest to lighting, so two fixtures is the ceiling and a third
means raising `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` and paying RAM for it.

(Two simultaneous connections were unverified when this was written. They were
demonstrated on 2026-10-18 - the BMS and an SP107E held one each with the
scanner still running - so the ceiling is real arithmetic rather than a
guess.) Two
other radios could sidestep that entirely, and both were looked at properly
enough to be worth recording.

## What was measured: Wi-Fi and BLE do coexist, and the bill is heap

Measured on hardware 2026-09-10 with `WiFi.mode(WIFI_STA)` and
`esp_now_init()` running alongside the BMS connection and the sensor scan:

| | Advertisements | Loops / 2 s | Free heap |
|---|---|---|---|
| BLE only | 4.93/s | ~46,500 | ~95 KB |
| Wi-Fi up, silent | 3.29/s | ~43,800 | ~54 KB |
| ESP-NOW at 4 Hz | 3.52/s | ~41,600 | ~53 KB |

Nothing broke. ESP-NOW delivered 195 of 195 packets in each run, the BMS
stayed online, no integration held the loop and the UI kept flushing. **Wi-Fi
costs 41,392 bytes**, within a hundred bytes of the prediction.

The useful detail is which cost comes from what. Advertisement reception falls
the moment the radio comes up and transmitting barely adds to it - 3.29
against 3.52/s, inside the spread - so **there is no send rate to tune; the
loss is the price of the radio existing.** Transmitting costs about 5% of loop
rate on top, and 4 Hz is far more traffic than light commands would generate.
`decisions/0016` later found the same shape for BLE advertising, which is some
evidence the rule generalises on this chip.

The spike is on the `wifi-coexistence-spike` branch, not on main; `SPIKE_WIFI`
there reproduces both rows. **UNVERIFIED:** it never received an ESP-NOW
packet or drove a light, and Wi-Fi was in station mode joined to nothing. A
backhaul actually associated with an access point is a heavier load.

## ESP-NOW: the stronger alternative, and not chosen

An ESP32-based controller such as the Gledopto GL-C-017WL-D - DC5-24 V, four
channels, 800 pixels, ships with WLED - could be driven over ESP-NOW rather
than BLE. That fixes most of `0012`'s objections in one move: connectionless,
so the three-connection ceiling stops applying; CCMP/AES-128 with a PMK and
LMK, so there is real encryption, which `decisions/0018` says this class
otherwise lacks entirely; about 17 peers; millisecond latency instead of a BLE
connect.

Three routes, and they are not equivalent:

| | AP needed | Security | State readback |
|---|---|---|---|
| Stock WLED, ESP-NOW remote | no | MAC allowlist | **none** |
| Stock WLED, Wi-Fi JSON | yes | WPA2 | full |
| Reflashed, custom ESP-NOW | no | CCMP + HMAC | full |

**WLED's ESP-NOW support is a *remote* protocol** - one-way, button events
only - so the stock route reintroduces exactly the "confirm, do not assume"
violation that disqualified the analogue controllers in `0012`. Only
reflashing delivers the whole prize, at the cost of owning node firmware.

Espressif's own caveat is that MAC addresses are spoofable, so anything
security-critical wants an application-level HMAC in the payload; for van
lighting, encryption plus an allowlist is proportionate.

One hardware caution: the GL-C-017WL-D is rated **-20 to 45 C**, against the
SP107E's 60 C. A parked van in summer sun goes past 45, so placement matters.

Not chosen because it costs the 41 KB above and, in the only version worth
having, means maintaining firmware on every fixture. Worth revisiting if the
backhaul lands anyway - the 41 KB is paid once, so ESP-NOW would then be
effectively free - or if the connection ceiling actually binds.

## LoRa: set aside

An SX1262 second radio would escape the ceiling entirely and reach a trailer
or a remote tank. Set aside for three reasons: the chip is almost certainly
not fitted on this board (`HARDWARE.md`), the pinout puts `BUSY` and `DIO1`
behind the I2C expander, which would put an I2C round trip in front of every
SPI command, and there is no ecosystem of LoRa light controllers to buy -
SenseCAP's own LoRa sensors are LoRaWAN, which wants a gateway and a network
server in a van. Point-to-point means building both ends.

Worth revisiting only for something BLE genuinely cannot reach.

## Consequences

Lighting stays on BLE at the fixture. The ceiling stays at two controllers,
and raising it is a RAM decision rather than a redesign. The coexistence
numbers are the evidence for any future radio decision and are the reason this
record exists rather than a branch nobody will find.
