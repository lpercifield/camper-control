package com.campercontrol.monitor;

/**
 * BTHome v2 decoding, phone side.
 *
 * This is deliberately a hand-written mirror of src/core/bthome.cpp rather than
 * anything clever: the object table is the wire contract between the two ends,
 * and both ends live in this repository so the two copies can be compared by
 * eye. The firmware's copy is covered by `pio test -e native`.
 *
 * The one rule worth restating here, because it is what keeps a wrong number
 * off the screen: a BTHome object carries no length, so its size is implied by
 * its id. An unknown id is therefore not skippable - parsing stops rather than
 * guessing an offset and decoding garbage.
 */
final class BtHome {

    /** The 16-bit service UUID BTHome advertises under. */
    static final String SERVICE_UUID = "0000fcd2-0000-1000-8000-00805f9b34fb";

    private static final int OBJ_PACKET_ID   = 0x00;  // uint8
    private static final int OBJ_BATTERY     = 0x01;  // uint8,  %
    private static final int OBJ_TEMPERATURE = 0x02;  // sint16 x0.01 C
    private static final int OBJ_HUMIDITY    = 0x03;  // uint16 x0.01 %
    private static final int OBJ_VOLTAGE     = 0x0C;  // uint16 x0.001 V
    private static final int OBJ_POWER       = 0x5C;  // sint32 x0.01 W
    private static final int OBJ_CURRENT     = 0x5D;  // sint16 x0.001 A

    boolean valid;
    boolean complete;

    boolean havePacketId, haveBattery, haveTemperature, haveHumidity,
            haveVoltage, havePower, haveCurrent;

    int packetId;
    int batteryPct;
    float temperatureC;
    float humidityPct;
    float voltageV;
    /** Signed: negative is the van draining, positive is charging. */
    float powerW;
    /**
     * Signed, and the narrow one. BTHome's signed current is sint16 x0.001, so
     * it holds only +-32.767 A and the firmware drops it rather than clamping
     * when the pack goes past that. {@link #currentOrDerived()} is what
     * callers should use.
     */
    float currentA;

    private static int sizeOf(int id) {
        switch (id) {
            case OBJ_PACKET_ID:
            case OBJ_BATTERY:
                return 1;
            case OBJ_TEMPERATURE:
            case OBJ_HUMIDITY:
            case OBJ_VOLTAGE:
            case OBJ_CURRENT:
                return 2;
            case OBJ_POWER:
                return 4;
            default:
                return 0;  // unknown: a full stop, not a guess
        }
    }

    private static int u16(byte[] p, int i) {
        return (p[i] & 0xFF) | ((p[i + 1] & 0xFF) << 8);
    }

    private static int s16(byte[] p, int i) {
        return (short) u16(p, i);
    }

    private static int s32(byte[] p, int i) {
        return (p[i] & 0xFF) | ((p[i + 1] & 0xFF) << 8)
                | ((p[i + 2] & 0xFF) << 16) | ((p[i + 3] & 0xFF) << 24);
    }

    /**
     * @param data the service-data payload, i.e. everything after the two UUID
     *             bytes - exactly what Android's
     *             {@code ScanRecord.getServiceData()} hands back.
     */
    static BtHome decode(byte[] data) {
        BtHome out = new BtHome();
        if (data == null || data.length < 1) return out;

        // Device info byte: bit 0 is encryption, bits 5-7 are the version.
        int info = data[0] & 0xFF;
        if ((info & 0x01) != 0) return out;   // encrypted; we hold no key
        if ((info >> 5) != 2) return out;     // not BTHome v2

        out.valid = true;

        int i = 1;
        while (i < data.length) {
            int id = data[i] & 0xFF;
            int size = sizeOf(id);
            if (size == 0) return out;                  // unknown id
            if (i + 1 + size > data.length) return out; // truncated

            int v = i + 1;
            switch (id) {
                case OBJ_PACKET_ID:
                    out.packetId = data[v] & 0xFF;
                    out.havePacketId = true;
                    break;
                case OBJ_BATTERY:
                    out.batteryPct = data[v] & 0xFF;
                    out.haveBattery = true;
                    break;
                case OBJ_TEMPERATURE:
                    out.temperatureC = s16(data, v) * 0.01f;
                    out.haveTemperature = true;
                    break;
                case OBJ_HUMIDITY:
                    out.humidityPct = u16(data, v) * 0.01f;
                    out.haveHumidity = true;
                    break;
                case OBJ_VOLTAGE:
                    out.voltageV = u16(data, v) * 0.001f;
                    out.haveVoltage = true;
                    break;
                case OBJ_POWER:
                    out.powerW = s32(data, v) * 0.01f;
                    out.havePower = true;
                    break;
                case OBJ_CURRENT:
                    out.currentA = s16(data, v) * 0.001f;
                    out.haveCurrent = true;
                    break;
                default:
                    return out;  // unreachable while sizeOf and this agree
            }
            i += 1 + size;
        }

        out.complete = true;
        return out;
    }

    /**
     * True when this frame came from the controller rather than from a
     * thermo-hygrometer. Identified by what it carries, not by its name or its
     * address: no temperature sensor broadcasts pack power or current, and a
     * name can be absent while an address is not portable to iOS.
     */
    boolean looksLikeTheVan() {
        return valid && (havePower || haveCurrent);
    }

    /**
     * Pack current, falling back to power / voltage when the direct field was
     * dropped for being out of range.
     *
     * This is a recovery of the same number, not an estimate from a second
     * measurement: the firmware publishes power as exactly voltage x current,
     * so the division gives back what the BMS reported. Returns NaN when
     * neither route is available.
     */
    float currentOrDerived() {
        if (haveCurrent) return currentA;
        if (havePower && haveVoltage && voltageV > 0.1f) {
            return powerW / voltageV;
        }
        return Float.NaN;
    }

    /** True when the figure above came from power / voltage. */
    boolean currentIsDerived() {
        return !haveCurrent && havePower && haveVoltage && voltageV > 0.1f;
    }

    static String toHex(byte[] b) {
        StringBuilder sb = new StringBuilder(b.length * 3);
        for (byte x : b) sb.append(String.format("%02X ", x));
        return sb.toString().trim();
    }
}
