# WiFi MultiDrive – Secure Wireless File Sharing Using ESP32

WiFi MultiDrive is an ESP32-based wireless file-sharing prototype with a browser-based interface for user registration, authentication, and file management through a local Wi-Fi access point. Files are stored on a MicroSD card connected to the ESP32.

## Features

- ESP32 creates its own Wi-Fi access point.
- Browser-based interface for file sharing.
- User registration and login.
- Salted SHA-256 password hashing.
- Time-limited session management.
- File upload and download.
- Administrator controls for user and file management.
- MicroSD storage over SPI.

## Hardware Requirements

- ESP32 development board
- MicroSD card module and compatible MicroSD card
- Jumper wires
- USB cable and computer for programming

## Software and Libraries

- Arduino IDE with ESP32 board support
- `WiFi.h`
- `WebServer.h`
- `DNSServer.h`
- `SPI.h`
- `SD.h`
- `mbedtls/md.h` for SHA-256 hashing

## SPI Connections

The firmware is configured for these ESP32 SPI pins:

| MicroSD module | ESP32 pin |
|---|---:|
| SCK / CLK | GPIO 18 |
| MISO / DO | GPIO 19 |
| MOSI / DI | GPIO 23 |
| CS | GPIO 5 |
| VCC | Use the voltage supported by your SD module |
| GND | GND |

Check your module specifications before wiring. Use a filesystem supported by the firmware.

## Getting Started

1. Install the Arduino IDE and ESP32 board support.
2. Connect the MicroSD module using the pin mapping above.
3. Open `FINAL_SEVER.ino` in Arduino IDE.
4. Review the Wi-Fi settings and administrator credentials in the source code.
5. **Before using or publishing the project, replace default credentials and hard-coded secrets. Do not commit real passwords to a public repository.**
6. Select the correct ESP32 board and serial port.
7. Upload the firmware.
8. Open Serial Monitor at **115200 baud** and review the startup messages.
9. Connect a phone or computer to the Wi-Fi network configured in the firmware.
10. Open the ESP32 access-point IP address shown in Serial Monitor in a browser.

> The exact network name, password, administrator account, and access-point address depend on your local firmware settings. Do not publish private credentials in this README.

## How It Works

1. The ESP32 starts a Wi-Fi access point and HTTP web server.
2. A user registers or logs in through the web interface.
3. The firmware verifies credentials and creates an authenticated session.
4. Authenticated users can browse files and upload or download them.
5. Administrator routes provide additional user and file-management operations.
6. Files are stored on the MicroSD card.

## Project Structure

```text
WiFi-MultiDrive-ESP32/
├── FINAL_SEVER.ino
├── README.md
├── Project_Report.pdf       # Optional: add your report
└── images/                  # Optional: add circuit and prototype photos
```

## Current Status and Troubleshooting

This is an academic prototype. Confirm that the SD card initializes successfully before testing file operations. If Serial Monitor reports **“SD Card Mount Failed”**, file operations may not work until the issue is resolved.

Troubleshooting checklist:

- Verify SCK, MISO, MOSI, and CS wiring.
- Check the SD module's power and logic-level compatibility.
- Try a known-good, supported MicroSD card; back up its contents before formatting.
- Confirm the chip-select pin in the code matches the wiring.
- Review startup and SD initialization messages in Serial Monitor.

Add performance figures and successful end-to-end test results only after measuring them on the actual hardware.

## Security Notes

- Change default usernames and passwords before demonstration.
- Do not publish passwords, session tokens, private user data, or credential hashes.
- This prototype uses HTTP without TLS encryption. Use it in a controlled test environment unless transport security is added.
- SHA-256 alone is not a password-specific key-derivation function. For production use, consider a password-hashing scheme designed for this purpose.
- Review authorization checks for every file operation before storing sensitive files.

## Future Enhancements

- Resolve and validate MicroSD initialization on the target hardware.
- Add comprehensive upload/download and multi-user tests.
- Measure transfer speed, response time, and operation success rate.
- Improve password storage and transport security.
- Add storage usage indicators, audit logs, and stronger file validation.

## Authors

- **Dhanush B**
- **R. Vaishnavi**

**Department:** Artificial Intelligence  
**Institution:** SRM Institute of Science and Technology, Ramapuram, Chennai

## Disclaimer

This project is intended for academic and educational purposes. Test it in a controlled environment and do not use it to store sensitive data without further security review.
