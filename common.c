/*
 * ============================================================================
 * common.c - Implementacion de funciones compartidas
 * ============================================================================
 *
 * Este archivo contiene la implementacion de las funciones de utilidad
 * declaradas en common.h. Estas funciones son usadas por todos los modulos.
 * ============================================================================
 */

#include "common.h"

/* ============================================================================
 * generarPayload - Genera datos aleatorios para el payload del sensor
 * ============================================================================ */

void generarPayload(char* buffer, DWORD bufferSize, SensorType type) {
    /* Inicializar con ceros */
    memset(buffer, 0, bufferSize);

    /* Generar datos basados en el tipo de sensor */
    switch (type) {
        case SENSOR_MOTOR:
            /* Simular datos del motor: RPM, temperatura, presion */
            buffer[0] = (char)((rand() % 8000) + 1000);        /* RPM bajo */
            buffer[1] = (char)((rand() % 200) + 50);            /* Temperatura */
            buffer[2] = (char)((rand() % 100) + 20);           /* Presion aceite */
            buffer[3] = (char)((rand() % 50));                  /* Combustible */
            /* Llenar el resto con datos pseudoaleatorios */
            for (DWORD i = 4; i < bufferSize; i++) {
                buffer[i] = (char)(rand() % 256);
            }
            break;

        case SENSOR_NEMATICOS:
            /* Simular datos de neumaticos: presion, temperatura */
            buffer[0] = (char)((rand() % 50) + 25);             /* Presion PSI */
            buffer[1] = (char)((rand() % 100) + 20);           /* Temperatura */
            buffer[2] = (char)((rand() % 10));                 /* Desgaste % */
            buffer[3] = (char)((rand() % 4));                  /* Posicion (0-3) */
            for (DWORD i = 4; i < bufferSize; i++) {
                buffer[i] = (char)(rand() % 256);
            }
            break;

        case SENSOR_FRENOS:
            /* Simular datos de frenos: temperatura, desgaste */
            buffer[0] = (char)((rand() % 300) + 50);           /* Temperatura disco */
            buffer[1] = (char)((rand() % 100));                /* Porcentaje de uso */
            buffer[2] = (char)((rand() % 20));                 /* Desgaste (%) */
            buffer[3] = (char)((rand() % 2));                  /* ABS activo */
            for (DWORD i = 4; i < bufferSize; i++) {
                buffer[i] = (char)(rand() % 256);
            }
            break;

        case SENSOR_GPS:
            /* Simular datos GPS: lat, lon, altitud, velocidad */
            /* Coordenadas ficticias cerca de Venezuela */
            buffer[0] = (char)((rand() % 10) + 6);              /* Grados latitud */
            buffer[1] = (char)((rand() % 100));                /* Decimales lat */
            buffer[2] = (char)(-((rand() % 10) + 60));         /* Grados longitud */
            buffer[3] = (char)((rand() % 100));               /* Decimales lon */
            buffer[4] = (char)((rand() % 500));                /* Altitud (m) */
            buffer[5] = (char)((rand() % 300));                /* Velocidad (km/h) */
            for (DWORD i = 6; i < bufferSize; i++) {
                buffer[i] = (char)(rand() % 256);
            }
            break;

        default:
            /* Datos completamente aleatorios */
            for (DWORD i = 0; i < bufferSize; i++) {
                buffer[i] = (char)(rand() % 256);
            }
            break;
    }
}

/* ============================================================================
 * imprimirEvento - Imprime un evento en formato legible
 * ============================================================================ */

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

/* ============================================================================
 * obtenerNombreSensor - Retorna el nombre textual del tipo de sensor
 * ============================================================================ */

const char* obtenerNombreSensor(SensorType type) {
    switch (type) {
        case SENSOR_MOTOR:
            return "MOTOR";
        case SENSOR_NEMATICOS:
            return "NEMATICOS";
        case SENSOR_FRENOS:
            return "FRENOS";
        case SENSOR_GPS:
            return "GPS";
        default:
            return "DESCONOCIDO";
    }
}

/* ============================================================================
 * obtenerNombrePrioridad - Retorna el nombre textual de la prioridad
 * ============================================================================ */

const char* obtenerNombrePrioridad(EventPriority priority) {
    switch (priority) {
        case PRIORITY_LOW:
            return "BAJA";
        case PRIORITY_NORMAL:
            return "NORMAL";
        case PRIORITY_HIGH:
            return "ALTA";
        case PRIORITY_CRITICAL:
            return "CRITICA";
        default:
            return "DESCONOCIDA";
    }
}

/* ============================================================================
 * obtenerTimestampActual - Obtiene timestamp de alta resolucion
 * ============================================================================ */

void obtenerTimestampActual(LARGE_INTEGER* timestamp) {
    /* QueryPerformanceCounter proporciona alta resolucion */
    QueryPerformanceCounter(timestamp);
}
