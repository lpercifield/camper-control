# Roadmap

Last reviewed 2026-10-18.

## Where it actually is

Runs on hardware. Boots, drives the panel, reads touch, and carries a Settings
overlay whose values survive a power cycle. Flash 50.5%, static RAM 39.5%,
**~74 KB free heap**, LVGL pool 73% used. Main loop ~21k iterations/sec and
LVGL 84 flushes/sec, re-measured on hardware 2026-10-18. No integration holds
the loop.

The loop lost about 9% of its spare headroom on 2026-10-18 to the phone
broadcast (`decisions/0016`), and the flush rate did not move - 84/s before and
after, so nothing a person can see changed. Flash and static RAM moved by
3.5 KB and 2.6 KB for the same reason.

**A phone can now read the pack without joining anything.** The van advertises
state of charge, pack voltage and battery temperature as a BTHome v2 frame, and
is never connectable, so it spends none of the three BLE connection slots. This
replaced the Wi-Fi SoftAP this file had planned for the job: a phone holds one
Wi-Fi association and wireless CarPlay / Android Auto already owns it. See
**Design gaps 2**.

Static RAM and free heap both moved on 2026-09-17: `LV_MEM_SIZE` went 48 KB ->
64 KB (`decisions/0013`) after the LVGL pool ran out and boot-looped the board.
That is 16 KB of the Wi-Fi headroom this file was counting on - see **Design
gaps 2**.

The BMS connects on the remembered address and reaches `online`, so the live
pack display works. Wireless temperature and humidity works too: a SwitchBot
W3400010 is read on the Climate page, confirmed against the vendor app on
2026-09-16, and any BTHome v2 broadcaster is read by the same path. The Climate
page is tiles with a 24-hour per-sensor graph behind them, temperatures follow a
global °C/°F setting, and there is a hand-set clock.

**The device cannot store anything durably.** Both routes were closed on
2026-09-17, by probe rather than by argument: the microSD slot belongs to the
RP2040, and the internal flash filesystem panics the chip whenever it is touched
while the RGB panel refreshes. Daily highs and lows are kept in RAM and reset on
every power cycle. See `decisions/0014` and **Design gaps 3**.

**There are no blockers left.** What remains is one bug worth fixing - the
scanner starves other listeners while the BMS is failing to connect - and then
the difference between a working device and one worth living with.

One thing that is not a blocker but shapes everything after it: **every update
so far has needed a cable**, and the framework silently disables the
bootloader's rollback protection, so shipping a bad image over the air would
strand a van. Gap 6.

## Blockers

None. The device boots, drives the panel, reads touch, connects to the battery
and remembers its settings.

## Design gaps

**1. `Domain::System` has no page.** The enum and `domainName()` know about it;
`kNavDomains` does not. Anything registered there - uptime, heap, link health,
exactly the diagnostics that would have shortened this week - is silently
invisible. A trap for the next integration author.

**2. The phone path is BLE, and Wi-Fi backhaul is a separate question.**
Revised 2026-10-18, after several weeks of use closed a design this file had
held open from the start.

**A SoftAP cannot be the phone path.** A phone holds exactly one Wi-Fi
association, and wireless CarPlay and Android Auto *are* that association - the
phone joins the head unit's access point. Joining the van's AP drops the car's
screen, and the ESP32-S3 is 2.4 GHz only, so there is no second band to escape
into. The choice is not "two networks", it is the van's readings or the car's
screen, every single time, and that is not a trade anyone makes twice. This
supersedes the "one access point serving both" plan that gap 6 still rests on.

BLE carries no such exclusivity: a phone holds a BLE connection and a Wi-Fi
association at once, which is how it already talks to a watch while CarPlay
runs. And the radio is already up and paid for - **peripheral role is compiled
in today**, since NimBLE's `nimconfig.h` defaults
`CONFIG_BT_NIMBLE_ROLE_PERIPHERAL` to 1, so a GATT server adds no new stack
where Wi-Fi adds a measured 41 KB against ~75 KB free.

**Split read from write. They have almost nothing in common.**

*Reading* - glancing at state of charge, tank level and cabin temperature from
bed or from outside - needs no connection at all. The van broadcasts its own
entities in an advertisement. That costs no connection slot, no pairing, no
inbound attack surface and no connect latency; it serves any number of phones
at once; and it never touches the scanner-starvation bug under Smaller things.
The codec is already half written: `core/bthome.cpp` decodes BTHome v2, and
encoding the van's own readings is that table read backwards - which also means
any existing BTHome-aware tool reads the van with **no app written at all**, the
cheapest possible spike. Budget is ~24 usable bytes of legacy advertisement
payload, so more than about five readings wants the payload rotating across
successive advertisements: the same merge-rather-than-replace shape
`env_sensors.cpp:onAdvertisement` already implements, running on the sending
side.

*Writing* - switches, setpoints, the clock - needs a connection, and every cost
lands here:

- **The connection ceiling bites.** `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` is 3 by
  default, the BMS holds one, and `decisions/0012` has already promised two to
  lighting. A phone is the fourth. Keep the phone's connection **transient** -
  connect, write, confirm, disconnect - and the ceiling never has to move:
  control is bursty, and nothing needs the link held once reading is a
  broadcast.
- **Do not use BLE bonding.** NimBLE's bond store writes NVS from the host task
  on core 0. That breaks this project's own rule that every NVS write happens on
  the main loop through `Settings`, and it breaks it while the panel refreshes,
  which is precisely the `decisions/0014` hazard the rule exists to prevent. An
  application-layer HMAC challenge-response keeps the rule intact instead:
  `Settings` writes the shared key once, on the main loop, like any other
  setting. The trade is an unencrypted link, which is fine for a state of
  charge - it is write *authorisation* and replay that matter here.
- **The panel is the pairing advantage.** 480x480 of screen means showing a
  six-digit code for the crew to type into the app, which is a better trust
  story than a WPA2 password on a sticker.

**The custom app is the real cost, and it is not a firmware cost.** The firmware
end is identical for all three tiers:

| | Install | iPhone | Reads advertisements |
|---|---|---|---|
| nRF Connect / LightBlue | app store | yes | yes |
| Web Bluetooth page | none | **no** | **no** |
| Native or Flutter app | sideload | **paid** | yes |

A generic BLE app proves the firmware end today and costs nothing to write. A
Web Bluetooth page is one codebase and no install, but Apple has never shipped
Web Bluetooth in Safari - it needs a third-party browser - and Web Bluetooth
cannot read advertisements in practice, so it sees only GATT, which forces a
connection and a slot and discards the read path above. A native app gets
everything, including background scanning on Android, but **an iPhone needs an
Apple Developer account at $99/yr or the app stops working every seven days**,
which on a one-van project is the largest recurring line item in it.

### Decided 2026-10-18: Android first, iOS not foreclosed, broadcast first

The first increment is the **broadcast-only read path**, and the app target is
**Android first without making any choice that eliminates iOS later.** That
second clause is not a platitude - it rules out specific options that would
otherwise be the cheapest ones, so the constraints are written down here rather
than rediscovered:

- **The cheapest Android route is the one that forecloses iOS.** A hosted Web
  Bluetooth page is free, installs nothing and lasts forever - and Safari has
  never shipped Web Bluetooth, so an iOS build would mean starting over. It is
  also already disqualified for this increment, because Web Bluetooth cannot
  read advertisements at all. **Flutter with `flutter_blue_plus`** is the
  recommendation: it reads advertisements on both platforms, so the iOS build
  stays one signing certificate away rather than one rewrite away.
