# Calibration

## Wind Speed
Replace `speed_from_pulses()` using anemometer datasheet constant (Hz->m/s or km/h).

## Wind Direction
Replace `dir_from_adc()` with a lookup table mapping ADC steps to degrees.

## Battery
Add divider to BAT_ADC_PIN and implement `read_battery_mv()`.
