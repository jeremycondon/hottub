# Monitoring (optional Grafana support)

The main firmware (`firmware/hottub/`) exposes spa and device metrics at
`http://hottub.local/metrics` in Prometheus text exposition format. Point a
Prometheus server at it, then use Grafana with a Prometheus data source to
build dashboards. There's no built-in dashboard JSON to import — the metric
names below are enough to build panels directly.

This is entirely optional and off the RS485 critical path: the HTTP server
only runs in the main loop, never in the interrupt/callback path.

## Enabling / disabling

Metrics are built in by default. To leave them out of a build (e.g. to save
flash, or if you don't run Prometheus), compile with:

```sh
arduino-cli compile --profile hottub --build-property build.extra_flags=-DENABLE_METRICS=0 .
```

or edit the `ENABLE_METRICS` define at the top of `hottub.ino`.

## Metrics exposed

| Metric | Type | Notes |
|---|---|---|
| `hottub_current_temp_fahrenheit` | gauge | Omitted while the temp sensor reading is unknown (spa mid-cycle) |
| `hottub_set_temp_fahrenheit` | gauge | Target temperature |
| `hottub_pump1_speed` | gauge | 0=off, 1=low, 2=high |
| `hottub_pump2` | gauge | 0/1 |
| `hottub_light` | gauge | 0/1 |
| `hottub_circulation_pump` | gauge | 0/1 |
| `hottub_heating` | gauge | 0/1 |
| `hottub_high_range` | gauge | 0/1 |
| `hottub_armed` | gauge | 1 if RS485 transmit is armed (see main `README.md`) |
| `hottub_spa_data_stale` | gauge | 1 if no RS485 status frame has been seen recently |
| `hottub_chip_temp_celsius` | gauge | ESP32-S3 die temperature; omitted before the first poll |
| `hottub_wifi_rssi_dbm` | gauge | WiFi signal strength |
| `hottub_uptime_seconds` | counter | Seconds since boot |

## Prometheus scrape config

```yaml
scrape_configs:
  - job_name: hottub
    scrape_interval: 30s
    static_configs:
      - targets: ["hottub.local:80"]
```

## Grafana

1. Add a Prometheus data source pointing at your Prometheus server.
2. Build panels from the metrics above, e.g.:
   - Time series: `hottub_current_temp_fahrenheit`, `hottub_set_temp_fahrenheit`
   - State timeline: `hottub_heating`, `hottub_pump1_speed`, `hottub_pump2`
   - Stat/alert: `hottub_spa_data_stale` — alert if `1` for more than a few
     scrape intervals, which usually means the RS485 bus stopped delivering
     status frames.
   - Stat: `hottub_chip_temp_celsius` — cross-check against the `[warn]`
     enclosure-temperature log line emitted over telnet/serial.
