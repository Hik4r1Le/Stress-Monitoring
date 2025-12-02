### 1. Wearable Unit (ESP32 + MAX30105 + SSD1306 OLED)

| Device              | Pin on Sensor / Module | ESP32 Pin | Notes                              |
|---------------------|------------------------|-----------|------------------------------------|
| MAX30105 (Pulse Ox) | VCC                    | 3.3V      |                                    |
|                     | GND                    | GND       |                                    |
|                     | SDA                    | GPIO 8    | Custom I2C (defined in code)       |
|                     | SCL                    | GPIO 10   | Custom I2C (defined in code)       |
| SSD1306 OLED        | VCC                    | 3.3V      |                                    |
|                     | GND                    | GND       |                                    |
|                     | SDA                    | GPIO 8    | Shares I2C bus with MAX30105       |
|                     | SCL                    | GPIO 10   | Shares I2C bus with MAX30105       |

> **Power**: ESP32 powered via USB or 5V → VIN pin (do **not** use the 3.3V pin as power input)

### 2. Desk Unit (ESP32-CAM + TFT Display)

| Device              | Pin on Module          | ESP32-CAM Pin | Notes                                      |
|---------------------|------------------------|---------------|--------------------------------------------|
| TFT Display         | VCC                    | 3.3V or 5V    | Most TFTs work fine with 3.3V              |
|                     | GND                    | GND           |                                            |
|                     | CS                     | GPIO 5        | Default in TFT_eSPI setup                  |
|                     | DC                     | GPIO 2        |                                            |
|                     | RST                    | GPIO 4 or -1  | Can be left floating (-1 in code)          |
|                     | MOSI                   | GPIO 23       |                                            |
|                     | SCK                    | GPIO 18       |                                            |
| Camera (OV2640)     | Built-in               | —             | No extra wiring needed                     |

> **Power**: ESP32-CAM must be powered via **5V VIN** or USB programmer (FTDI)