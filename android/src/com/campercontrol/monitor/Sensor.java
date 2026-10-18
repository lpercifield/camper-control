package com.campercontrol.monitor;

/**
 * One environment sensor heard over the air - a SwitchBot thermo-hygrometer or
 * any BTHome v2 broadcaster.
 *
 * These are read <em>directly</em> rather than relayed through the controller.
 * The sensors already broadcast, the phone is in the same van, and the
 * controller's advertisement is full - it is exactly 31 bytes. Relaying would
 * also hit BTHome's duplicate-id problem: two temperatures in one frame are
 * both object 0x02 and cannot be told apart. See docs/decisions/0017.
 *
 * What is lost by not relaying: the names the crew typed into the controller,
 * and the daily highs and lows it keeps. Those live in the controller's NVS and
 * nothing here can see them.
 */
final class Sensor {

    /**
     * The platform's device identifier, used as an <strong>opaque</strong>
     * key - never parsed, never shown as a MAC, never persisted. On Android it
     * happens to be a hardware address; on iOS the same slot holds a rotating
     * per-install UUID. Treating it as opaque is what keeps this portable, and
     * it is a different question from identifying the van itself, which is done
     * by what the frame carries.
     */
    final String key;

    String advertisedName;
    boolean haveTemp, haveHum, haveBatt;
    float temperatureC, humidityPct;
    int batteryPct;
    int rssi;
    long lastSeenMs;
    String kind = "BTHome";

    Sensor(String key) {
        this.key = key;
    }

    /**
     * Folds in whatever this frame carried, leaving the rest alone.
     *
     * **Merge, do not replace.** A SwitchBot splits its reading across two
     * advertisements - temperature and humidity in manufacturer data, battery
     * in service data - so overwriting on every frame blanks the temperature
     * twice a minute. The firmware learned this the same way; see
     * EnvSensors::onAdvertisement.
     */
    void mergeTemp(float c) {
        temperatureC = c;
        haveTemp = true;
    }

    void mergeHum(float pct) {
        humidityPct = pct;
        haveHum = true;
    }

    void mergeBattery(int pct) {
        batteryPct = pct;
        haveBatt = true;
    }

    String displayName() {
        if (advertisedName != null && !advertisedName.isEmpty()) {
            return advertisedName;
        }
        return "(unnamed " + kind + ")";
    }

    boolean stale(long nowMs) {
        // Deliberately longer than the firmware's 15 s: these sensors
        // broadcast about twice a minute, so 15 s would show everything as
        // stale almost always.
        return nowMs - lastSeenMs > 180_000L;
    }
}
