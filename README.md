# Wind Station (Beach Node + Rooftop Gateway) — LoRa P2P + InfluxDB + Grafana

Complete project for an off-grid beach wind station:
- Sampling every 10 seconds
- Aggregation to 1-minute values
- LoRa P2P uplink (868.3 MHz)
- Store & Forward to SD with ACK + backfill
- InfluxDB time-series storage
- Grafana dashboards

## Structure
- `hardware/bom/` — BOM Excel (prices + purchase links)
- `hardware/wiring/` — wiring notes
- `firmware/esp32/` — ESP32 Arduino firmware
- `gateway/docker/` — docker-compose for InfluxDB + Grafana + receiver
- `gateway/grafana/` — dashboard JSON
- `docs/` — deployment, checklist, calibration
- `images/` — concept images

## Quick Start (Gateway)
1. Copy `gateway/docker/` to Raspberry Pi: `/opt/wind/`
2. Edit `docker-compose.yml` tokens/passwords
3. Run:
   ```bash
   docker compose up -d
   docker compose logs -f receiver
   ```
4. Grafana: `http://<pi-ip>:3000`

## Quick Start (ESP32)
Flash `firmware/esp32/wind_station_esp32.ino` using Arduino IDE + ESP32 support.
Calibrate speed/direction functions for your exact sensor model.

## Important
For a real SX1302 concentrator you typically run a packet forwarder/basic station that outputs UDP to port 1700.
