#include "HttpBody.h"

#if defined(MATRIX_PORTAL_M4)

// Returns 1 if the chunked body is complete, 0 if more data is needed, -1 if malformed.
// Appends the decoded data to out when it is not null.
static int walkChunks(const String &raw, String *out)
{
    unsigned int pos = 0;
    while (true)
    {
        int eol = raw.indexOf("\r\n", pos);
        if (eol < 0)
            return 0;

        const char *sizeStart = raw.c_str() + pos;
        char *sizeEnd;
        unsigned long size = strtoul(sizeStart, &sizeEnd, 16);
        if (sizeEnd == sizeStart)
            return -1;

        unsigned int dataStart = eol + 2;
        if (size == 0)
            return 1;
        if (raw.length() < dataStart + size + 2)
            return 0;

        if (out)
            *out += raw.substring(dataStart, dataStart + size);
        pos = dataStart + size + 2;
    }
}

bool readHttpBody(HttpClient &http, Client &client, String &body, unsigned long idleTimeoutMs)
{
    int contentLen = http.contentLength();
    bool chunked = http.isResponseChunked();
    if (contentLen > 0)
        body.reserve(contentLen + 1);

    char buffer[512];
    unsigned long lastData = millis();
    while (true)
    {
        if (contentLen > 0 && (int)body.length() >= contentLen)
            break;
        if (chunked && walkChunks(body, nullptr) != 0)
            break;

        int avail = client.available();
        if (avail > 0)
        {
            int toRead = avail < (int)sizeof(buffer) - 1 ? avail : (int)sizeof(buffer) - 1;
            int bytesRead = client.read((uint8_t *)buffer, toRead);
            if (bytesRead > 0)
            {
                buffer[bytesRead] = '\0';
                body += buffer;
                lastData = millis();
            }
        }
        else if (!client.connected() || millis() - lastData >= idleTimeoutMs)
        {
            break;
        }
        else
        {
            delay(1);
        }
    }

    if (chunked)
    {
        String decoded;
        decoded.reserve(body.length());
        if (walkChunks(body, &decoded) != 1)
            return false;
        body = decoded;
        return true;
    }
    if (contentLen > 0)
        return (int)body.length() >= contentLen;
    return body.length() > 0;
}

#endif // MATRIX_PORTAL_M4