- **Identify the van by service UUID, never by MAC address.** iOS never exposes
  a peripheral's address - CoreBluetooth hands out a rotating per-install UUID
  instead. Keying the app to "the van's MAC" works on Android and cannot be
  made to work on iOS. Advertise a fixed service UUID and filter on that.
- **Put the payload in service data, not manufacturer data.** iOS background
  scanning requires a service-UUID filter and does not deliver manufacturer
  data reliably behind one. BTHome v2 already lives in service data under
  `0xFCD2`, so the format that needs no app written is also the format that
  keeps iOS open - the two constraints point the same way, which is lucky
  rather than clever.
- **Stay inside legacy 31-byte advertising.** Extended advertising is not a
  universal floor across phones and no BTHome-aware tool expects it. This is
  what forces the payload rotation described above.
- **Do not design around advertisement rate.** iOS coalesces duplicates in the
  background and cannot be asked not to, so "updates every second" is an
  Android-only promise. Treat the broadcast as a current-value beacon, not a
  stream.
- **BTHome's own AES-CCM encryption is the answer if the broadcast should not
  be readable by neighbours**, and its symmetric key is written once by
  `Settings` on the main loop, consistent with the NVS rule above. Leave it
  **off** for the first spike: an encrypted payload is invisible to the generic
  tools that make the spike free.

Control over a connection is deliberately **not** in this increment. Everything
under *Writing* above still applies when it is, and one addition: it must be a
foreground, explicit action, because iOS gives no reliable background write.

### Built 2026-10-18: the read path is on hardware

The broadcast increment is **done and verified on hardware** - see the Done
section and `decisions/0016`. What this section predicted, and what actually
happened:

- **Advertising costs ~9% of the loop's spare headroom and the UI does not
  notice.** Broadcast on against off, same build: 20,849 against 22,837
  loops/sec median, and **84.0 flushes/sec in both**. Nothing a person can see
  changed.
- **The cost is the radio, not the refresh.** Refreshing the payload every 10 s
  instead of every 2 s left the loop rate unchanged (20,856 against 20,849), so
  `CFG_BLE_BROADCAST_REFRESH_MS` is a freshness knob and nothing else. The same
  shape as this file's Wi-Fi finding, where the loss was the price of the radio
  existing rather than a send rate to tune.
- **The prediction above was right about reception and for the wrong reason.**
  It guessed advertising would cost reception far less than Wi-Fi did; measured,
  the difference was 4.21 to 4.05 adv/s, and then a third run with the broadcast
  *on* came in at 4.35/s - above the broadcast-off arm. So there is no effect
  established at all, not a small one. One run per arm cannot separate 4% from
  this signal's spread.
- **Payload rotation was never needed, and the budget is now spent.** The
  first frame was 11 bytes; adding signed power and current took it to 19,
  which is 23 bytes of service-data AD and leaves exactly 8 for the name - two
  of header and six of name, hence `Camper`. Measured on hardware as `ad=31
  payload=19` with no refusals. **Anything added from here displaces
  something.** The "~24 usable bytes, rotate past five readings" arithmetic
  above was the right ceiling; this increment reached it.
- **The iOS address constraint is now observed, not asserted.** macOS reported
  the board as `715481C8-CF97-BE31-0B54-F5DC3C4A33ED` rather than its MAC,
  which is exactly the CoreBluetooth behaviour the bullet above predicts.

Two things did not go to plan, both recorded in `decisions/0016`:

- ~~Pack current and power are not in the frame, and cannot be.~~ **Wrong,
  and fixed on 2026-10-18 - see `decisions/0017`.** BTHome v2 does define
  signed current (`0x5D`, sint16 x0.001 A) and signed power (`0x5C`, sint32
  x0.01 W); the claim came from a partial read of the object table. Power is
  now broadcast always and current whenever it fits, and because signed
  current holds only +-32.767 A against a pack rated past 120 A, a reader
  recovers current from power / voltage when the field drops out. The
  advertisement is now **exactly full at 31 bytes**.
- **The app is dependency-free Java, not Flutter.** The recommendation above is
  unchanged and still the plan; there is no Flutter SDK on the build machine,
  and 430 lines of Java proved the firmware end today. What preserves the iOS
  option is the protocol, not the app. See `android/README.md`.

**UNVERIFIED: the APK has never been installed or run.** No Android device has
been attached. Its decoder was checked on the JVM against a frame captured
off-air, and its manifest was checked with `aapt2 dump badging`, but the UI, the
permission flow and the scan itself have not run on a phone. That is the next
thing to do, and it needs a phone rather than a laptop.

**Wi-Fi backhaul survives this, just not as the phone path.** MQTT into Home
Assistant is still the obvious shape for a van that sits on a driveway, and gap
6 still wants a radio. `core/settings.*` and the Settings overlay exist, and the
Settings page carries a placeholder row, but nothing scans, joins or stores a
network. Wi-Fi costs ~41 KB against ~75 KB free, so it fits with ~35 KB to
spare and coexistence was never the problem - but the credential screens are two
more screens on a pool with roughly two screens of slack, and **what the
backhaul talks to** is still undecided. Note NVS is not encrypted, so a stored
Wi-Fi password is readable by anyone who can dump the flash. See
`decisions/0009`.

**3. Nothing can be stored on this device, and it constrains more than
logging.** Two independent walls, both measured:

- **The microSD slot is the RP2040's.** GPIO 47 floats and the ESP32-S3's pin
  budget has no room for an SD bus; `EXP_SD_CS` is a carried-over community
  constant that leads nowhere. `HARDWARE.md` has the probe.
- **The flash filesystem panics the chip.** The RGB panel's IRAM-safe VSYNC
  handler calls `gdma_hal_append`, which lives in flash, during the
  cache-disabled window of any flash access. Reliable, not a race. The Arduino
  prebuilt libraries ship `CONFIG_LCD_RGB_RESTART_IN_VSYNC=y` and
  `CONFIG_LCD_RGB_ISR_IRAM_SAFE=y` but not `CONFIG_GDMA_CTRL_FUNC_IN_IRAM`,
  which is the one line that would fix it and the one line we cannot set.
  `decisions/0014`.

`core/datalog.*` and `bsp/storage.*` are written, tested and switched off at
`CFG_LOG_TO_FLASH`. They are waiting on a medium, not on code.

**The RP2040 link is now the highest-value unbuilt thing in the project.** It
owns the SD slot, the buzzer and the Grove ports; it is a separate chip with no
cache to lose; and it is reachable on `RP2040_UART_TX/RX` with PacketSerial and
COBS. One protocol unlocks durable logging, the audible alarm this device still
does not have, and the Grove sensors.

And a standing worry it raises: **`Settings` writes NVS while the panel runs**,
which is the same hazard with a one-to-two millisecond window. It has never
panicked in this project's history. That is luck with good odds rather than
safety, and it is a reason not to add more flash writes.

**4. The arduino-cli harness is dead weight.** Linux-only, already drifted, and
looks maintained. Delete it or put it in CI. See `decisions/0006`.

