package com.campercontrol.monitor;

import android.Manifest;
import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanRecord;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;
import android.view.Gravity;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Reads the van's BTHome broadcast and shows it. There is no connection: this
 * only listens, which is the whole design - see docs/ROADMAP.md design gap 2.
 * A passive listener spends none of the firmware's three BLE connection slots,
 * needs no pairing, and cannot change anything on the van.
 *
 * The UI is built in code rather than from layout XML so the APK needs no
 * resource compilation, which is what lets it build from the SDK tools without
 * Gradle. See android/README.md.
 */
public class MainActivity extends Activity {

    private static final ParcelUuid BTHOME =
            ParcelUuid.fromString(BtHome.SERVICE_UUID);
    private static final ParcelUuid SWITCHBOT =
            ParcelUuid.fromString(SwitchBot.SERVICE_UUID);
    private static final int PERM_REQUEST = 1;
    /** Older than this and the reading is shown as stale, not as current. */
    private static final long STALE_MS = 15_000L;

    private TextView status, soc, voltage, power, current, temperature, detail;
    private LinearLayout sensorBox;
    private TextView sensorHeading;
    private final Handler ui = new Handler(Looper.getMainLooper());

    /**
     * Sensors heard over the air, keyed by the platform's opaque device id.
     * Written on the scan callback thread and read on the UI thread, so it is
     * synchronized on itself at both ends rather than being a plain HashMap.
     */
    private final Map<String, Sensor> sensors = new LinkedHashMap<>();

    /** One entry per sensor row on screen: {name, value, footer}. UI thread. */
    private final Map<String, TextView[]> sensorRows = new LinkedHashMap<>();

    private BluetoothLeScanner scanner;
    private boolean scanning;

    /**
     * The device the controller turned out to be, remembered once identified.
     *
     * Needed because the van is recognised by carrying pack power or current,
     * and for the first ten seconds after a reboot it carries neither - the
     * BMS has not connected yet, so it advertises a frame with no readings at
     * all. Without this, that frame looks exactly like an anonymous BTHome
     * sensor and the controller lists itself. Observed on a Pixel 8 Pro,
     * 2026-10-18.
     */
    private volatile String vanKey;

    // Written on the scan callback thread, read on the UI thread.
    private volatile BtHome last;
    private volatile long lastMs;
    private volatile int lastRssi;
    private volatile String lastHex = "";
    /** The ICD version the controller last advertised; -1 when unknown. */
    private volatile int wireVersion = -1;
    private volatile String lastDevice = "";
    private volatile int frames;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        setContentView(buildUi());

        BluetoothManager bm = getSystemService(BluetoothManager.class);
        BluetoothAdapter adapter = bm == null ? null : bm.getAdapter();
        if (adapter == null) {
            status.setText("This device has no Bluetooth adapter.");
            return;
        }
        if (!adapter.isEnabled()) {
            status.setText("Bluetooth is off. Turn it on and reopen the app.");
            return;
        }
        scanner = adapter.getBluetoothLeScanner();

