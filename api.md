# Tank API Endpoints

## Authentication

There are two ways to authenticate; **HTTP Basic auth has been removed**
(see `BASIC_AUTH_DEPRECATION.md`).

- **Session cookie (browser):** `POST /api/login` with `{username, password}` returns
  an `HttpOnly` `session` cookie (`SameSite=Strict`), sent automatically on subsequent
  requests. There is no `WWW-Authenticate` challenge, so browsers use the `/login`
  page instead of the native credential popup. Protected HTML pages requested without
  a session return `302 Found` → `/login`; protected API calls without auth return
  `401` (JSON, no challenge). Sessions are in-RAM and reset on reboot.
- **Bearer API token (scripts):** `Authorization: Bearer <token>`. Create/revoke
  tokens at `/tokens` (web UI) or `/api/tokens`; they persist across reboots and are
  stored hashed. This replaces `curl -u user:pass`.

Public endpoints (no auth): `/login`, `/api/login`, `/api/logout`. Every other
endpoint (including `/api/device_info`) requires authentication.

### POST/GET/DELETE `/api/tokens`
Manage bearer API tokens (auth required).
- `GET` → `[{"id": "ab12cd34", "label": "backup"}, ...]` (metadata only).
- `POST` `{"label": "..."}` → `{"success": true, "id": "ab12cd34", "token": "ab12cd34.<secret>"}`.
  The full token is returned **once**; only its hash is stored. Send it as
  `Authorization: Bearer ab12cd34.<secret>`.
- `DELETE` `{"id": "ab12cd34"}` → `{"success": true}` (revoke).

### POST `/api/login`
Body: `{"username": "...", "password": "..."}`. On success: `200` with a
`Set-Cookie: session=...; Path=/; HttpOnly; SameSite=Strict; Max-Age=86400` header
and `{"success": true}`. On bad credentials: `401` with
`{"success": false, "message": "invalid credentials"}`.

### POST `/api/logout`
Revokes the caller's session (by its cookie) and clears the cookie
(`Set-Cookie: session=; Max-Age=0`). Always returns `200 {"success": true}`.

## GET `/api/device_info`
Device identity + network info: software/firmware/bootloader versions, serial,
board revision, MAC, and IPv4 addressing.

Response:
```json
{
  "serial": "ABCD1234ABCD1234",
  "version": "0.9.9 (abc1234)",
  "firmware_version": "0.9.9",
  "bootloader_version": 1,
  "board_revision": "Rev4",
  "ip_address": "192.168.1.100",
  "subnet_mask": "255.255.255.0",
  "gateway": "192.168.1.1",
  "mac_address": "00:11:22:33:44:55"
}
```

`version` is the application version with the build's commit SHA; `firmware_version`
is the same application version (`OPENJBOD_VERSION_STRING`) without the SHA, updated
on every DFU. `bootloader_version` is the integer MCUboot bootloader revision
(`OPENJBOD_BOOTLOADER_VER`), bumped only when the bootloader changes (requires a
full re-flash, not a DFU update).

## GET `/api/status`
Returns a comprehensive system status. Used for the index page. Includes `uptime`
(milliseconds since boot) — the former standalone `/uptime` endpoint was merged here
to save the overview page a request.

Response:
```json
{
  "status": "success",
  "uptime": 123456,
  "temperature": {
    "valid": true,
    "active_source": "onboard",
    "ds18b20":     { "temperature": 31.5, "valid": true },
    "ds18b20_ext": { "temperature": 26.0, "valid": true, "present": true },
    "rp2040":      { "temperature": 36.3, "valid": true }
  },
  "fan": {
    "valid": true,
    "rpm": 1200,
    "speed_percent": 50,
    "fault": false
  },
  "device": {
    "serial": "ABCD1234ABCD1234",
    "version": "0.0.1",
    "board_revision": "1.0",
    "hostname": "tank-device"
  },
  "network": {
    "mac_address": "00:11:22:33:44:55",
    "ip_address": "192.168.1.100",
    "subnet_mask": "255.255.255.0",
    "gateway": "192.168.1.1",
    "ip_method": "dhcp"
  },
  "power": {
    "state": "off",
    "on_boot": false,
    "on_boot_delay": 0,
    "follow_usb": false,
    "follow_usb_delay": 5
  },
  "beacon": false
}
```