**5. Lighting has a page and no integration.** The plan is settled and the
hardware is not bought: each fixture gets its own BLE LED controller, powered
from the 12 V run that already feeds it, so an existing run becomes a
controllable light **without pulling any new control wiring** - which is the
entire point, and the reason the obvious answer of PWM from the RP2040 was
rejected. Reasoning and the rejected alternatives are in `decisions/0012`.

Reference part is the **SP107E**, ~$15: DC5-24 V so it survives a bank that
absorbs at 14.6 V, up to 960 pixels, WS2813/WS2815/WS2818, remembers its state
across a power cut, rated -20 to 60 C. Service `0xFFE0`, characteristic
`0xFFE1`, four-byte commands, and it reads its own state back (`0x01`, `0x02`)
so `confirm()` can be honest.

Three things gate it:

- **Fix the scanner starvation first.** A second connectable device adds its
  own connect and reconnect cycles to a scanner that is already held down
  across the BMS's, and the temperature sensors starve. This is a prerequisite,
  not a follow-up. See the item under Smaller things.
- **Two controllers is the ceiling.** NimBLE allows three connections and the
  BMS holds one. A third fixture means raising
  `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` and paying RAM for it, or moving lighting
  to Wi-Fi. **UNVERIFIED:** two simultaneous connections have never been made
  on this board.
- **Confirm RGBW before ordering.** The listed ICs are RGB, and RGB white is
  muddy in a cabin. WS2814 is the 12 V RGBW part; whether this controller
  drives it is unconfirmed.

The protocol is close to the SP110E's but not the same - off is `0xBB` not
`0xAB`, colour `0x0C` not `0x1E` - and the readback payload is only partly
mapped, so the integration wants a real device in hand before it is written.
Budget ~0.2 A per fixture of idle draw, controller plus pixel ICs, with the
lights off.

### These controllers have no access control at all

Not pairing, not bonding, not a PIN - the SP107E class is open GATT, and so is
every cheap BLE LED controller. `decisions/0012` weighed voltage, idle draw,
protocol dialects and the connection ceiling and never considered this, which
is a gap in that record rather than in the product.

Three consequences, and the second is the one that gets missed. A neighbour
with the vendor app can change the lights. Worse, **they can deny you control**
- these peripherals accept one central at a time, so whoever connects first
holds the slot. And the lights answering at all tells a passer-by the van is
powered. The blast radius is lights only: the controller is an isolated
peripheral and cannot reach the BMS, the registry or anything else.

What actually helps, in order:

- **Hold the connection.** While the van is connected the peripheral refuses
  everyone else. Free, and it suits the design anyway. Two gaps: the slot is
  open while the van is off or rebooting, and if somebody is already connected
  at boot then *we* are locked out instead. **UNVERIFIED** that this class
  accepts only one central - worth checking with the first controller bought.
- **Keep the fixture's existing wall switch upstream.** The controller is then
  physically dead when the light is off: no radio, no attack surface and no
  parasitic draw. It answers the security problem and the 5 Ah/day idle draw
  with a wire that is already there.
- Renaming the device is not a defence; a scanner finds it by service UUID.

If "nobody else may trigger the lights" ever becomes a hard requirement rather
than a preference, **this class of product cannot meet it**, and no firmware on
the van side changes that. That would supersede `0012` and make lighting depend
on Wi-Fi landing first.

### ESP-NOW is the stronger alternative, and is not yet decided

Evaluated 2026-09-10, not chosen. An ESP32-based controller such as the
Gledopto GL-C-017WL-D - DC5-24 V, four channels, 800 pixels, ships with WLED -
could be driven over **ESP-NOW** rather than BLE. That fixes, in one move, most
of the objections above: connectionless, so the three-connection ceiling stops
applying; CCMP/AES-128 with a PMK and LMK, so there is real encryption; about
17 peers; and millisecond latency instead of a BLE connect.

Three routes, and they are not equivalent:

| | AP needed | Security | State readback |
|---|---|---|---|
| Stock WLED, ESP-NOW remote | no | MAC allowlist | **none** |
| Stock WLED, Wi-Fi JSON | yes | WPA2 | full |
| Reflashed, custom ESP-NOW | no | CCMP + HMAC | full |

WLED's ESP-NOW support is a *remote* protocol - one-way, button events only -
so the stock route reintroduces exactly the "confirm, do not assume" violation
that disqualified the analogue controllers. Only reflashing delivers the whole
prize, at the cost of owning node firmware.

The radio cost is measured, under Smaller things: Wi-Fi takes 41 KB and about a
fifth of advertisement reception, but it is paid once and would be paid anyway
if the backhaul lands. Espressif's own caveat is that MAC addresses are
spoofable, so anything security-critical wants an application-level HMAC in the
payload; for van lighting, encryption plus an allowlist is proportionate.

One hardware caution: the GL-C-017WL-D is rated **-20 to 45 C**, against the
SP107E's 60 C. A parked van in summer sun goes past 45 C, so placement matters.

### LoRa was considered and set aside

An SX1262 second radio (2026-09-10) would escape the BLE connection ceiling
entirely and reach a trailer or a remote tank. Set aside for three reasons: the
chip is almost certainly not fitted on this board (see `HARDWARE.md`), the
pinout puts `BUSY` and `DIO1` behind the I2C expander, and there is no
ecosystem of LoRa light controllers to buy - SenseCAP's own LoRa sensors are
LoRaWAN, which wants a gateway and a network server in a van. Point-to-point
means building both ends. Worth revisiting only for something BLE genuinely
cannot reach.

**6. There is no way to update the firmware without a cable.** Every change so
far has been flashed over the CH340 with a laptop in the van. That is fine while
the van is on the drive and useless anywhere else, and it is the thing that
decides whether a fix can ever reach a van that is already out.

The partition table already supports it. `default_8MB.csv` ships two 3.3 MB app
slots and an `otadata`, and the image is **1,684,836 bytes - 50.4% of one
slot** (2026-09-25), so both slots plus the 1.5 MB `spiffs` fit inside 8 MB with
room to spare. Nothing about the flash layout needs changing.

Three things stand between that and a working update, and the first is the one
nobody would guess.

### The framework disarms rollback before `setup()` runs

The prebuilt libraries set both `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` and
`CONFIG_APP_ROLLBACK_ENABLE=y`, so the bootloader *can* revert a bad image. The
Arduino core then throws that away. In `initArduino()`:

```c
if (!verifyRollbackLater()) {            // weak, returns false by default
  if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
    if (verifyOta()) {                   // weak, returns true by default
      esp_ota_mark_app_valid_cancel_rollback();
```

Both hooks are `__attribute__((weak))` with defaults that say "yes, this image
is fine" - and they run **before a single line of `setup()`**. An image that
boots and then boot loops is marked valid and the van is stuck, cable-only.

This project has produced exactly that image twice in two weeks: the LVGL pool
exhaustion in `decisions/0013` and the LittleFS cache panic in `decisions/0014`.
Both crashed *after* `setup()` began. Either, shipped over the air, would have
bricked a van until someone arrived with a laptop.

The fix is to override the weak symbols: `verifyRollbackLater()` returns true,
and the main loop calls `esp_ota_mark_app_valid_cancel_rollback()` only once the
device has proved itself. **The health signal already exists** - `displayLoop`'s
heartbeat is the project's own definition of alive (`loops` and `flushes` both
climbing, `docs/TROUBLESHOOTING.md`), and `Hub` reports a stalled integration by
name. "Valid after N heartbeats with flushes climbing" is the policy, and it is
derived from the debugging protocol rather than invented for OTA.

