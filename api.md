# Tank API Endpoints

## GET `/api/device_info`
This sends various details about the device, such as uptime, software version, board version, serial number, hostname, MAC and network info.

Response:
```json
{
  "uptime": 123456,
  "version": "0.0.1",
  "serial": "ABCD1234ABCD1234",
  "board_revision": "Rev4",
  "hostname": "openjbod",
  "mac_address": "00:11:22:33:44:55",
  "ip_address": "192.168.1.100",
  "subnet_mask": "255.255.255.0",
  "gateway": "192.168.1.1",
  "ip_method": "dhcp"
}
```

## GET `/api/status`
Returns a comprehensive system status. Used for the index page.

Response:
```json
{
  "temperature": {
    "ds18b20_temp": 25.5,
    "ds18b20_valid": true,
    "rp2040_temp": 28.2,
    "rp2040_valid": true
  },
  "fan": {
    "fan_rpm": 1200,
    "fan_percent": 50,
    "fan_fault": false
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
  }
}
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
Returns the temperature readings from available sensors. Currently the RP2040 built-in sensor and the DS18B20 onboard sensor.

Response:
```json
{
  "ds18b20_temp": 25.5,
  "ds18b20_valid": true,
  "rp2040_temp": 28.2,
  "rp2040_valid": true
}
```

## GET `/api/fan`
Returns the current fan status and RPM/PWM readings.

Response:
```json
{
  "fan_rpm": 1200,
  "fan_percent": 50,
  "fan_fault": false
}
```

## POST `/api/fan/set`
Sets the current fan speed.

Body:
```json
{
  "fan_percent": 75
}
```

Response:
```json
{
  "status": "success",
  "fan_percent": 75
}
```

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
    "fan_curve": [
      {"temperature": 20.0, "fan_percent": 30},
      {"temperature": 40.0, "fan_percent": 50},
      {"temperature": 60.0, "fan_percent": 70},
      {"temperature": 80.0, "fan_percent": 90},
      {"temperature": 100.0, "fan_percent": 100}
    ]
  }
}
```

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