`beacon` reflects whether the locator beacon (see `POST /api/led`) is currently active.

## POST `/api/led`
Controls the **locator beacon**. The onboard status LED (`led0`) is otherwise
automatic — off at boot, blinking while awaiting an address, solid once the web
server is up. Enabling the beacon fast-blinks the LED to physically locate the unit,
overriding the status display until disabled. The beacon is not persisted (off after
reboot).

Body:
```json
{ "beacon": true }
```

## GET `/api/power`
This fetches the current power state.

Response:
```json
{
  "powered_on": true
}
```

## GET `/api/power/on`
Switches the ATX power supply on. Can be used regardless of state.

Response:
```json
{
  "status": "power_on",
  "result": "success"
}
```

## GET `/api/power/off`
Switches the ATX power supply on. Can be used regardless of state.

Response:
```json
{
  "status": "power_off",
  "result": "success"
}
```

## GET `/api/temp`
Returns the temperature readings from all available sensors: the onboard DS18B20
(GPIO18), the optional external DS18B20 on the GPIO11 pin header, and the RP2040
built-in die sensor.

`primary_source` is the configured preference (0 = onboard, 1 = header).
`active_source` is the probe actually used after fallback (onboard → header →
rp2040, depending on which are valid), and `active_temperature` is its reading.
`ds18b20_ext.present` reflects whether the header probe was detected at boot.

Response:
```json
{
  "status": "temp_reading",
  "primary_source": 0,
  "active_source": "onboard",
  "active_temperature": 31.5,
  "ds18b20":     { "temperature": 31.5, "valid": true,  "unit": "celsius" },
  "ds18b20_ext": { "temperature": 26.0, "valid": true,  "present": true, "unit": "celsius" },
  "rp2040":      { "temperature": 36.3, "valid": true,  "unit": "celsius" }
}
```

## GET `/api/fan`
Returns the fan controller (EMC2301) state: PWM output, tachometer reading and the
active tach configuration.

Response:
```json
{
  "status": "fan_reading",
  "pwm":  { "duty": 128, "percent": 50, "base_hz": 26000, "divide": 1, "effective_hz": 26000 },
  "fan":  { "rpm": 3540, "tach_count": 1110, "tach_valid": true,
            "fault": false, "stall": false, "spin_fail": false, "drive_fail": false,
            "watchdog": false, "status_reg": 0 },
  "tach": { "pulses_per_rev": 2, "edges": 5, "edges_auto": true, "range": 1, "min_rpm": 480 },
  "control": { "mode": "auto", "target_percent": 40, "min_drive_percent": 0 },
  "initialized": true
}
```

- `rpm` is derived from the 13-bit tach count as
  `(edges - 1) * 1966080 * range / (pulses_per_rev * tach_count)` (the EMC2301
  datasheet equation; with the datasheet edge count `2 * pulses_per_rev + 1` this is
  `3932160 * range / tach_count`). `tach_valid` is false (and `rpm` 0) when no tach
  signal is seen: fan stopped, absent, ATX power off, or turning slower than
  `min_rpm`. Sampling 3 edges lowers the floor (240 RPM for a 2-pulse fan) so quiet
  fans idling at 300 RPM still read; the characterization selects that automatically
  for fans below 2000 RPM at 30 % duty.
- `stall`, `spin_fail`, `drive_fail` and `watchdog` mirror the controller's status
  register (read-to-clear, so each flag is reported once per occurrence).
  `fault` is `stall || drive_fail`. A stall flag with no fan connected is normal.
- `control.mode` is `auto`, `external` or `calibrating`.

## POST `/api/fan/set`
Sets the fan drive directly. The body is form-encoded (not JSON):

```
percent=75
```
or
```
duty=191
```

`percent` is clamped to 0-100 and `duty` to 0-255. Automatic fan control overrides the
value within one update interval unless `use_external_fan_control` is enabled.

Response:
```json
{
  "status": "fan_set_success",
  "pwm": { "duty": 191, "percent": 75 },
  "note": "automatic control overrides this within one update interval unless use_external_fan_control is set"
}
```