Without that override, do not ship OTA at all. With it, a bad image costs one
reboot.

### Writing 1.7 MB of flash is the `decisions/0014` hazard at scale

`decisions/0014` established that any flash access while the RGB panel refreshes
panics the chip - a LittleFS `mkdir`, a few kilobytes, did it reliably. An OTA
writes the whole app slot.

The failure is benign, which is the one piece of luck here. `esp_ota_begin()`
erases the target slot and `Update.end()` only repoints `otadata` at the end, so
a panic mid-write leaves the running app untouched and the next boot is the old
firmware. **The risk is an update that never completes, not a brick.** That
makes it safe to attempt before the hazard is solved.

Completing it needs the panel quiet during the write. One hypothesis, untested:
`CONFIG_LCD_RGB_RESTART_IN_VSYNC=y` means `draw_bitmap` is what arms the
flash-resident restart path, so if LVGL stops flushing entirely the interrupt
may stop reaching `gdma_hal_append`. **UNVERIFIED**, and a cheap experiment:
stop calling `lv_timer_handler()`, write a megabyte of scratch to the unused
`spiffs` partition, and see whether it survives. If it does, an OTA is "show a
progress screen, stop the UI, write, reboot" and the hazard never fires. If it
does not, the honest answer is tearing the panel down first, which means
reaching `esp_lcd_panel_del()` through a patch to Arduino_GFX
(`PORTING_NOTES.md`).

The `spiffs` partition is free for that experiment: `CFG_LOG_TO_FLASH` is 0, so
nothing uses it.

### The transport is the easy part, and it no longer pairs with gap 2

| | Needs | Time for 1.7 MB | Note |
|---|---|---|---|
| Wi-Fi SoftAP upload form | Wi-Fi, ~41 KB | seconds | no router, no creds |
| Wi-Fi HTTP pull | Wi-Fi + a server | seconds | wants a release host |
| BLE from a phone | a connection slot | minutes | see below |
| USB cable | nothing | seconds | works today |

**Revised 2026-10-18.** This section used to argue that SoftAP was cheap because
it shared its 41 KB with the phone mirror under gap 2 - one access point serving
both a readings page and an "update firmware" upload. Gap 2 has since
established that a SoftAP cannot be the phone path at all, because joining it
drops wireless CarPlay or Android Auto. **SoftAP now has to justify the radio on
its own**, which it can still do: an update is a deliberate, parked, one-off act
where nobody cares that the car's screen is dark. But it is no longer free by
association, and the two decisions are now independent. **Give the AP a WPA2
password**: anyone who can join it can reflash the van, and `CONFIG_SECURE_BOOT`
and flash encryption are both off, so there is no signature check behind it.

**The "BLE takes over an hour" row was wrong, for an instructive reason.** It
came from the measurement under Smaller things - the link comes up at the
23-byte default and nothing asks for more - but that is an observation about
*this device as central*, talking to a BMS. When a **phone** is the central it
drives the MTU exchange itself: Android asks for up to 517 bytes and iOS for
185, with no firmware involvement at all. So the ~84,000 writes become a few
thousand, and the realistic figure is minutes. The MTU is not a prerequisite
here; the phone does it for us. **UNVERIFIED** - estimated from the MTU and
connection interval, never measured on this board, and real BLE throughput is
usually well under the arithmetic. What does still hold is that it spends one of
the three connection slots, which `decisions/0012` has already promised to
lighting.

### Order of work

1. Override the rollback hooks and prove a deliberately-broken image reverts.
   This is worth doing **before** any transport exists, because it is the part
   that makes the rest safe, and it is testable over the cable.
2. Run the "does flash work with LVGL stopped" experiment against `spiffs`.
3. Only then pick a transport, and pick it on its own merits - gap 2 no
   longer shares the bill. SoftAP is still the front runner for an update
   specifically, because an update happens parked and deliberately.
**7. The phone can only read, and three things want it to write.** Added
2026-10-18. Sensor names, the clock, and eventually the temperature unit all
want to travel from the phone to the controller, and **none of them can
today**: the advertisement is one-way by design and the controller is
deliberately non-connectable (`decisions/0016`). This is the item that
reverses that property, so it deserves the most care of the three below.

What makes it worth doing anyway is the clock. There is no RTC, no Wi-Fi and
no NTP, so **the only source of time is a person typing it into the Settings
page** (`ARCHITECTURE.md`), and after a power cut the clock is wrong by
however long the power was off and says so in amber. A phone in the van knows
the time exactly. Syncing it is what makes the daily highs and lows
trustworthy rather than approximately placed, and it retires a whole screen.
Note the clock is **local time with no timezone**, so the phone must send
local time, not UTC.

Four things stand in the way, and the third is the one that decides the shape:

- **It spends a connection slot.** NimBLE allows three, the BMS holds one and
  `decisions/0012` has promised two to lighting. Keep the phone's connection
  transient - connect, write, confirm, disconnect - and the ceiling never has
  to move.
- **Do not use BLE bonding for the authentication.** NimBLE's bond store
  writes NVS from the host task on core 0, which breaks this project's rule
  that every NVS write happens on the main loop through `Settings`, and breaks
  it during panel refresh - the `decisions/0014` hazard, in the one context
  the rule exists to prevent. An application-layer HMAC challenge-response
  keeps the rule intact. Writing a synced name or clock is an NVS write too,
  so it must be handed to `Settings` and committed on the main loop, which
  `main.cpp` already does every pass.
- **Sensor identity does not survive the crossing.** The controller keys
  sensors by MAC; **iOS never gives an app a MAC**, only a rotating
  per-install UUID (observed 2026-10-18, `docs/ICD.md`). So a name sync keyed
  by address works on Android and can never be ported. The way out is to stop
  trying: once there is a connection, let the controller hand over its whole
  sensor table - slot, address, name, readings - and have the app display
  *that*, with its own scanning as the fallback for when the controller is out
  of range. Identity then belongs to the controller alone and the problem
  dissolves. **That is a bigger change than "sync the names" sounds**, and it
  is the right one.
- **A beacon from the phone would avoid the connection entirely, on Android
  only.** The controller already scans continuously, so a phone advertising a
  small time-and-names payload would be picked up with no connection and no
  inbound surface at all. It is tempting and it is a trap for this project:
  **iOS gives an app no way to put arbitrary manufacturer data in an
  advertisement**, only service UUIDs and a local name. Taking this route
  would quietly make the write path Android-only, which is exactly the
  decision gap 2 set out not to make.

Any of this changes the wire format, so it is an **ICD change**: see
`docs/ICD.md`, and expect it to be a major version bump since it is not an
additive object in an existing frame.

**8. There is no "time remaining" estimate.** Added 2026-10-18. The single
number a person actually wants - *how long will the battery last at what I am
drawing now* - as `hours:mins`. Everything it needs is already published:
`battery.remaining` in amp-hours (`get_balance_capacity()`), `battery.current`
signed, and `battery.power`.

The arithmetic is trivial and is not the problem. The problem is that **a naive
estimate is a confident lie**, which is the failure this project cares most
about:

