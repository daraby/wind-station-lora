# Field Checklist — Wind Station

## Bench
- [ ] ESP32 boots and prints serial logs
- [ ] LoRa init OK
- [ ] SD init OK and wind.csv appends
- [ ] Gateway receiver logs packets on UDP 1700
- [ ] InfluxDB receives data
- [ ] Grafana shows graphs

## Roof
- [ ] Outdoor 868 MHz antenna mounted high
- [ ] Coax short and good quality
- [ ] Pi stable power and internet
- [ ] Measure RSSI/SNR

## Beach
- [ ] IP67 sealed, cable glands tight
- [ ] Vent plug installed
- [ ] Antenna mounted high on mast
- [ ] Wind sensor free of obstacles
- [ ] Solar mounted correctly
- [ ] Verify 60 min continuous data, no seq gaps