## GET / POST `/api/fan/calibrate`
Best-effort fan characterization. The run executes in the fan-control thread and
takes up to two minutes: tach check at 50 %/100 %, a duty sweep from 30 % to 100 %,
a start scan from standstill (10-25 %), and, only if the response at the configured
PWM frequency is flat or non-monotonic, a short retry at the other base
frequencies. It then stores `fan_pwm_base_hz`, `fan_tach_edges` (3 for slow fans, else
auto), `fan_tach_range` (largest range whose floor is comfortably below the lowest
observed RPM), `fan_min_drive_percent`
(lowest starting duty + 5) and the `fan_cal_*` results in the environment settings
and applies them immediately. The previous duty is restored afterwards. Runs are
aborted on request, after 120 s, on any I2C error, or if the active temperature
exceeds 55 °C or rises more than 8 °C. Time spent below the fan curve's current
target is capped at 10 s.

Pulses per revolution cannot be measured (it only scales the RPM figure), so it is
never changed automatically; an implausible full-speed RPM yields `suggested_ppr`.

The fan must have power (ATX on). One run happens automatically after boot when no
result has ever been stored, once ATX power is on.

`POST` body (form-encoded, optional): `action=start` (default), `action=abort` or
`action=reset` (restore the fan hardware defaults: 26 kHz, divider 1, 2 pulses/rev,
edges auto, range x1, minimum drive 0, and clear the stored results).

`POST` responses: `202 {"status":"calibration_started"}`, `409 {"status":"calibration_running"}`,
`409 {"status":"temperature_too_high"}`, `503 {"status":"fan_controller_not_initialized"}`,
`200 {"status":"calibration_abort_requested"}`, `200 {"status":"fan_hw_reset"}`.

`GET` response:
```json
{
  "state": "done",
  "phase": "finalize",
  "progress_percent": 100,
  "elapsed_ms": 61200,
  "abort_reason": null,
  "result": {
    "tach_present": true,
    "monotonic": true,
    "atx_power_on": true,
    "budget_cutoff": false,
    "used_retry": false,
    "min_spin_percent": 15,
    "max_rpm": 1490,
    "rpm_at": [[30,520],[40,690],[50,860],[60,1010],[70,1160],[80,1300],[90,1410],[100,1490]],
    "chosen_pwm_base_hz": 26000,
    "chosen_edges": 3,
    "chosen_range": 1,
    "assumed_ppr": 2,
    "suggested_ppr": 2,
    "confidence": 2,
    "note": "ok: 520..1490 RPM, starts at 15%"
  }
}
```

`state` is one of `idle`, `pending`, `running`, `done`, `aborted`, `error`;
`abort_reason` one of `user`, `temperature`, `timeout`, `i2c`, `no_tach`, `settings`.
`confidence` is 0 (low), 1 (medium) or 2 (high).

## GET `/api/settings`
Gets the currently configured device settings.

Response:
```json
{
  "network": {
    "ip_method": "dhcp",
    "ip_addr": "192.168.1.100",
    "gw_addr": "192.168.1.1",
    "ip_mask": "255.255.255.0",
    "dns1": "8.8.8.8",
    "hostname": "tank-device"
  },
  "power": {
    "ignore_power_switch": false,
    "on_boot": true,
    "on_boot_delay": 5000,
    "follow_usb": true,
    "follow_usb_delay": 2000
  },
  "http": {
    "enable_http": true,
    "enable_https": true,
    "http_port": 80,
    "https_port": 443,
    "use_custom_certificates": false
  },
  "environment": {
    "use_external_fan_control": false,
    "fan_update_interval_ms": 5000,
    "fan_hysteresis_percent": 5,
    "primary_temp_source": 0,
    "fan_curve": [
      {"temperature": 20.0, "fan_percent": 30},
      {"temperature": 40.0, "fan_percent": 50},
      {"temperature": 60.0, "fan_percent": 70},
      {"temperature": 80.0, "fan_percent": 90},
      {"temperature": 100.0, "fan_percent": 100}
    ],
    "fan_pwm_base_hz": 26000,
    "fan_pwm_divide": 1,
    "fan_tach_pulses_per_rev": 2,
    "fan_tach_edges": 0,
    "fan_tach_range": 1,
    "fan_min_drive_percent": 0,
    "fan_cal_min_spin_percent": 15,
    "fan_cal_max_rpm": 1490
  },
  "console": {
    "uart_enabled": true,
    "usb_enabled": true
  }
}
```