- Current swings hard as a fridge compressor cycles or an inverter loads up.
  Dividing by the instantaneous value makes the figure flap between two hours
  and twenty minutes, and a number that moves like that is worse than none.
  It wants smoothing over minutes - an EWMA or a small ring, in the shape
  `core/history.*` already uses.
- Near zero current the answer is infinite. It must say so, not print
  `999:59`.
- Charging is a different question, not a negative answer. Either say
  "charging" or answer "time to full", but do not run the discharge formula
  backwards.
- A few seconds after a load changes, the estimate is wrong no matter how it
  is smoothed. It should **refuse to answer until it has enough samples**, the
  same way the clock admits to being approximate.

It belongs in `src/core/` as hardware-free logic with host tests - that is
what `env:native` is for, and the edge cases above are exactly what tests are
good at pinning.

**Getting it to the phone is the awkward part, and it is an ICD question.**
The controller is the only end that knows the amp-hours, so the controller
should compute it; two implementations of the same estimate would drift, which
is what `docs/ICD.md` exists to prevent. But **the advertisement has three
spare bytes of thirty-one**, and BTHome has no object id that honestly means
"minutes remaining" - `0x42 duration` is uint24 at 0.001 s, the wrong width
and the wrong scale. So this is the first feature that forces the decision
`decisions/0017` deferred: a scan response for a second 31 bytes, payload
rotation, or extended advertising. Decide that before writing the estimate, or
it will be stranded on the device.

**9. The app and the controller do not look like the same product.** Added
2026-10-18. The controller is LVGL - a nav bar across the domains, tiles two
across, graphs behind them, amber for anything stale, a Settings overlay. The
app is a single scrolling column that was written to prove the radio worked.

Two cautions before cloning screens:

- **Mirroring pages one for one produces empty pages.** The app has Power,
  partially, and Climate from the sensors it hears directly. Water and
  Lighting do not exist on either end yet. The honest target is the *visual
  language* - the palette, the type scale, tiles rather than a list, amber for
  stale, the same °C/°F rule - not a page-for-page copy of a nav bar with two
  dead entries.
- **The colours are currently duplicated, which means they will drift.**
  `src/ui/theme.h` holds the controller's palette and the app hardcodes
  `#14161A`, `#E8E8E8`, `#8A93A0` and friends in `MainActivity`. One of them
  has to be the source, and the other has to be checked against it -
  `tools/check_icd.py` is the obvious place, since it already exists to stop
  exactly this kind of two-ended drift.

And one that is not cosmetic: the temperature unit is a **global setting in
NVS** on the controller (`core/units.h`, and deliberately not a per-screen
choice), while the app always prints Celsius. Consistency here means syncing
that setting, which is gap 7's problem, not a UI problem.

## Smaller things

- **`WiFi.h` is included for a single line.** `bms_jbd.cpp` pulls in the whole
  Wi-Fi and Networking stack purely to call `WiFi.macAddress()` for the BLE
  device name, and that include is the only reason `platformio.ini` carries the
  `Network` lib_deps and `-I` workaround. `NimBLEDevice::getAddress()` returns
  the same thing. Removing the include should let the workaround go with it -
  worth doing before Wi-Fi lands for real, so the dependency is deliberate
  rather than accidental.
- **Wi-Fi and BLE do coexist on this board, and the bill is mostly heap.**
  Measured 2026-09-10, because the ESP-NOW lighting idea needs the Wi-Fi radio
  while the van needs BLE continuously, and the two share one 2.4 GHz radio.
  With `WiFi.mode(WIFI_STA)` and `esp_now_init()` running alongside the BMS
  connection and the sensor scan:

  | | Advertisements | Loops / 2 s | Free heap |
  |---|---|---|---|
  | BLE only | 4.93/s | ~46,500 | ~95 KB |
  | Wi-Fi up, silent | 3.29/s | ~43,800 | ~54 KB |
  | ESP-NOW at 4 Hz | 3.52/s | ~41,600 | ~53 KB |

  Nothing broke. ESP-NOW delivered 195 of 195 packets in each run, the BMS
  connected and stayed online, no integration held the loop, and the UI kept
  flushing. **Wi-Fi costs 41,392 bytes** - within a hundred bytes of the ~41 KB
  this file already predicted.

  The useful detail is which cost comes from what. Advertisement reception
  falls the moment the radio comes up and transmitting barely adds to it (3.29
  against 3.52/s, inside the spread), so there is no send-rate to tune - the
  loss is the price of the radio existing. Transmitting does cost loop rate,
  about 5% on top, and 4 Hz is far more traffic than light commands would ever
  generate.

  So the direction is viable. The reservation is heap: 54 KB free against 95 KB
  is less than half the headroom, and the System page, more sensors and the
  Wi-Fi backhaul all still want room. Note that the 41 KB is paid once - if the
  backhaul lands later it is the same stack, so ESP-NOW for lighting is
  effectively free on top of a decision this project already wants to make.

  The spike itself is on the `wifi-coexistence-spike` branch, not on main -
  `SPIKE_WIFI` there reproduces both rows.

  **UNVERIFIED:** it never received an ESP-NOW packet or drove a light,
  and Wi-Fi was in station mode joined to nothing. A real backhaul associated
  with an access point is a heavier radio load than this measured.
- **A longer BLE connection interval measured worse, not better.** The BMS link
  runs on NimBLE's default 50 ms interval and is polled twice a second
  (`kRefreshMs` is 500), so the radio wakes roughly 15 times more often than
  the data needs. Asking for 200 ms works - the BMS accepts, and the negotiated
  values come back 200 ms with a 4000 ms supervision timeout - but measured on
  2026-09-10 it took advertisement reception *down*, from a mean 4.93/s across
  two runs to 4.03/s across two. Loop rate and BMS function were unaffected
  either way. The prediction was that waking the radio four times less often
  would free airtime for scanning; it did not, and **the mechanism is not
  understood**. Left in `BleSerialClient.cpp` behind `CONN_PARAM_TUNING`,
  defaulting off. Worth re-running when a second connection actually contends
  for the radio - which is the lighting plan in `decisions/0012` - because that
  is the case it was meant for and the only one where the trade can be judged.
- **The BLE MTU never negotiates up.** `BleSerialClient.h` defines `MIN_MTU 50`
  and `BLE_BUFFER_SIZE 512`, but the link comes up at the 23-byte default -
  20 bytes of payload per write - and stays there. Nothing asks for more.
  Whether that costs anything on a protocol whose frames are short is not
  measured, but the constant claims an intent the code does not carry out.
  Note this is a fact about **this device as central**: a phone connecting
  to us drives the MTU exchange itself, so it does not inherit this. Gap 6
  used to assume otherwise.
- **A BMS that cannot connect starves every other BLE listener.**
  `BleSerialClient` pauses the shared scanner for the whole connect sequence,
  and a connect failing with HCI `0x3e` retries four times at a 5 s timeout, so
  the scanner can sit paused for twenty seconds at a stretch and temperature
  readings go stale. Observed 2026-09-09; see `TROUBLESHOOTING.md`. The fix is
  to resume the scanner between attempts rather than across the whole cycle.
- **An entity updated in the first millisecond after boot reads as never
  updated.** `Entity` encodes "never updated" as `updatedMs_ == 0`, which is
  also a legitimate value of `millis()`, so such a value shows `--` until the
  next update. Found by `test_entity.cpp:test_epoch_zero`, which pins the
  current behaviour rather than fixing it - nothing publishes that early (the
  BMS takes ~10 s to reach `online`), so this has never been observable. A
  separate `everUpdated_` flag is the fix if it ever matters.
