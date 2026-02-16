## Pin Connections

### TMC2209 Stepper Driver
| ESP32-S3 Pin | TMC2209 Connection | Description |
|--------------|-------------------|-------------|
| GPIO 5       | STEP               | Step signal |
| GPIO 6       | DIR                | Direction signal |
| GPIO 4       | EN                 | Enable pin (optional) |
| GPIO 17      | PDN_UART (TX)      | UART TX to TMC2209 |
| GPIO 18      | PDN_UART (RX)      | UART RX from TMC2209 |
| GND          | GND                | Ground |
| 5V/3.3V      | VIO                | Logic power (check TMC2209 requirements) |

**Note:** TMC2209 UART uses a single-wire bidirectional interface. Connect both RX and TX to the PDN_UART pin on the TMC2209.

### SSD1306 OLED Display (128x64, I2C)
| ESP32-S3 Pin | OLED Pin   | Description |
|--------------|------------|-------------|
| GPIO 10      | SDA        | I2C Data    |
| GPIO 11      | SCL        | I2C Clock   |
| 3.3V         | VCC        | Power       |
| GND          | GND        | Ground      |

**Note:** I2C address is 0x3C. Add 4.7kΩ pull-up resistors to SDA and SCL if not present on the OLED module.