`environment.primary_temp_source`: preferred temperature probe — `0` = onboard
DS18B20 (GPIO18), `1` = external/header DS18B20 (GPIO11). Fan control and the
reported `active_source` fall back automatically if the selected probe is invalid.

`console.uart_enabled` / `console.usb_enabled`: enable the hardware UART and/or
USB CDC-ACM shell/console. Both default `true`. Changes take effect on the next
reboot. Disabling the UART console also silences its log output; it is the
primary recovery console, so disable with care.

## POST `/api/settings`
This handles updating the device configuration. Note that even elements that are not actively used can be updated. For example, you can set `ip_addr` while `ip_method` is set to DHCP and it will still save.

You can post either the full settings JSON body, or just the elements you wish to update.

Example Body:
```json
{
  "network": {
    "data": {
      "ip_method": "static",
      "ip_addr": "192.168.1.50",
      "gw_addr": "192.168.1.1",
      "ip_mask": "255.255.255.0",
      "dns1": "8.8.8.8",
      "hostname": "ojbd1"
    }
  }
}
```

Response:
```json
{
  "status": "success",
  "message": "Settings updated successfully",
  "restart_required": true
}
```

## GET `/api/settings/backup`
Download the device's raw settings file (`settings.dat`) as a binary attachment —
a full backup of the configuration (network, power, users, API tokens, environment,
console, and any custom TLS certificate). Auth required.

- Response: `200`, `Content-Type: application/octet-stream`,
  `Content-Disposition: attachment; filename="settings.dat"`, body = the file.
- For automation: `curl -OJ -b <cookie> https://<host>/api/settings/backup`
  (or with `-H "Authorization: Bearer <token>"`).

## POST `/api/settings/restore`
Restore a configuration by uploading a `settings.dat` previously obtained from
`/api/settings/backup`. The body is the raw file (`Content-Type:
application/octet-stream`). The device writes it over the live settings file and
**reboots** to apply it. Auth required.

- Success: `200` `{"status":"restored","bytes":<n>,"message":"rebooting to apply"}`,
  then the device reboots (the connection drops).
- Errors: `400` (empty upload), `409` (another restore in progress), `500`
  (filesystem error).
- For automation: `curl -b <cookie> -H "Content-Type: application/octet-stream" \
  --data-binary @settings.dat https://<host>/api/settings/restore`.

## GET `/api/users`
Get the current user list. Note passwords are not returned.

```json
{
  "users": [
    {
      "username": "admin",
      "has_password": true
    },
    {
      "username": "user1", 
      "has_password": true
    }
  ]
}
```

## POST `/api/users`

Allows you to create/update/delete users.

Example Create:
```json
{
  "action": "create",
  "username": "newuser",
  "password": "securepassword"
}
```
Example Update:
```json
{
  "action": "update",
  "username": "newuser",
  "password": "securepassword1"
}
```
Example Delete:
```json
{
  "status": "success",
  "message": "User created successfully"
}
```

Response:
```json
{
  "status": "success",
  "message": "User created successfully"
}
```

## POST `/api/certificates/upload`
Allows you to upload custom SSL/TLS certificates for the HTTPS service. Must be enabled via Settings -> HTTP -> Use Custom Certificates after upload.

Uploads should use the `multipart/form-data` content type.
The form fields are:
- `certificate`: SSL certificate file (DER format)
- `private_key`: Private key (DER format)

Response:
```json
{
  "status": "success",
  "message": "Certificates uploaded and stored successfully",
  "certificate_size": 1234,
  "private_key_size": 567
}
```

## POST `/api/firmware`
Uploads a new signed MCUboot firmware image (raw body, `application/octet-stream`)
and applies it. The image is streamed directly into the secondary slot, marked
pending, and the device reboots into it; MCUboot swaps it in and the new firmware
confirms itself once the web server comes back up. If the new image fails to boot
healthy, MCUboot automatically reverts to the previous one on the next reboot.

The image is the `zephyr.signed.bin` produced by a signed (sysbuild + MCUboot)
build - see `make dfu-image VERSION=X.Y.Z` and `make dfu`.

Request body: the raw signed image bytes (not multipart).

Response (on success, just before the reboot):
```json
{
  "success": true,
  "bytes": 525364,
  "message": "rebooting to apply update"
}
```

Errors: `401` if unauthenticated, `409` if another upload is in progress, `500` if
the flash write or pending-flag write fails.