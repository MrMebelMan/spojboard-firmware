#ifndef TELNETLOGGER_H
#define TELNETLOGGER_H

#include <stdint.h>

// ============================================================================
// Telnet Logger - Remote serial monitoring over WiFi
// ============================================================================

#if defined(MATRIX_PORTAL_M4)
// Stub implementation for M4 - telnet not supported
class TelnetLogger
{
public:
    static TelnetLogger& getInstance()
    {
        static TelnetLogger instance;
        return instance;
    }

    bool begin(uint16_t port = 23) { (void)port; return false; }
    void loop() {}
    void print(const char* message) { (void)message; }
    void println(const char* message) { (void)message; }
    bool isActive() { return false; }
    bool hasClients() { return false; }
    void end() {}

private:
    TelnetLogger() {}
};

#else
// Full ESP32 implementation
#include <ESPTelnet.h>

/**
 * Singleton telnet logger for remote debugging
 * Mirrors Serial output to telnet clients when debug mode is enabled
 */
class TelnetLogger
{
public:
    /**
     * Get singleton instance
     */
    static TelnetLogger& getInstance();

    /**
     * Initialize telnet server
     * @param port Telnet port (default: 23)
     * @return true if started successfully
     */
    bool begin(uint16_t port = 23);

    /**
     * Process telnet client connections (call in loop)
     */
    void loop();

    /**
     * Print message to telnet clients (if any connected)
     * @param message Message to send
     */
    void print(const char* message);

    /**
     * Print message with newline to telnet clients
     * @param message Message to send
     */
    void println(const char* message);

    /**
     * Check if telnet server is running
     */
    bool isActive();

    /**
     * Check if any clients are connected
     */
    bool hasClients();

    /**
     * Stop telnet server
     */
    void end();

private:
    TelnetLogger();
    TelnetLogger(const TelnetLogger&) = delete;
    TelnetLogger& operator=(const TelnetLogger&) = delete;

    ESPTelnet telnet;
    bool active;
};

#endif // MATRIX_PORTAL_M4

#endif // TELNETLOGGER_H
