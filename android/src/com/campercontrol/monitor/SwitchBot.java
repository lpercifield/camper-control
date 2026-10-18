package com.campercontrol.monitor;

/**
 * SwitchBot Indoor/Outdoor Thermo-Hygrometer (W3400010) decoding, phone side.
 *
 * A hand-written mirror of src/core/switchbot.cpp. Unlike BTHome this is not an
 * open format - it is the one SwitchBot device that does not follow SwitchBot's
 * own published BLE spec, and every decoder in the wild is reverse engineered.
 *
 * **The byte offsets here count the two company-id bytes.** That matters more
 * than it looks: the two published decoders disagree on indices by exactly two,
 * because one counts the company id and the other does not, and choosing wrong
 * yields a plausible wrong temperature rather than an error. The firmware's
 * copy counts them, because that is what NimBLE's getManufacturerData()
 * returns - so this one does too, and {@link #decodeManufacturer} is handed a
 * buffer with the company id put back on the front. Keeping the two mirrors
 * indexed identically is the whole point.
 */
final class SwitchBot {

    /** Little-endian 0x0969 in the advertisement. */
    static final int COMPANY_ID = 0x0969;
    /** 16-bit service UUID the battery arrives under. */
    static final String SERVICE_UUID = "0000fd3d-0000-1000-8000-00805f9b34fb";
    /** Device type byte for the outdoor meter, 'w'. */
    private static final int TYPE_OUTDOOR_METER = 0x77;

    boolean valid;
    boolean haveTemp;
    boolean haveHum;
    float temperatureC;
    float humidityPct;

    /**
     * @param data manufacturer data <strong>including</strong> the two
     *             company-id bytes. Android's
     *             {@code getManufacturerSpecificData()} strips them, so the
     *             caller puts them back - see the class comment.
     */
    static SwitchBot decodeManufacturer(byte[] data) {
        SwitchBot out = new SwitchBot();
        // 2 company id + 6 address + 5 payload bytes before humidity is read.
        if (data == null || data.length < 13) return out;

        int company = (data[0] & 0xFF) | ((data[1] & 0xFF) << 8);
        if (company != COMPANY_ID) return out;

        out.valid = true;

        // [2..7] is the device address, then two undocumented bytes.
        // Temperature is split: the low nibble of one byte is tenths, the low
        // seven bits of the next are whole degrees, and the top bit of that
        // byte is the SIGN - set means POSITIVE, which is the opposite of the
        // usual convention and the easiest thing here to get backwards.
        float tenths = (data[10] & 0x0F) * 0.1f;
        float whole = data[11] & 0x7F;
        boolean positive = (data[11] & 0x80) != 0;
        out.temperatureC = (whole + tenths) * (positive ? 1.0f : -1.0f);
        out.haveTemp = true;

        out.humidityPct = data[12] & 0x7F;
        out.haveHum = true;
        return out;
    }

    /**
     * @param data the service-data payload after the 16-bit UUID.
     * @return battery percent, or -1 if this is not such a frame.
     */
    static int decodeServiceBattery(byte[] data) {
        if (data == null || data.length < 3) return -1;
        // The first byte is the device type. Checking it keeps us from reading
        // a battery percentage out of some other SwitchBot product's frame.
        if ((data[0] & 0xFF) != TYPE_OUTDOOR_METER) return -1;
        // The device sets the top bit of the battery byte, sending 0xE4 rather
        // than 0x64, so masking it is load-bearing.
        int pct = data[2] & 0x7F;
        return pct <= 100 ? pct : -1;
    }
}
