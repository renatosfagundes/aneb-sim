# lcd_hello — I2C 1602 LCD demo

Drives a 16x2 character LCD over I2C through the PCF8574 backpack at
address 0x27. Demonstrates the path that replaced the earlier
`Serial.println("__LCD__...")` UART convention.

## Build

Requires `arduino-cli` and the `marcoschwartz/LiquidCrystal_I2C` library:

```bash
arduino-cli core install arduino:avr
arduino-cli lib install "LiquidCrystal I2C"
arduino-cli compile --fqbn arduino:avr:nano \
                    --output-dir build \
                    firmware/examples/lcd_hello
cp build/lcd_hello.ino.hex firmware/examples/lcd_hello.hex
```

## Run

In the simulator UI, click "Load firmware..." for any ECU and pick
`firmware/examples/lcd_hello.hex`. The widget will show:

```
Hello, ANEB!
RTOS @ UFPE  000   <- counter ticks every 500 ms
```
