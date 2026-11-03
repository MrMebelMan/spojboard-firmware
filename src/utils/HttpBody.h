#ifndef HTTPBODY_H
#define HTTPBODY_H

#if defined(MATRIX_PORTAL_M4)

#include <Arduino.h>
#include <ArduinoHttpClient.h>

// Status used when a 200 response body arrives incomplete or malformed
static const int HTTP_ERROR_BODY_INCOMPLETE = -100;

/**
 * Read the full response body after responseStatusCode(), decoding chunked transfer encoding.
 * Reads raw bytes from the client because HttpClient::read(buf, size) ignores chunk framing.
 */
bool readHttpBody(HttpClient &http, Client &client, String &body, unsigned long idleTimeoutMs);

#endif // MATRIX_PORTAL_M4

#endif // HTTPBODY_H
