/*
 * common.c - funciones de utilidad compartidas entre modulos
 */

#include "common.h"

/* --- generar payload aleatorio segun tipo de sensor --- */

void generarPayload(char* buffer, DWORD bufferSize, SensorType type) {
    memset(buffer, 0, bufferSize);

    switch (type) {
        case SENSOR_MOTOR:
            buffer[0] = (char)((rand() % 8000) + 1000);
            buffer[1] = (char)((rand() % 200) + 50);
            buffer[2] = (char)((rand() % 100) + 20);
            buffer[3] = (char)((rand() % 50));
            for (DWORD i = 4; i < bufferSize; i++)
                buffer[i] = (char)(rand() % 256);
            break;

        case SENSOR_NEMATICOS:
            buffer[0] = (char)((rand() % 50) + 25);
            buffer[1] = (char)((rand() % 100) + 20);
            buffer[2] = (char)((rand() % 10));
            buffer[3] = (char)((rand() % 4));
            for (DWORD i = 4; i < bufferSize; i++)
                buffer[i] = (char)(rand() % 256);
            break;

        case SENSOR_FRENOS:
            buffer[0] = (char)((rand() % 300) + 50);
            buffer[1] = (char)((rand() % 100));
            buffer[2] = (char)((rand() % 20));
            buffer[3] = (char)((rand() % 2));
            for (DWORD i = 4; i < bufferSize; i++)
                buffer[i] = (char)(rand() % 256);
            break;

        case SENSOR_GPS:
            buffer[0] = (char)((rand() % 10) + 6);
            buffer[1] = (char)((rand() % 100));
            buffer[2] = (char)(-((rand() % 10) + 60));
            buffer[3] = (char)((rand() % 100));
            buffer[4] = (char)((rand() % 500));
            buffer[5] = (char)((rand() % 300));
            for (DWORD i = 6; i < bufferSize; i++)
                buffer[i] = (char)(rand() % 256);
            break;

        default:
            for (DWORD i = 0; i < bufferSize; i++)
                buffer[i] = (char)(rand() % 256);
            break;
    }
}

/* --- imprimir evento para debug --- */

void imprimirEvento(const SensorEvent* event) {
    SYSTEMTIME st;
    FileTimeToSystemTime((FILETIME*)&event->timestamp, &st);

    printf("----------------------------------------\n");
    printf("  Evento ID:       %lu\n", event->eventId);
    printf("  Sensor ID:       %lu\n", event->sensorId);
    printf("  Tipo de Sensor:  %s\n", obtenerNombreSensor(event->sensorType));
    printf("  Prioridad:      %s\n", obtenerNombrePrioridad(event->priority));
    printf("  Timestamp:      %02d:%02d:%02d.%03d\n",
           st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    printf("  Tamano Payload: %lu bytes\n", event->payloadSize);
    printf("  Payload (hex):  %02X %02X %02X %02X ...\n",
           (unsigned char)event->payload[0],
           (unsigned char)event->payload[1],
           (unsigned char)event->payload[2],
           (unsigned char)event->payload[3]);
    printf("----------------------------------------\n");
}

/* --- nombre del sensor --- */

const char* obtenerNombreSensor(SensorType type) {
    switch (type) {
        case SENSOR_MOTOR:     return "MOTOR";
        case SENSOR_NEMATICOS: return "NEMATICOS";
        case SENSOR_FRENOS:    return "FRENOS";
        case SENSOR_GPS:       return "GPS";
        default:               return "DESCONOCIDO";
    }
}

/* --- nombre de la prioridad --- */

const char* obtenerNombrePrioridad(EventPriority priority) {
    switch (priority) {
        case PRIORITY_LOW:      return "BAJA";
        case PRIORITY_NORMAL:   return "NORMAL";
        case PRIORITY_HIGH:     return "ALTA";
        case PRIORITY_CRITICAL: return "CRITICA";
        default:                return "DESCONOCIDA";
    }
}

/* --- timestamp de alta resolucion --- */

void obtenerTimestampActual(LARGE_INTEGER* timestamp) {
    QueryPerformanceCounter(timestamp);
}
