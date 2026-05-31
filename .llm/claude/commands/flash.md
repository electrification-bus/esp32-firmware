# Flash ESP32 Device

Flash the build to the connected ESP32 device and start the serial monitor.

## Steps to perform:

1. **Stop any running serial monitor** - Kill any running PlatformIO device monitor process that may be holding the COM port

2. **Flash the build to the device** - Use PlatformIO to upload the firmware:
   ```
   pio run -t upload
   ```

3. **Start the serial monitor** - After flashing completes, start the serial monitor to see device output:
   ```
   pio device monitor
   ```

## Notes:
- The COM port must be released before flashing can succeed
- If upload fails, check that no other process is using the COM port
- Default baud rate for monitor is typically 115200 (configured in platformio.ini)
