# Transport Display - Standalone (Golemio API)

A standalone ESP32-based transit departure display that fetches real-time departure data directly from Prague's Golemio API. No external server required!

## Features

- **Direct API Access**: Fetches departure data from Golemio PID API every 30 seconds (configurable)
- **WiFi Setup Portal**: Automatic AP mode with captive portal when WiFi fails - no code editing needed!
- **Web Configuration**: Easy setup via built-in web interface
- **Persistent Settings**: Configuration stored in ESP32 flash memory, survives reboots
- **Czech Character Support**: U8g2 fonts for proper display of Czech station names
- **Real-time Updates**: Shows line number, destination, and ETA in minutes
- **AC Indicator**: Shows asterisk (*) for air-conditioned vehicles
- **Color-coded Lines**: Different colors for metro, tram, bus, and night lines

## Hardware

- Adafruit MatrixPortal ESP32-S3
- 2x HUB75 64x32 LED Matrix panels (128x32 total)

## Quick Start

### First Time Setup (AP Mode)

1. **Flash the Firmware**
   ```bash
   pio run -t upload
   ```

2. **Connect to Setup AP**
   - The display will show WiFi credentials:
     ```
     WiFi Setup Mode
     SSID: TransportDisplay-XXXX
     Pass: xxxxxxxx
     Go to: 192.168.4.1
     ```
   - Connect your phone/computer to this WiFi network
   - A captive portal should open automatically (or go to http://192.168.4.1)

3. **Configure via Web UI**
   - Enter your home WiFi credentials
   - Enter your Golemio API key (get one at [api.golemio.cz/api-keys](https://api.golemio.cz/api-keys/))
   - Enter your stop ID(s)
   - Click "Save & Connect to WiFi"

4. **Device Connects**
   - The device will restart and connect to your WiFi
   - Find its new IP address on the display or your router
   - Departures will start showing automatically

### If WiFi Fails

If the device can't connect to WiFi (wrong password, network down, etc.), it will automatically:
1. Stop trying after 20 attempts (~10 seconds)
2. Create a new AP with a **random password**
3. Display the credentials on the LED matrix
4. Wait for you to configure new WiFi settings

This ensures you can always access the device for reconfiguration!

## Web Interface

The built-in web server provides:

- **Status Dashboard**: WiFi status, API status, memory usage
- **Configuration Form**: 
  - WiFi credentials
  - Golemio API key
  - Stop ID(s) - comma-separated for multiple stops
  - Refresh interval (10-300 seconds)
  - Number of departures to display
- **Actions**:
  - Force refresh
  - Reboot device

### AP Mode vs Normal Mode

| Feature | AP Mode | Normal Mode |
|---------|---------|-------------|
| WiFi | Creates its own network | Connects to your network |
| API Calls | Disabled | Every 30s (configurable) |
| Display | Shows AP credentials | Shows departures |
| Web UI | Setup focused | Full dashboard |

## Getting Your Stop ID

1. Visit [data.pid.cz/stops/xml/StopsByName.xml](http://data.pid.cz/stops/xml/StopsByName.xml)
2. Search for your stop name (Ctrl+F)
3. Find the `gtfsIds` value (e.g., `U693Z2P`)
4. You can enter multiple IDs separated by commas

## Display Layout

### Normal Mode
```
┌────────────────────────────────────┐
│ [31] Sídliště Řepy           12' │  Row 1
│ [7 ] Radlická                 5' │  Row 2  
│ [A ] Depo Hostivař            2' │  Row 3
│ Wed 08.Jan              14:35    │  Row 4 (Date/Time)
└────────────────────────────────────┘
```

### AP Setup Mode
```
┌────────────────────────────────────┐
│ WiFi Setup Mode                   │
│ SSID: TransportDisplay-A1B2       │
│ Pass: k7m3p9x2                    │
│ Go to: 192.168.4.1                │
└────────────────────────────────────┘
```

## Color Coding

### Line Numbers
- **Green**: Metro A, Tram 31
- **Yellow**: Metro B, Default trams
- **Red**: Metro C, Tram 8
- **Orange**: Tram 7
- **Purple**: Tram 12
- **Blue**: S-trains
- **Cyan**: Night trams (91-99)

### ETA Colors
- **White**: > 5 minutes
- **Yellow**: 2-5 minutes
- **Red**: < 2 minutes
- **Orange**: Delayed

## Troubleshooting

### Can't Connect to AP
- Make sure you're connecting to the right network (TransportDisplay-XXXX)
- The password is case-sensitive and shown on the display
- Try forgetting the network and reconnecting

### "API Error: HTTP 401"
- Invalid API key - check your key at api.golemio.cz

### "API Error: HTTP 429"
- Rate limited - increase refresh interval

### "No Departures"
- Check stop ID is correct
- Some stops may have no service at certain times

### Device Keeps Going to AP Mode
- Double-check your WiFi password
- Make sure your WiFi is 2.4GHz (ESP32-S3 doesn't support 5GHz)
- Check if your router is blocking new devices

### Display Issues
- Check HUB75 cable connections
- Verify panel chain order

## Technical Details

### AP Mode Credentials
- SSID: `TransportDisplay-XXXX` (XXXX = last 4 chars of MAC address)
- Password: 8 random alphanumeric characters (regenerated each time)
- IP: `192.168.4.1`

### Captive Portal
The device implements captive portal detection for:
- Android (`/generate_204`, `/gen_204`)
- iOS/macOS (`/hotspot-detect.html`)
- Windows (`/ncsi.txt`, `/connecttest.txt`)
- Firefox (`/success.txt`)

### Memory Usage
- JSON buffer: 8KB for API responses
- Configuration stored in NVS flash
- Typical free heap: ~200KB

## License

MIT License - Feel free to modify and share!

## Credits

- [Golemio API](https://api.golemio.cz) - Prague open data platform
- [ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-DMA) - Display driver
- [U8g2](https://github.com/olikraus/u8g2) - Font rendering with Czech support