- **Bring-up scaffolding is still in `src/bsp/display.cpp`**: the loop/flush
  heartbeat and `scanI2C`. The expander sweep and the `0x48` probe are gone,
  having answered their question. These two are worth keeping in some form -
  the natural home is the System page (1), where they would be readable on the
  device instead of only over the wire.
- **`flushCb` rewrites LVGL's buffer in place** to rotate 180 degrees. Correct
  for `RENDER_MODE_PARTIAL` today, but it is a side effect on memory LVGL owns
  and will break quietly if the render mode changes. `lv_display_set_rotation()`
  or a corrected panel init table is the honest fix.
- **`Registry::inDomain()` allocates per call**, returning a `std::vector` by
  value from the UI refresh at 4 Hz. Heap churn on a device with no
  defragmentation. An iterator or caller-supplied span costs nothing now.
- **Entity ownership is undocumented in the header.** The registry holds raw
  pointers it does not own; integrations hold the entities as members. Reasonable
  for embedded, but it is an unwritten contract. One sentence in `registry.h`.
- **Alarm slots are never reclaimed.** `count_` only grows and `clear()` just
  sets `active = false`. Fine while ids are static literals - which they must be
  anyway, and now doubly so, since alarms are raised from the BMS task and read
  from the main loop. State the constraint where `raise()` is declared.
- **The BMS task's stack is oversized.** 8 KB allocated, and `taskLoop()`
  reports 6,044 bytes of headroom - about 2 KB actually used. It could drop to
  4 KB, but with ~97 KB free since the NimBLE port there is no reason to take
  that risk yet. The reporting stays so the decision can be evidence-based
  whenever RAM matters again.

## Done

- **An ICD between the controller and the app, enforced at the commit**
  (2026-10-18). The two ends ship separately - the van over a cable in a
  driveway, the phone whenever someone remembers - so they will be different
  ages, and a silent mismatch is not a crash but a plausible wrong number on a
  screen someone is using to decide whether to run the heater. `docs/ICD.md`
  is now the normative contract; `include/icd.h` and `Icd.java` are the halves
  each end compiles.

  **The version travels in the advertised BLE name** - `CC` plus the version,
  so `CC1` - because the advertisement had no room for a payload object and
  the name was going spare. It is built from the constant by token pasting, so
  bumping the version moves the name with it, and the name is therefore
  protocol: it left `config.h` for `icd.h`.

  The rule that gives the version meaning: it is **major only, and bumps only
  on a breaking change**. Adding a reading does not bump it, because BTHome
  ids ascend and an older reader stops at the first id it does not know and
  reports a short frame rather than a wrong one. So an unknown version means
  something the reader cannot see has changed meaning - and the app therefore
  **hides the readings** instead of showing them.

  `tools/check_icd.py` is the machine-checkable part: versions match, every
  object the firmware actually broadcasts is decodable by the app, the shared
  UUIDs and company id agree, and the advertisement still fits 31 bytes - the
  last recomputed from the real `publish()` calls rather than a number written
  down once. The **pre-commit hook runs it, and `SKIP_DOC_CHECK=1` does not
  skip it**, because a drifted protocol is a broken van rather than a missing
  paragraph. `android/build.sh` runs it too.

  Verified 2026-10-18 by breaking it four ways on purpose - a version bumped
  on one side, a broadcast object the app cannot decode, a drifted constant,
  and a name grown past the budget - each of which fails the check. And end to
  end on hardware: firmware deliberately built at v2 advertising as `CC2`
  against the v1 app, which hid all five pack readings and named which end was
  older, while the sensor list kept working, since sensors are not part of
  this interface. Reverted to v1 and reflashed afterwards.

  Also cleared here: two SwitchBots were listed at once, so the sensor list is
  no longer unverified beyond a single device.

- **Pack power and current on the phone, and the sensors read directly**
  (2026-10-18). The broadcast gained **signed power (`0x5C`) and signed current
  (`0x5D`)**, which `decisions/0016` had wrongly said BTHome could not carry;
  it reads the object table incompletely. Signed current still holds only
  +-32.767 A against a pack rated past 120 A, so it is dropped rather than
  clamped when the pack exceeds it and the app recovers current from power /
  voltage - an exact recovery, because `JbdBms` publishes power as `v * a`.
  `decisions/0017`.

  **The advertisement is now exactly full**: 19 payload bytes, 23 of
  service-data AD, 8 for the name, 31 total. Measured on hardware as `ad=31
  payload=19` with no refusals, and pinned by
  `test_bthome_encode.cpp:test_the_full_frame_fits_a_legacy_advertisement`.
  `CFG_BLE_BROADCAST_NAME` shrank to `Camper` to pay for it, and anything added
  from here displaces something.

  **The temperature sensors are not relayed - the app reads them off the air.**
  Relaying had no room left, and two temperatures in one BTHome frame are both
  object `0x02` with no way to tell them apart. The sensors already broadcast
  and the phone is in the same van, so the app grew a SwitchBot decoder
  mirroring `core/switchbot.cpp` and lists every thermo-hygrometer in earshot.
  It costs no firmware change, no payload and has no ceiling on sensor count.
  What it cannot see is the names the crew typed into the controller and the
  daily highs it keeps, both of which live in its NVS.

  Still absent, and not for want of a signed field: **remaining amp-hours has
  no BTHome object id at all**, and the per-cell voltages would be repeated
  `0x0C` objects told apart only by position, which is ambiguous to every
  generic reader.

  Verified on hardware 2026-10-18: `ad=31 payload=19`, no refusals, and read
  off-air as `40 00 1A 01 63 02 D7 05 0C E2 36 5C 00 00 00 00 5D 00 00` -
  packet 26, 99%, 14.95 C, 14.050 V, +0.00 W, +0.000 A. 202 host cases pass.
  Verified on a Pixel 8 Pro the same day: all five pack readings live, plus a
  real SwitchBot W3400010 at 23.0 C, 24% and 100% battery read directly off
  the air at -99 dBm.

  Three bugs the phone found that nothing else would have, all in the app and
  all written up in `android/README.md`: **the controller listed itself as a
  sensor** during the ten seconds after a reboot when its frame carries no
  readings and so looks like an anonymous broadcaster; **rebuilding the sensor
  rows every tick** reset the scroll position continuously; and **the top of
  the screen was drawn under the status bar**, because an app targeting SDK 35
  on Android 15 is edge-to-edge whether it asks or not. The last one is worth
  the most: it looked exactly like a scrolling bug, and swiping proved it was
  not, because the content fitted and there was nothing to scroll.

  **UNVERIFIED: no negative power or current has been seen**, because the bench
  pack is at rest; the signed path is covered by host tests only, including the
  wire bytes of -312.50 W. Two SwitchBots have since been listed at
  once, so that part is no longer unverified.

