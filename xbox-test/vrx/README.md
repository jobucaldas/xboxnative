# VRX allocation probe

The committed console log records a historical Xbox Series S run of three native
cases: direct RWX allocation, ordinary RW-to-RX protection, and the FromApp
RW-to-RX path. To validate the log's expected case outcomes offline:

```sh
node xbox-test/vrx/tests/review-vrx-log.mjs xbox-test/vrx/results/console-vrx.log
```

This checks log text only. A successful review does not establish that the log
is fresh, identify the device, or prove a new device or guest execution.
