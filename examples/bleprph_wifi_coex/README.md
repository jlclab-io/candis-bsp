# Candis Wi-Fi and BLE coexistence example

This example combines a Wi-Fi station with a NimBLE GATT server. It scans for access points while BLE is advertising, joins the configured access point, performs an HTTP request, and then waits for a GATT client to write a short payload.

## Configure

Set the access point and HTTP endpoint before building:

```sh
idf.py --preview -C examples/bleprph_wifi_coex menuconfig
```

Open **Candis Wi-Fi and BLE coexistence example** and set:

| Option | Purpose |
| --- | --- |
| `EXAMPLE_ESP_WIFI_SSID` | Access point SSID |
| `EXAMPLE_ESP_WIFI_PASSWORD` | Access point password |
| `EXAMPLE_ESP_MAXIMUM_RETRY` | Station reconnect attempts |
| `EXAMPLE_HTTP_URL` | HTTP endpoint used for the internet check |

The defaults use a plain HTTP endpoint, so the certificate bundle is disabled to keep the application within the standard 1.5 MiB application partition.

## Run

Build the example and follow [USB and flashing](../../docs/en/usb.md). The application console runs on the native USB-C OTG port.

```sh
idf.py --preview -C examples/bleprph_wifi_coex build
```

The startup log reports `CANDIS_COEX_WIFI_SCAN` followed by `CANDIS_COEX_INTERNET`. BLE advertises as `Candis-Coex` with service `0xFF00` and data characteristic `0xFF01`.

On a Linux host, scan and connect with `bluetoothctl`. Discover the value handle with `gatttool --characteristics`, then write a short payload to characteristic `0xFF01`:

```sh
gatttool -b <device-address> -t public \
  --char-write-req -a <value-handle> -n 43414e444953
```

The device logs the received byte count and prints the final `CANDIS_COEX_COMPLETE` result after both the HTTP request and BLE write finish.

When Bluetooth is enabled, ESP-IDF requires the default active-scan timing. The example performs the Wi-Fi scan while BLE advertising is active and avoids an additional scan after the BLE connection.