        ui.post(tick);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (scanner == null) return;
        if (checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN)
                != PackageManager.PERMISSION_GRANTED) {
            status.setText("Waiting for the Nearby devices permission…");
            requestPermissions(
                    new String[]{Manifest.permission.BLUETOOTH_SCAN},
                    PERM_REQUEST);
            return;
        }
        startScan();
    }

    @Override
    protected void onPause() {
        super.onPause();
        stopScan();
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] perms,
                                           int[] granted) {
        if (code != PERM_REQUEST) return;
        if (granted.length > 0 && granted[0] == PackageManager.PERMISSION_GRANTED) {
            startScan();
        } else {
            status.setText("Nearby devices permission denied - cannot scan.\n"
                    + "Grant it in Settings > Apps > Camper Control.");
        }
    }

    // ---- scanning -----------------------------------------------------------

    private void startScan() {
        if (scanning || scanner == null) return;
        // Deliberately unfiltered, and filtered in onScanResult instead. A
        // ScanFilter on service *data* needs a data prefix and mask to match,
        // and getting that wrong fails silently - which on a radio is an
        // afternoon. Background scanning will need a real filter; a foreground
        // MVP does not. See android/README.md.
        ScanSettings settings = new ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .setCallbackType(ScanSettings.CALLBACK_TYPE_ALL_MATCHES)
                .build();
        try {
            scanner.startScan(null, settings, callback);
            scanning = true;
            status.setText("Scanning for the van…");
        } catch (SecurityException e) {
            status.setText("Scan refused: " + e.getMessage());
        }
    }

    private void stopScan() {
        if (!scanning || scanner == null) return;
        try {
            scanner.stopScan(callback);
        } catch (SecurityException ignored) {
            // Permission revoked while running; nothing useful to do.
        }
        scanning = false;
    }

    private final ScanCallback callback = new ScanCallback() {
        @Override
        public void onScanResult(int type, ScanResult result) {
            ScanRecord rec = result.getScanRecord();
            if (rec == null) return;
            long now = System.currentTimeMillis();
            String name = rec.getDeviceName();
            // An opaque key, not a MAC to be shown or stored. See Sensor.key.
            String key = result.getDevice().getAddress();

            Map<ParcelUuid, byte[]> sd = rec.getServiceData();

            // ---- BTHome: either the controller or a BTHome sensor ----
            byte[] bthome = sd == null ? null : sd.get(BTHOME);
            if (bthome != null) {
                BtHome parsed = BtHome.decode(bthome);
                if (parsed.valid) {
                    if (parsed.looksLikeTheVan()) vanKey = key;
                    if (key.equals(vanKey)) {
                        wireVersion = Icd.versionFromName(name);
                        lastDevice = name == null ? "(unnamed)" : name;
                        last = parsed;
                        lastMs = now;
                        lastRssi = result.getRssi();
                        lastHex = BtHome.toHex(bthome);
                        frames++;
                        // It may already have been filed as a sensor
                        // during the boot window above.
                        synchronized (sensors) {
                            sensors.remove(key);
                        }
                    } else {
                        Sensor sn = sensorFor(key, name, "BTHome", now,
                                result.getRssi());
                        if (parsed.haveTemperature) {
                            sn.mergeTemp(parsed.temperatureC);
                        }
                        if (parsed.haveHumidity) {
                            sn.mergeHum(parsed.humidityPct);
                        }
                        if (parsed.haveBattery) {
                            sn.mergeBattery(parsed.batteryPct);
                        }
                    }
                }
            }

            // ---- SwitchBot: a reading split across two advertisements ----
            // Temperature and humidity arrive in manufacturer data, battery in
            // service data, so each is folded in on its own and neither wipes
            // the other.
            byte[] mfr = rec.getManufacturerSpecificData(SwitchBot.COMPANY_ID);
            if (mfr != null) {
                // Android strips the company id; the decoder's offsets count
                // it, exactly as the firmware's do. Putting it back is what
                // keeps the two mirrors indexed identically - see SwitchBot.
                byte[] full = new byte[mfr.length + 2];
                full[0] = (byte) (SwitchBot.COMPANY_ID & 0xFF);
                full[1] = (byte) ((SwitchBot.COMPANY_ID >> 8) & 0xFF);
                System.arraycopy(mfr, 0, full, 2, mfr.length);

                SwitchBot sb = SwitchBot.decodeManufacturer(full);
                if (sb.valid) {
                    Sensor sn = sensorFor(key, name, "SwitchBot", now,
                            result.getRssi());
                    if (sb.haveTemp) sn.mergeTemp(sb.temperatureC);
                    if (sb.haveHum) sn.mergeHum(sb.humidityPct);
                }
            }

            byte[] sbService = sd == null ? null : sd.get(SWITCHBOT);
            if (sbService != null) {
                int pct = SwitchBot.decodeServiceBattery(sbService);
                if (pct >= 0) {
                    Sensor sn = sensorFor(key, name, "SwitchBot", now,
                            result.getRssi());
                    sn.mergeBattery(pct);
                }
            }
        }

        @Override
        public void onScanFailed(int errorCode) {
            ui.post(() -> status.setText("Scan failed, code " + errorCode));
        }
    };

    /** Scan-callback thread only. */
    private Sensor sensorFor(String key, String name, String kind, long now,
                             int rssi) {
        synchronized (sensors) {
            Sensor sn = sensors.get(key);
            if (sn == null) {
                sn = new Sensor(key);
                sensors.put(key, sn);
            }
            if (name != null && !name.isEmpty()) sn.advertisedName = name;
            sn.kind = kind;
            sn.lastSeenMs = now;
            sn.rssi = rssi;
            return sn;
        }
    }

    // ---- rendering ----------------------------------------------------------

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            render();
            ui.postDelayed(this, 500);
        }
    };

    private void render() {
        BtHome r = last;
        if (r == null) {
            soc.setText("--");
            voltage.setText("--");
            temperature.setText("--");
            power.setText("--");
            current.setText("--");
            detail.setText("Nothing heard from the controller yet.\n\nIt "
                    + "advertises every second or so. If this stays empty, "
                    + "check that it is powered and that CFG_BLE_BROADCAST "
                    + "is 1.");
            // Sensors broadcast for themselves, so show whatever is in
            // earshot whether or not the controller is up.
            renderSensors();
            return;
        }

        long age = System.currentTimeMillis() - lastMs;
        boolean stale = age > STALE_MS;

        // An unknown ICD version means a *breaking* change by definition -
        // the version only bumps when something already in the frame changed
        // meaning. So the numbers cannot be trusted, and showing them anyway
        // would be exactly the confident-wrong-number failure this project
        // refuses. A field we merely cannot read is the milder case and is
        // reported through `complete` instead. See docs/ICD.md.
        if (!Icd.compatible(wireVersion)) {
            soc.setText("--");
            voltage.setText("--");
            temperature.setText("--");
            power.setText("--");
            current.setText("--");
            status.setText(String.format(Locale.US,
                    "Controller speaks ICD v%d; this app speaks v%d.",
                    wireVersion, Icd.VERSION));
            detail.setText(String.format(Locale.US,
                    "Readings hidden on purpose.\n\nA version change means "
                    + "something in the frame changed meaning, so decoding it "
                    + "with v%d rules would show plausible wrong numbers.\n\n"
                    + "%s\n\nraw   %s",
                    Icd.VERSION,
                    wireVersion > Icd.VERSION
                            ? "The controller is newer. Update the app."
                            : "The app is newer. Update the controller.",
                    lastHex));
            renderSensors();
            return;
        }

        soc.setText(r.haveBattery
                ? String.format(Locale.US, "%d%%", r.batteryPct) : "--");
        voltage.setText(r.haveVoltage
                ? String.format(Locale.US, "%.2f V", r.voltageV) : "--");
        temperature.setText(r.haveTemperature
                ? String.format(Locale.US, "%.1f °C", r.temperatureC)
                : "--");

        // Signed, and the sign is the point: a leading + or - says charging or
        // draining at a glance, which is the question this screen exists for.
        power.setText(r.havePower
                ? String.format(Locale.US, "%+.0f W", r.powerW) : "--");

        float amps = r.currentOrDerived();
        current.setText(Float.isNaN(amps) ? "--"
                : String.format(Locale.US, "%+.1f A", amps));

        int colour = stale ? Color.parseColor("#C08A2E")
                           : Color.parseColor("#E8E8E8");
        soc.setTextColor(colour);
        voltage.setTextColor(colour);
        temperature.setTextColor(colour);
        power.setTextColor(colour);
        current.setTextColor(colour);

        renderSensors();

        status.setText(stale
                ? String.format(Locale.US,
                        "Last heard %.0f s ago - the van may be off.",
                        age / 1000.0)
                : String.format(Locale.US, "Live - %s, ICD v%d, %d dBm",
                        lastDevice,
                        wireVersion == -1 ? Icd.VERSION : wireVersion,
                        lastRssi));

        // A field the firmware dropped is shown as "--" rather than as a last
        // known value, which is the same choice the firmware makes when a
        // reading goes stale: an honest gap beats a confident wrong number.
        StringBuilder sb = new StringBuilder();
        sb.append("icd         ")
          .append(wireVersion == -1
                  ? "v" + Icd.VERSION + " assumed (no name in frame)"
                  : "v" + wireVersion)
          .append("\npacket id   ")
          .append(r.havePacketId ? String.valueOf(r.packetId) : "--")
          .append("\nframes      ").append(frames)
          .append("\nage         ")
          .append(String.format(Locale.US, "%.1f s", age / 1000.0))
          .append("\ncomplete    ").append(r.complete)
          .append("\ncurrent     ")
          .append(r.currentIsDerived() ? "from power / voltage" : "direct")
          .append("\nraw         ").append(lastHex);
        if (r.currentIsDerived()) {
            sb.append("\n\nThe pack is past BTHome's signed current field "
                    + "(\u00B132.767 A), so the controller dropped it rather "
                    + "than clamping. Current above is power divided by "
                    + "voltage, which recovers the same number.");
        }
        if (!r.complete) {
            sb.append("\n\nThe frame carried an object id this app does not "
                    + "know, so parsing stopped there. Values above are "
                    + "whatever was decoded before that point.");
        }
        detail.setText(sb.toString());
    }

    /**
     * Updates the sensor rows in place.
     *
     * **Deliberately not a rebuild.** The first version called
     * removeAllViews() and re-added every row on each 500 ms tick, which reset
     * the enclosing ScrollView's position continuously - the heading and the
     * first reading scrolled off and could not be scrolled back, because the
     * next tick undid it. Observed on a Pixel 8 Pro, 2026-10-18. Rows are
     * therefore created once, in discovery order, and only their text changes.
     *
     * Discovery order rather than freshest-first for the same reason:
     * re-sorting means re-ordering views, which is the same churn by another
     * name.
     */
    private void renderSensors() {
        List<Sensor> list;
        synchronized (sensors) {
            list = new ArrayList<>(sensors.values());
        }
        long now = System.currentTimeMillis();

        int shown = 0;
        for (Sensor sn : list) {
            // A broadcaster we can read nothing from is not worth a row. This
            // also catches the controller during its first seconds after a
            // reboot, before it has any readings to put in the frame.
            if (!sn.haveTemp && !sn.haveHum && !sn.haveBatt) continue;
            if (sn.key.equals(vanKey)) continue;
            shown++;

            TextView[] row = sensorRows.get(sn.key);
            if (row == null) {
                row = newSensorRow();
                sensorRows.put(sn.key, row);
            }

            boolean stale = sn.stale(now);
            row[0].setText(sn.displayName() + "   \u00b7   " + sn.kind);

            StringBuilder v = new StringBuilder();
            v.append(sn.haveTemp
                    ? String.format(Locale.US, "%.1f \u00b0C", sn.temperatureC)
                    : "--");
            if (sn.haveHum) {
                v.append(String.format(Locale.US, "   %.0f%%", sn.humidityPct));
            }
            row[1].setText(v.toString());
            row[1].setTextColor(stale ? Color.parseColor("#C08A2E")
                                      : Color.parseColor("#E8E8E8"));

            StringBuilder foot = new StringBuilder();
            if (sn.haveBatt) {
                foot.append(String.format(Locale.US, "battery %d%%   ",
                        sn.batteryPct));
            }
            foot.append(String.format(Locale.US, "%d dBm   %.0f s ago",
                    sn.rssi, (now - sn.lastSeenMs) / 1000.0));
            if (stale) foot.append("   (stale)");
            row[2].setText(foot.toString());
        }

        sensorHeading.setText(shown == 0
                ? "SENSORS \u2014 none heard yet"
                : String.format(Locale.US, "SENSORS \u2014 %d", shown));
    }

    /** Builds one sensor row and attaches it. Called once per sensor. */
    private TextView[] newSensorRow() {
        TextView name = new TextView(this);
        name.setTextSize(13);
        name.setTextColor(Color.parseColor("#8A93A0"));

        TextView value = new TextView(this);
        value.setTextSize(26);
        value.setTypeface(Typeface.DEFAULT_BOLD);
        value.setTextColor(Color.parseColor("#E8E8E8"));

        TextView sub = new TextView(this);
        sub.setTextSize(11);
        sub.setTypeface(Typeface.MONOSPACE);
        sub.setTextColor(Color.parseColor("#6F7885"));

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.VERTICAL);
        row.setPadding(0, 0, 0, dp(16));
        row.addView(name);
        row.addView(value);
        row.addView(sub);
        sensorBox.addView(row);

        return new TextView[]{name, value, sub};
    }

    // ---- a layout, without a layout file ------------------------------------

    private ViewGroup buildUi() {
        int pad = dp(20);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.parseColor("#14161A"));
        root.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("Camper Control");
        title.setTextSize(22);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setTextColor(Color.parseColor("#E8E8E8"));
        title.setPadding(0, 0, 0, dp(2));
        root.addView(title);

        status = new TextView(this);
        status.setTextSize(14);
        status.setTextColor(Color.parseColor("#8A93A0"));
        status.setPadding(0, 0, 0, dp(18));
        root.addView(status);

        root.addView(reading("STATE OF CHARGE", soc = big()));
        root.addView(reading("PACK POWER", power = big()));
        root.addView(reading("PACK CURRENT", current = big()));
        root.addView(reading("PACK VOLTAGE", voltage = big()));
        root.addView(reading("BATTERY TEMPERATURE", temperature = big()));

        // Sensors are read straight off the air, not relayed by the
        // controller - see Sensor's class comment.
        sensorHeading = new TextView(this);
        sensorHeading.setTextSize(11);
        sensorHeading.setLetterSpacing(0.12f);
        sensorHeading.setTextColor(Color.parseColor("#6F7885"));
        sensorHeading.setPadding(0, dp(12), 0, dp(12));
        root.addView(sensorHeading);

        sensorBox = new LinearLayout(this);
        sensorBox.setOrientation(LinearLayout.VERTICAL);
        root.addView(sensorBox);

        detail = new TextView(this);
        detail.setTextSize(12);
        detail.setTypeface(Typeface.MONOSPACE);
        detail.setTextColor(Color.parseColor("#8A93A0"));
        detail.setPadding(0, dp(24), 0, 0);
        root.addView(detail);

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(Color.parseColor("#14161A"));
        scroll.setFillViewport(true);
        scroll.addView(root);
        scroll.setClipToPadding(false);

        // Edge-to-edge is not optional for an app targeting SDK 35 on Android
        // 15: the window already spans the status bar and the gesture bar, so
        // without this the first reading is drawn underneath the clock.
        // Found by screenshot on a Pixel 8 Pro, 2026-10-18 - it is invisible
        // in an emulator with no cutout.
        scroll.setOnApplyWindowInsetsListener((v, insets) -> {
            Insets bars = insets.getInsets(WindowInsets.Type.systemBars());
            v.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            return insets;
        });
        return scroll;
    }

    private TextView big() {
        TextView t = new TextView(this);
        t.setTextSize(40);
        t.setTextColor(Color.parseColor("#E8E8E8"));
        t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    private ViewGroup reading(String label, TextView value) {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(0, 0, 0, dp(18));

        TextView l = new TextView(this);
        l.setText(label);
        l.setTextSize(11);
        l.setLetterSpacing(0.12f);
        l.setTextColor(Color.parseColor("#6F7885"));
        l.setGravity(Gravity.START);

        box.addView(l);
        box.addView(value);
        return box;
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }
}
