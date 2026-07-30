# Native Zigbee casement-window opener

Standalone ESP-IDF firmware for:

- Seeed Studio XIAO ESP32-C6
- DRV8871
- ordinary 12 V brushed DC motor
- passive 3-pin quadrature encoder (`COMMON`, `A`, `B`)
- two normally-open manual buttons
- one normally-open mechanical slip/end/obstruction switch
- 12 V to 5 V buck converter

It creates a Zigbee Home Automation **Window Covering** endpoint on endpoint 1,
cluster `0x0102`, supporting:

- Open
- Close
- Stop
- Go To Lift Percentage
- Current lift-percentage reporting
- Operational-status reporting

## Safety model

There is deliberately no motor-current or encoder-stall safety detection.

The physical V-gear/slip mechanism is the end-stop and obstruction mechanism.
Every rising edge from the slip switch immediately sets both DRV8871 inputs LOW.
The direction that caused the trip remains blocked. Only reverse motion is allowed
until the switch releases.

The encoder is used only for position.

## Wiring

See `wiring.svg`.

### XIAO pin map

| XIAO | GPIO | Connection |
|---|---:|---|
| D0 | 0 | DRV8871 IN1 |
| D1 | 1 | DRV8871 IN2 |
| D2 | 2 | Encoder A |
| D3 | 21 | Encoder B |
| D4 | 22 | OPEN button |
| D5 | 23 | CLOSE button |
| D6 | 16 | Slip switch |

Encoder `COMMON`, both button returns and the slip-switch return connect to GND.
The inputs use pull-ups. The encoder has no VCC connection.

## Power

Set the buck to **5.0 V before connecting the XIAO**.

- Supply +12 V -> fuse -> DRV8871 VM/POWER+
- Supply +12 V -> fuse -> buck IN+
- Supply GND -> DRV8871 GND/POWER-
- Supply GND -> buck IN-
- Buck 5 V OUT+ -> XIAO 5V
- Buck OUT- -> XIAO GND
- DRV8871 MOTOR1/MOTOR2 -> motor

Use a common ground.

Add:

- 470–1000 µF electrolytic plus 100 nF ceramic at DRV8871 VM/GND
- 100 nF ceramic directly across the motor
- suitable fuse in the +12 V lead
- external 4.7–10 kΩ pull-ups on long/noisy encoder and switch lines

## Boot behavior

1. Motor outputs initialize off.
2. If the slip switch is active, move OPEN only until it releases.
3. Move CLOSE until the switch activates.
4. Stop immediately and reset encoder count to zero.
5. Enter normal operation.

Because one switch cannot distinguish a true endpoint from a persistent
obstruction, the path must be clear during boot homing.

## Manual controls

- Hold OPEN: open.
- Release: stop.
- Hold CLOSE: close.
- Release: stop.
- Hold both for 2 seconds while stopped and switch released: save current encoder
  count as 100% open.
- Hold both for 10 seconds: erase NVS/Zigbee network state and reboot.

After a slip trip, press the opposite direction until the switch releases.

## Build

The supported build environment is ESP-IDF 5.5.4 targeting the ESP32-C6. Install
that ESP-IDF release, activate its environment in the current shell, then run from
the repository root:

```bash
source /path/to/esp-idf/export.sh
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

On Windows, activate ESP-IDF with its supplied PowerShell or Command Prompt script
and replace the serial port with something like `COM5`.

If a local ESP-IDF installation is not available, push a branch or manually run
the **Build firmware** workflow in GitHub Actions. It uses Espressif's pinned
ESP-IDF 5.5.4 container and produces a downloadable flashable-firmware artifact.

This project pins the legacy/stable Espressif Zigbee component family used by its
source:

- `esp-zigbee-lib 1.6.8`
- `esp-zboss-lib 1.6.4`

Do not silently upgrade to 2.x: Espressif replaced and renamed the Zigbee API in
2.x. Port `zigbee_window.c` deliberately if upgrading.

## Pairing with Home Assistant ZHA

1. Start “Add device” in ZHA.
2. Flash or factory-reset the opener.
3. The device starts network steering.
4. It should interview as a Window Covering device with cluster `0x0102`.
5. Complete physical homing, then calibrate 100% open with both buttons.
6. Test Open, Close, Stop and several percentage positions.

## Pairing with Zigbee2MQTT

The standard endpoint should normally be recognized as a cover. If the coordinator
does not automatically expose it, add an external converter matching endpoint 1
and Window Covering cluster `0x0102`. The firmware itself still uses standard ZCL
commands and attributes.

## Commissioning warning

Bench-test with the motor mechanically disconnected first. Verify:

1. Both DRV8871 inputs remain LOW through reset.
2. OPEN and CLOSE move in the intended directions.
3. The slip switch stops both directions immediately.
4. The tripped direction is blocked.
5. Reverse movement releases the switch.
6. Boot homing closes rather than opens.
7. Calibration and percentage movement are correct.

If direction is reversed, change `MOTOR_DIRECTION_INVERTED` in `main/pins.h`.

## Build status

The complete ESP32-C6 firmware and the host-side state-machine tests build
successfully in GitHub Actions. The firmware workflow also verifies that the
bootloader, partition table and application image can be collected into a
downloadable artifact.

The repository includes three automation workflows under `.github/workflows/`.

### Continuous firmware build

`build.yml` runs on every push to `main`, every pull request, and manual dispatch.
It builds the project inside Espressif's ESP-IDF 5.5.4 container for the ESP32-C6
and uploads a downloadable workflow artifact containing:

- `bootloader.bin`
- `partition-table.bin`
- `zigbee_casement_window.bin`
- `flash_args`, when generated
- `flasher_args.json`, when generated
- `project_description.json`, when generated
- the resolved `sdkconfig`
- build commit information

Open the workflow run in GitHub and download the artifact from its **Artifacts**
section. Artifacts are retained for 30 days by the supplied workflow.

### State-machine tests

`test.yml` runs the host-side Python tests on pushes and pull requests. These tests
verify the central mechanical-safety rules, including immediate stop, same-direction
lockout after a trip, and restoration after switch release.

### Automated releases

`release.yml` runs whenever a tag beginning with `v` is pushed. It rebuilds the
firmware and creates a GitHub Release with ZIP and TAR.GZ firmware bundles attached.
The underlying firmware build path is exercised by the continuous build workflow;
publishing a release additionally requires pushing a version tag.

Create a release like this:

```bash
git tag -a v0.1.0 -m "Prototype firmware v0.1.0"
git push origin v0.1.0
```

GitHub Actions will then:

1. Build the ESP32-C6 firmware from that exact tag.
2. Package the application, bootloader, partition table, build metadata, README,
   and wiring diagram.
3. Generate release notes.
4. Attach both archive formats to the GitHub Release.

No repository secrets are required for these workflows. The release workflow uses
the repository-scoped `GITHUB_TOKEN` and declares `contents: write` permission.

### Branch protection recommendation

After the first successful runs, open the repository's branch-protection settings
and require these checks before merging into `main`:

- `Build firmware / build`
- `Test state machine / test`

This prevents pull requests that fail compilation or behavioral tests from being
merged.

### Updating ESP-IDF

The CI image is deliberately pinned to `espressif/idf:v5.5.4`, matching the Zigbee
SDK generation used by this source. Upgrade it intentionally in both workflow files,
then open a pull request and let CI expose any API incompatibilities before merging.
Do not change the image to `latest` for production releases.