- **A phone can read the van, with no Wi-Fi and no app** (2026-10-18). The van
  advertises its state as an unencrypted BTHome v2 service-data frame under
  `0xFCD2` and is **never connectable**, so reading it costs no connection slot,
  no pairing and no inbound attack surface, and serves any number of phones at
  once. `core/bthome_encode.*` builds the frame, `integrations/ble_broadcast.*`
  puts it on the radio. `decisions/0016` has the reasoning and the rejected
  alternatives.

  This replaced the SoftAP plan rather than postponing it: a phone holds one
  Wi-Fi association and wireless CarPlay / Android Auto already owns it, so the
  readings page this file had planned would have cost the car's screen every
  time. Design gap 2 is rewritten; gap 6 no longer gets to share the radio's
  cost with it.

  **The arrow runs the other way for the first time.** Every other integration
  owns a device and publishes entities *into* the registry; this one reads the
  registry and puts it on the radio. It is still an `Integration`, because
  `Hub` is what would report it by name if it ever held the loop - but
  `ARCHITECTURE.md` now records the exception, and that advertising has a single
  owner for the same reason scanning does.

  The measurement worth keeping: **advertising costs ~9% of the loop's spare
  headroom and the UI does not notice.** 20,849 against 22,837 loops/sec
  median, broadcast on against off, and 84.0 flushes/sec in both. And **the
  cost is the radio, not the refresh** - every 10 s instead of every 2 s left
  the loop rate identical at 20,856, so `CFG_BLE_BROADCAST_REFRESH_MS` is a
  freshness knob and nothing else. The advertisement-reception difference
  (4.21 to 4.05/s) is not a real effect: a third run with the broadcast on
  measured 4.35/s.

  Two honest gaps at the time. The first - "pack current and power cannot be
  carried" - **was wrong, and was fixed the same day in `decisions/0017`**:
  BTHome has signed variants the first reading of the table missed. The
  **Android app had never run on a phone**: it is
  430 lines of dependency-free Java in `android/`, it builds to a signed APK
  from the SDK tools with no Gradle, and its decoder was checked on the JVM
  against a frame captured off-air - but no device was attached to install it.
  Flutter is still the plan for the real app; see `android/README.md`.

  Verified on hardware 2026-10-18: boots clean, BMS reaches `online`, three
  readings in the frame, LVGL pool unchanged at 16,388 free and 73% used, heap
  74,072, 20,961 loops/sec and 84.0 flushes/sec. Read off-air twice from an
  independent stack: `40 00 0A 01 63 02 B9 05 0C F6 36` decoding to packet id
  10, 99%, 14.65 C and 14.070 V, agreeing byte for byte with the firmware's
  encoder, a Python decoder and the app's Java one. 193 host cases pass.
- **A touch UI for setting the clock** (2026-09-17). The first attempt reused
  the rename keyboard and asked for `2026-09-17 14:03` as text, which is the
  wrong tool for picking five numbers - the `-` and `:` are behind a page
  switch. Replaced with five columns of up/down steppers, 86x58 each, holding
  an arrow to repeat, and a live preview of what they are building. The
  arithmetic is `stepDateField()` in `core/clock.cpp`, tested: stepping a field
  never carries into another, and shortening a month clamps the day rather than
  rolling over - 31 January into February is 28 February, not 3 March.

  It cost more pool than it looked like it would, and the fix is worth knowing
  about: ten `lv_button`s with child labels is twenty objects and left the pool
  at **90% used**, one screen away from `decisions/0013`'s boot loop. Two
  `lv_buttonmatrix` rows and building the screen on demand put it back to
  16,388 free - exactly where it was before the screen existed.
  `decisions/0015`, which is the first time the lazy-overlay escape hatch `0013`
  wrote down has been used.

  Also fixed here: **renaming a sensor had been silently doing nothing** since
  the text-entry overlay was made shared. `closeEntry()` cleared the address
  before the save callback read it.

  Verified on hardware 2026-09-17: every column stepped through real LVGL
  events, the February clamp observed, the clock applied and marked exact, and
  five open/close cycles leaving the pool at 16,296 free and 2% fragmented.
- **A 24-hour graph, daily highs and lows, and a clock to hang them on**
  (2026-09-17). The graph window went from 90 minutes to 24 hours (144 samples
  ten minutes apart, on elapsed time rather than the wall clock). Daily high
  and low per sensor come from the live per-minute readings, not from the graph
  ring - a ten-minute ring walks past the hour the sun is on the roof - and
  reset at local midnight. `core/clock.*` is a hand-set wall clock, persisted
  in NVS and marked `approximate` after a restore, because the board has no RTC
  and no NTP; the rename keyboard became a shared text-entry overlay so setting
  it cost no extra LVGL pool.

  **The daily file asked for is not there, and cannot be on this hardware.**
  Both storage routes were probed and both are closed - see **Design gaps 3**
  and `decisions/0014`. The format, rotation and replay are written and tested
  behind `CFG_LOG_TO_FLASH`, waiting on the RP2040.

  Verified on hardware 2026-09-17: midnight rollover observed with the clock
  driven forward (`day=20713` to `20714`, extremes reset), no panics, ~41k
  loops and 168 flushes per heartbeat, heap steady ~73 KB.
- **Climate page: tiles, a per-sensor graph, and a global temperature unit**
  (2026-09-17). Sensors are tiles two across rather than a list, so a
  temperature is legible from across the van; tapping one opens a detail page
  with the readings large and an hour and a half of temperature and humidity
  behind them. Renaming moved onto that page, off the tile. `core/units.*`
  holds the °C/°F choice for the whole system and `core/history.*` holds the
  trend ring; both are hardware-free and covered by `pio test -e native`, which
  is now 119 cases.

  The rule worth keeping: **readings are stored in Celsius and converted once,
  on the way to the screen.** `TemperatureEntity::value()` stays Celsius so
  every alarm threshold keeps comparing against the same numbers when the crew
  taps °F.

  The graph stores gaps rather than repeats - a sensor that went quiet leaves a
  hole in the line instead of a flat stretch that was never measured.

  It cost more than it looked like it would: see `decisions/0013`. Verified on
  hardware 2026-09-17 - tiles and the detail page driven from a temporary
  self-test through 175 s with no panic, ~45k loops and 168 flushes per
  heartbeat on the Power page, heap steady at ~74 KB, pool 16,452 free with the
  graph open and the unit switched live underneath it.
- **Wireless temperature and humidity, end to end** (2026-09-16). A real
  SwitchBot W3400010 reads on the Climate page, confirmed against the
  SwitchBot app. `core/bthome.*` decodes BTHome v2, `core/switchbot.*` decodes
  the SwitchBot, and `integrations/env_sensors.*` publishes either into
  `Domain::Climate`. Sensors are discovered rather than configured: any
  broadcaster in earshot binds a slot in a fixed table of eight, the Climate
  page gives each one a row, and tapping a row renames it into NVS.

  The SwitchBot settled two things its documentation could not. The byte
  offsets that work are the ones **counting the two company-id bytes**, which
  is what `NimBLEAdvertisedDevice::getManufacturerData()` returns - the other
  published convention is off by two, and choosing wrong yields a plausible
  wrong temperature rather than an error. And the device **sets the top bit of
  the battery byte**, sending `0xE4` rather than `0x64`, so masking it is
  load-bearing. A real frame is manufacturer data
  `69 09 <address> 0B 02 08 95 26 00` under company `0x0969` plus service data
  `77 00 E4` under UUID `0xFD3D`, decoding to 21.8 C, 38% and 100% battery.

  Readings arrive split across two advertisements - temperature and humidity in
  manufacturer data, battery in service data - so `onAdvertisement` merges
  rather than replaces; otherwise a battery-only frame blanks the temperature
  twice a minute. `SWITCHBOT_TRACE` in `env_sensors.cpp` dumps raw frames and
  decoded values, and is off.
