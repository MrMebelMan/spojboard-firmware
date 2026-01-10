# Changelog

All notable changes to SpojBoard firmware will be documented in this file.

## [r3] - 2026-01-10

### Added
- **Demo Mode**: Standalone demo mode with customizable sample departures
  - Web interface to edit sample departures (line, destination, ETA, A/C status)
  - Available in both AP and STA modes
- **Smart ETA Updates**: 10-second ETA recalculation from cached timestamps
  - Reduces API calls by 6x while keeping display fresh
  - Allows longer refresh intervals (up to 300s)
- **Telnet Logging**: Remote debugging via telnet (port 23)
  - Enable via debugMode config field in settings
- **Extended Departure Buffer**: Increased from 6 to 12 departures
  - Provides 8-12 minute buffer during peak times
- **Condensed Font Support**: Automatic font switching for long destinations
  - DepartureMonoCondensed5pt8b for destinations >16 chars (23 char capacity)
- **API Retry Logic**: Improved error handling and user feedback
- **Factory Reset**: Reset to defaults via settings page
- **Custom Line Colors**: User-configurable color mapping system
  - Pattern matching with trailing asterisk (e.g., "9*=CYAN")

### Changed
- **API Optimization**: Always fetch MAX_DEPARTURES (12) per stop for optimal caching
  - Increased JSON buffer to 12KB for busy stops
- **Web UI Improvements**
- **Display Improvements**
- **Logging**: All Serial.println() converted to debugPrintln() for consistent logging

### Fixed
- **OTA Handler**: Split handleUpdateUpload into separate progress/complete handlers
- **Stack Overflow**: Made tempDepartures static (~2KB moved off stack)
- **API Query**: minDepartureTime now queried in API, removed unnecessary filtering in parser
- **Font Metrics**: Fixed xAdvance values in DepartureMono5pt8b

## [r2] - 2026-01-09

### Fixed
- Enable HTTP redirect following for GitHub asset downloads

## [r1] - Initial Release

Initial public release of SpojBoard firmware.
