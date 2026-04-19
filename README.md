# macbook-charge-limit

Set an Intel MacBook battery charge limit from Linux.

The tool writes the Apple SMC `BCLM` and `BFCL` keys. It is intended for Intel
MacBooks where those keys exist, but Linux does not expose a standard
`charge_control_end_threshold` battery setting.

## Supported Hardware

Tested on:

- Apple MacBookAir6,2
- Linux 7.0.0-1-cachyos

The program refuses to run unless DMI reports `Apple Inc.` and a product name
starting with `MacBook`. It does not expose arbitrary SMC writes.

## Get The Source

Clone the repository:

```sh
git clone https://github.com/jtbrough/macbook-charge-limit.git
cd macbook-charge-limit
```

Build:

```sh
make
```

## Use Without Installing

Read the current SMC values:

```sh
sudo ./macbook-charge-limit read
```

Set a limit:

```sh
sudo ./macbook-charge-limit set 80
```

Reset to full charge:

```sh
sudo ./macbook-charge-limit reset
```

Valid limits are `20..100`.

## Install

```sh
sudo make install
sudo systemctl daemon-reload
sudo systemctl enable --now macbook-charge-limit.service
```

Installed files:

```text
/usr/local/bin/macbook-charge-limit
/etc/macbook-charge-limit.conf
/etc/systemd/system/macbook-charge-limit.service
```

`make install` does not overwrite an existing config file.

## Configure

Edit:

```text
/etc/macbook-charge-limit.conf
```

Example:

```ini
LIMIT=80
```

Apply a config change:

```sh
sudo systemctl restart macbook-charge-limit.service
```

Check the configured value:

```sh
sudo macbook-charge-limit read
```

Expected output for an 80% limit:

```text
BCLM=80 BFCL=80
```

## Verify Charging Behavior

The limit does not discharge a full battery down to the target. To verify it,
drain below the configured limit, plug in AC, and watch the battery stop near
the limit:

```sh
watch -n 10 'printf "AC="; cat /sys/class/power_supply/ADP1/online; for f in status capacity charge_now charge_full current_now voltage_now; do printf "%s=" "$f"; cat "/sys/class/power_supply/BAT0/$f"; done'
```

When the limit is honored, AC is connected, `current_now` is `0`, and `status`
is usually `Full` or `Not charging`.

## Update

```sh
git pull
make
sudo make install
sudo systemctl daemon-reload
sudo systemctl restart macbook-charge-limit.service
```

## Uninstall

Reset the SMC limit before removing the binary if you want normal 100% charging:

```sh
sudo macbook-charge-limit reset
```

Disable the service and remove installed files:

```sh
sudo systemctl disable --now macbook-charge-limit.service
sudo make uninstall
sudo systemctl daemon-reload
```

Remove the config if it is no longer needed:

```sh
sudo rm -f /etc/macbook-charge-limit.conf
```

## Notes

This is a userspace workaround. Native desktop integration depends on a kernel
driver exposing the standard power-supply threshold interface.
