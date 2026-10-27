# Capturing real CN105 frames

How to record the dialogue between an **official Mitsubishi CN105 device** (MAC-567IF, MAC-577IF-E, MAC-587IF / MELCloud Wi-Fi interface) and the indoor unit, then turn it into annotated frame files for the tests and the simulator.

Wired remotes (PAR-xx, PAC-YT…) use a different bus, not CN105; this guide does not apply to them. IR remote changes are visible indirectly, through what the official module polls.

## 1. Tap the CN105 line (Y-cable)

CN105 pinout (see README): 1 = 12V, 2 = GND, 3 = 5V, 4 = TX (from the indoor unit), 5 = RX (to the indoor unit). Connector: JST PA, 2.0 mm, 5 pins, housing `PAP-05V-S`.

```
Indoor unit CN105 ────────────────── Official module
  pin 2 GND  ──────┬──────────────────
  pin 4 TX   ──────┼──┬───────────────   (indoor unit → module)
  pin 5 RX   ──────┼──┼──┬────────────   (module → indoor unit)
                   │  │  │
                  GND  A  B    ← sniffer inputs only
```

All five wires go straight through from the indoor unit to the module; the sniffer only taps GND, pin 4 and pin 5.

Rules:

1. **Listen only.** Never connect a sniffer output/TX to the CN105 lines.
2. **5V signals.** ESP32 GPIOs are not 5V tolerant: use a 10 kΩ series / 20 kΩ to GND divider or a level shifter per line. A logic analyzer must accept 5V inputs.
3. **Power off the indoor unit while wiring.** Leave pin 1 (12V) alone. Power the sniffer separately (USB) with a common GND.

## 2. Option A — ESP32 sniffer (ESPHome)

Flash [`examples/sniffer/cn105-sniffer.yaml`](../examples/sniffer/cn105-sniffer.yaml): two receive-only UARTs (GPIO25 = pin 4, GPIO26 = pin 5), each logged with its direction and the device `millis()`.

```bash
esphome run examples/sniffer/cn105-sniffer.yaml
esphome logs examples/sniffer/cn105-sniffer.yaml | tee captures/2026-10-27-cool-22.log
```

The "Capture marker" button in Home Assistant writes a `MARK` line, handy to tag each action you perform during a session.

Timestamps are taken when a chunk is flushed (about 10 ms after its last byte), so latencies are approximate (± a few ms) but both directions share one time base.

## 3. Option B — logic analyzer (sigrok / PulseView)

Most precise option: exact timing, parity errors visible, no firmware.

**Hardware** — any sigrok-supported analyzer with 5V-tolerant inputs, 2 channels are enough:

- Cheap "24 MHz 8-channel" analyzers (Cypress FX2 based, `fx2lafw` driver), e.g. the SparkFun USB Logic Analyzer 24MHz/8-channel (5.25V max input, 2.0V high threshold).
- Saleae Logic 8 (commercial, own Logic 2 software; digital inputs protected to ±25V, 5V listed as a supported IO standard).

Connect D0 → pin 4 (indoor unit TX), D1 → pin 5 (module TX), GND → pin 2.

**Capture** (1 MHz is plenty for 2400 baud; `--time` is in ms):

```bash
sigrok-cli -d fx2lafw --config samplerate=1m --channels D0,D1 --time 600000 -o capture.sr
```

Or record with PulseView and save as `.sr`.

**Decode** — one file per direction, with sample numbers:

```bash
sigrok-cli -i capture.sr -P uart:rx=D0:baudrate=2400:parity=even -A uart=rx-data --protocol-decoder-samplenum > hp.txt
sigrok-cli -i capture.sr -P uart:rx=D1:baudrate=2400:parity=even -A uart=rx-data --protocol-decoder-samplenum > mod.txt
```

Older sigrok releases call the option `parity_type`; check with `sigrok-cli -P uart --show`.

## 4. Convert to annotated frames

[`tools/cn105_capture.py`](../tools/cn105_capture.py) (Python 3, standard library only) re-frames the byte streams (chunks may split or merge frames), validates checksums, labels each frame and merges both directions in time order:

```bash
# ESP32 sniffer logs
python3 tools/cn105_capture.py captures/2026-10-27-cool-22.log --stats -o cool-22.cn105

# logic analyzer
python3 tools/cn105_capture.py --sigrok-hp hp.txt --sigrok-mod mod.txt --samplerate 1e6 --stats
```

Output (`--format jsonl` for JSON lines):

```
2000.0 MOD FC 42 01 30 10 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 7B  # GET:settings
2110.0 HP FC 62 01 30 10 02 00 00 00 01 1C 00 00 00 00 03 A7 00 00 00 00 94  # GET_RESP:settings
```

`--stats` prints frame counts, checksum errors, the period between identical requests (the official module's polling cadence) and request → response latencies.

If the sigrok output format of your version is not recognised (expected lines: `<start>-<end> <decoder>: [<class>: ]<HH>`), open an issue with the first lines of `hp.txt`.

## 5. What to capture

One short session per scenario, noting the indoor unit model and each action:

- power-up and module connection;
- several minutes idle (polling cadence, which info codes the official module asks for);
- from MELCloud and from the IR remote: on/off, each mode, setpoint, fan, vanes;
- MELCloud timers — the only source for `0x05` timer frames (FEAT-06);
- a defrost cycle if possible; unplug/replug the module.

## References

- ESPHome UART bus, `debug:` block: <https://esphome.io/components/uart/>
- sigrok-cli manual: <https://manpages.debian.org/bookworm/sigrok-cli/sigrok-cli.1.en.html>
- sigrok UART decoder options (source): <https://github.com/sigrokproject/libsigrokdecode/blob/master/decoders/uart/pd.py>
- fx2lafw driver (cheap FX2 analyzers): <https://sigrok.org/wiki/Fx2lafw>
- SparkFun USB Logic Analyzer 24MHz/8-channel: <https://www.sparkfun.com/usb-logic-analyzer-24mhz-8-channel.html>
- Using it with PulseView (SparkFun tutorial): <https://learn.sparkfun.com/tutorials/using-the-usb-logic-analyzer-with-sigrok-pulseview/all>
- Saleae supported voltages: <https://support.saleae.com/user-guide/supported-voltages>
- JST PAP-05V-S housing: <https://octopart.com/part/jst/PAP-05V-S>
