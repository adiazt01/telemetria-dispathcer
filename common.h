/*
 * common.h - Tipos y constantes compartidos entre modulos
 * Grupo 8 - Carlos Brito, Jesus Guzman, Armando Diaz, Jose Marquez
 */

#ifndef COMMON_H
#define COMMON_H

#define _WIN32_WINNT 0x0501

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * CONSTANTES
 * ============================================================================ */

#define BUFFER_NAME           "TelemetryBuffer"
#define MUTEX_NAME            "BufferMutex"
#define SEMAPHORE_SLOTS_NAME  "BufferSlots"
#define SEMAPHORE_DATA_NAME   "BufferData"
#define EVENT_DEBUG_NAME      "DebugEvent"
#define EVENT_SHUTDOWN_NAME   "ShutdownEvent"
#define PIPE_NAME             "\\\\.\\pipe\\TelemetryPipe"
#define BUFFER_SIZE           100
#define MAX_PAYLOAD_SIZE      256
#define TIMEOUT_MS            5000
#define WORKER_POOL_SIZE      4
#define LOG_FILE              "telemetry_log.txt"

/* --- tipos de sensor y prioridad --- */

typedef enum {
    SENSOR_MOTOR = 0,
    SENSOR_NEMATICOS = 1,
    SENSOR_FRENOS = 2,
    SENSOR_GPS = 3
} SensorType;

typedef enum {
    PRIORITY_LOW = 0,
    PRIORITY_NORMAL = 1,
    PRIORITY_HIGH = 2,
    PRIORITY_CRITICAL = 3
} EventPriority;

/* evento enviado del sensor al broker por el named pipe */
#pragma pack(push, 1)
typedef struct {
    DWORD eventId;
    DWORD sensorId;
    DWORD sensorType;
    LARGE_INTEGER timestamp;
    DWORD priority;
    DWORD payloadSize;
    char payload[MAX_PAYLOAD_SIZE];
} SensorEvent;
#pragma pack(pop)

/* buffer circular en memoria compartida */
typedef struct {
    volatile LONG writePos;
    volatile LONG readPos;
    volatile LONG availableSlots;
    volatile LONG availableData;
    volatile LONG eventsProcessed;
    volatile BOOL debugMode;
    volatile BOOL shutdownRequested;
    volatile LONG activeSensors;
    DWORD bufferSize;
    SensorEvent events[BUFFER_SIZE];
} CircularBuffer;

/* parametros para cada hilo worker del dispatcher */
typedef struct {
    int workerId;
    HANDLE hBufferMapping;
    HANDLE hMutex;
    HANDLE hSemSlots;
    HANDLE hSemData;
    HANDLE hLogFile;
    HANDLE hShutdownEvent;
} WorkerParams;

/* parametros para cada hilo receptor del broker */
typedef struct {
    int sensorId;
    HANDLE pipeHandle;
    HANDLE hBufferMapping;
    HANDLE hMutex;
    HANDLE hSemSlots;
    HANDLE hSemData;
} ReceiverParams;

/* --- funciones compartidas --- */

void generarPayload(char* buffer, DWORD bufferSize, SensorType type);
void imprimirEvento(const SensorEvent* event);
const char* obtenerNombreSensor(SensorType type);
const char* obtenerNombrePrioridad(EventPriority priority);
void obtenerTimestampActual(LARGE_INTEGER* timestamp);

#endif /* COMMON_H */
