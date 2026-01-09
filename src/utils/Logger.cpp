#include "Logger.h"

void logTimestamp()
{
    char timestamp[24];
    sprintf(timestamp, "[%010lu] ", millis());
    Serial.print(timestamp);
}

void logMemory(const char *location)
{
    logTimestamp();
    Serial.print("MEM@");
    Serial.print(location);
    Serial.print(": Free=");
    Serial.print(ESP.getFreeHeap());
    Serial.print(" Min=");
    Serial.println(ESP.getMinFreeHeap());
}
