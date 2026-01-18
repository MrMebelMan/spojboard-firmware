#ifndef CAPTIVEPORTAL_H
#define CAPTIVEPORTAL_H

#include <IPAddress.h>

// Platform-specific web server includes
#if defined(MATRIX_PORTAL_M4)
    #include <WiFiNINA.h>
    // M4 doesn't have DNSServer - captive portal is stubbed out
    typedef WiFiServer WebServerType;
#else
    #include <DNSServer.h>
    #include <WebServer.h>
    typedef WebServer WebServerType;
#endif

// ============================================================================
// Captive Portal Manager
// ============================================================================

#if defined(MATRIX_PORTAL_M4)
// Stub implementation for M4 - no captive portal support
class CaptivePortal
{
public:
    CaptivePortal() : active(false) {}
    ~CaptivePortal() {}

    bool begin(IPAddress apIP) { (void)apIP; return false; }
    void stop() {}
    void processRequests() {}
    void setupDetectionHandlers(WebServerType* server) { (void)server; }
    bool isActive() const { return false; }

private:
    bool active;
    IPAddress apIP;
};

#else
// Full ESP32 implementation with DNS captive portal

/**
 * Manages DNS server and captive portal detection for AP mode.
 * Redirects all DNS queries to the AP IP address and handles
 * captive portal detection endpoints for various operating systems.
 */
class CaptivePortal
{
public:
    CaptivePortal();
    ~CaptivePortal();

    /**
     * Start DNS server for captive portal
     * @param apIP IP address of the access point
     * @return true if DNS server started successfully
     */
    bool begin(IPAddress apIP);

    /**
     * Stop DNS server
     */
    void stop();

    /**
     * Process DNS requests (call from main loop)
     */
    void processRequests();

    /**
     * Register captive portal detection handlers with web server
     * @param server WebServer instance to register handlers with
     */
    void setupDetectionHandlers(WebServerType* server);

    /**
     * Check if captive portal is active
     */
    bool isActive() const { return active; }

private:
    DNSServer dnsServer;
    bool active;
    IPAddress apIP;

    static const byte DNS_PORT = 53;

    // Captive portal detection handler
    static void handleCaptivePortalRedirect(WebServerType* server, IPAddress apIP);
};

#endif // MATRIX_PORTAL_M4

#endif // CAPTIVEPORTAL_H