- **Shared BLE scanner, and a BTHome v2 decoder** (2026-09-09).
  `BleScanner` owns `NimBLEDevice::getScan()` and fans advertisements out to
  registered listeners; `BleSerialClient` is now one listener among them rather
  than the owner. Crucially it **keeps scanning while the BMS connection is
  up**, which the shipped code never did - the advertisement counter used to
  sit frozen for the whole time it was connected. Verified on hardware with a
  temporary second listener: `listeners=2`, and the second one kept receiving
  ~3.7 advertisements/sec with the BMS `online`. Costs the cooperative loop
  nothing (46,729 loops and 168 flushes per heartbeat against 46,821 and 168
  before) and ~1.5-2 KB of heap while a window is in flight.
  `core/bthome.*` decodes BTHome v2 with 17 host tests, chosen over any vendor
  format because pvvx >= 6.0 speaks only BTHome and one decoder covers the
  Xiaomi tags, Shelly BLU and most DIY sensors. See `ARCHITECTURE.md`.
- **Host tests for `src/core/`** (2026-09-08). `env:native` in `platformio.ini`
  builds `entity.cpp`, `registry.cpp` and `alarms.cpp` against a host compiler
  and a fake-clock `test_shim/Arduino.h`: 56 cases in about two seconds with no
  board, wired into CI ahead of the firmware build. Verified by mutation, not
  just by passing - loosening `stale()` to `>=`, making `Registry::find` a
  prefix match, and deleting the alarm auto-unmute each failed exactly the test
  that claims to cover them. Firmware size is unchanged (flash 49.2%, RAM
  32.6%), because the two `testReset()` helpers compile only under
  `CC_NATIVE_TEST`. This also closed the "no CI" half of the old item, which
  had already been overtaken by the CI work earlier the same day. See
  `ARCHITECTURE.md`.
- **Touch working** (2026-09-07). The controller answers at `0x48`, not the
  `0x38` its datasheet family uses; `Touch::read` needed no changes once the
  address was right. Axis mirroring is confirmed by use. See `HARDWARE.md`.
- **The BMS integration owns a task** (2026-09-07). Bluedroid's synchronous
  `connect()` was blocking the cooperative loop for 1.5-6 s per retry and
  stopping touch being polled, which read to a user as taps being ignored.
  Loop stalls went from five per 30 s to none, touch polling from 3-6/s back to
  a steady 28/s. See `decisions/0008`.
- **The BMS connects on a remembered address** (2026-09-07). The auto-save from
  `decisions/0009` captured `a4:c1:38:e0:af:18` on a successful connect, and the
  firmware now pins to it: `offline -> searching -> connecting -> online` in
  about 19 s from boot, with no loop stalls. This closed the last blocker
  without anyone having to read a MAC off a log and edit a constant.
- **CI, and a working commit gate** (2026-09-08). `.github/workflows/build.yml`
  runs `pio run` on every push and PR; until now every build had been verified
  on exactly one laptop. The pre-commit documentation gate turned out to have
  been **inert since it was written**: its own testing used `git reset --hard`,
  which deleted the then-untracked hook, and only the documentation describing
  it was ever committed. Restored, tracked, and verified firing.
- **Documentation restructured** (2026-09-08). The standard moved to
  `CONTRIBUTING.md` at the root, where "how we work" belongs, and
  `docs/README.md` became the index its filename promises. Two files called
  README serving different audiences was a genuine source of confusion.
- **Ported to NimBLE** (2026-09-07). +40 KB of heap (56,724 to 97,116 free at
  boot) and 400 KB of flash; the Arduino BLE library is gone from the graph
  entirely. Time to `online` improved from 12-19 s to 10 s. This unblocked
  Wi-Fi, and it is the foundation the shared scanner should be built on. Two
  things it had to learn: NimBLE will not connect while scanning and `stop()`
  is asynchronous, and HCI 0x3e is common and transient - retrying the connect
  beats paying for a rescan. See `decisions/0010`.
- **Screen timeout switches the backlight off** (2026-09-07). It dimmed to a
  glow before, which in a dark van is still a light source. The touch that
  wakes a dark screen is swallowed rather than delivered as a click.
- **Settings, persisted** (2026-09-07). `core/settings.*` stores the last
  connected BMS address, brightness and screen timeout in NVS, reachable from a
  gear in the header. The BMS address is saved automatically on connect and can
  be forgotten from the page. All writes happen on the main loop; tasks hand
  over. See `decisions/0009`.
- **`Hub::loop` names slow integrations** (2026-09-07). Rule 1 of
  `ARCHITECTURE.md` was unenforced; it now logs any integration holding the
  loop past 50 ms. This is what found the bug above.

- **Arduino_GFX bounce-buffer patch made durable** (2026-09-07).
  `tools/patch_gfx.py` applies it as a PlatformIO `pre:` script; verified by
  deleting the installed library and rebuilding from clean. See
  `decisions/0004` and `decisions/0007`.
- **Under version control** (2026-09-07). Repository initialised and pushed to
  `github.com/lpercifield/camper-control`, with `.gitignore` keeping the 223 MB
  of regenerable `.pio/` out of it.
- **Documentation kept honest at the commit** (2026-09-07).
  `.githooks/pre-commit` refuses a firmware change that documents nothing and
  prints the routing rule; `SKIP_DOC_CHECK=1` is the deliberate escape. This
  file going stale within a day of being written is what prompted it. Enable
  with `git config core.hooksPath .githooks` after a clone.
- **Session workflow captured** (2026-09-07). `CLAUDE.md` holds the commands,
  the traps and the instrument-before-theorising debugging protocol.
  `tools/readlog.py` is the serial capture that neither `pio device monitor`
  nor `cat` can do.

## Known and accepted

No audible alarm - the buzzer is on the RP2040 and needs the UART link.
The RP2040 is otherwise idle. See `PORTING_NOTES.md`.

This section used to say "one BLE connection only", citing `decisions/0003` and
`PORTING_NOTES.md`. Both citations were wrong: `0003` was superseded by `0010`
when the NimBLE port landed, and `PORTING_NOTES.md` never carried the claim.
NimBLE allows **three** connections and this project does not override the
default, so the BMS leaves two spare. Corrected 2026-09-09 - the claim had
been quietly ruling out a whole class of accessory. The one-client limit that
does exist is on the JBD BMS itself, not on this radio; see
`TROUBLESHOOTING.md`.

## Suggested order

1. **Fix the scanner starvation.** Not a numbered gap - it is under Smaller
   things - but it is the only thing actively degrading a feature that works:
   sensors go stale whenever the BMS struggles to connect. It also gates lights.
2. Add the System page (1), and move the heartbeat and bus scan into it - they
   are the numbers that diagnosed most of this project, and they are only
   visible over a serial cable.
3. Lights (4), once the starvation is fixed and a controller is in hand. The
   decision is made; what is left is the protocol capture and one integration
   file.
4. Wi-Fi backhaul (2). Decide what it talks to before building the plumbing -
   and note that ESP-NOW for lighting would ride on the same 41 KB.
5. Resolve the build-system split (3).

Sensors were the top item and are done. What is left is one bug, then the
things that turn a working device into a product.
