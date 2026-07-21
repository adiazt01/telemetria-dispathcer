/*
 * ============================================================================
 * PROYECTO: Sistema de Telemetría y Control Concurrente
 * GRUPO: 8 - Carlos Brito, Jesus Guzman, Armando Diaz, Jose Marquez
 * ASIGNATURA: Sistemas Operativos
 * ============================================================================
 *
 * common.h - Estructuras y constantes compartidas por todos los módulos
 *
 * Este archivo contiene las definiciones de estructuras, constantes y
 * funciones de utilidad comunes a todos los módulos del sistema.
 * ============================================================================
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
 * CONSTANTES DEL SISTEMA
 * ============================================================================ */

/* Nombre del buffer circular en memoria compartida */
#define BUFFER_NAME           "TelemetryBuffer"

/* Nombre del mutex para proteger el buffer circular */
#define MUTEX_NAME            "BufferMutex"

/* Nombre del semaforo de espacios disponibles (escritores) */
#define SEMAPHORE_SLOTS_NAME  "BufferSlots"

/* Nombre del semaforo de datos disponibles (lectores) */
#define SEMAPHORE_DATA_NAME   "BufferData"

/* Nombre del evento para que Monitor controle al Broker */
#define EVENT_DEBUG_NAME      "DebugEvent"

/* Nombre del evento de apagado */
#define EVENT_SHUTDOWN_NAME   "ShutdownEvent"

/* Nombre del pipe para cada sensor */
#define PIPE_NAME             "\\\\.\\pipe\\TelemetryPipe"

/* Tamano del buffer circular (numero de eventos) */
#define BUFFER_SIZE           100

/* Tamano maximo del payload de cada evento */
#define MAX_PAYLOAD_SIZE      256

/* Timeout para operaciones de espera (ms) */
#define TIMEOUT_MS            5000

/* Numero de workers en el pool del Dispatcher */
#define WORKER_POOL_SIZE      4

/* Nombre del archivo de log */
#define LOG_FILE              "telemetry_log.txt"

/* ============================================================================
 * DEFINICION DE TIPOS Y ESTRUCTURAS
 * ============================================================================ */

/*-----------------------------------------------------------------------------
 * Tipo de sensor (determina el tipo de dato que genera)
 *-----------------------------------------------------------------------------*/
typedef enum {
    SENSOR_MOTOR = 0,      /* Sensor del motor */
    SENSOR_NEMATICOS = 1,  /* Sensor de neumaticos */
    SENSOR_FRENOS = 2,     /* Sensor de frenos */
    SENSOR_GPS = 3         /* Sensor GPS de telemetria */
} SensorType;

/*-----------------------------------------------------------------------------
 * Nivel de prioridad del evento
 *-----------------------------------------------------------------------------*/
typedef enum {
    PRIORITY_LOW = 0,      /* Prioridad baja */
    PRIORITY_NORMAL = 1,   /* Prioridad normal */
    PRIORITY_HIGH = 2,     /* Prioridad alta */
    PRIORITY_CRITICAL = 3  /* Prioridad critica */
} EventPriority;

/*-----------------------------------------------------------------------------
 * Estructura de un evento de sensor
 *
 * Esta estructura representa un evento individual generado por un sensor.
 * Es enviada a traves del Named Pipe al Broker.
 *-----------------------------------------------------------------------------*/
#pragma pack(push, 1)
typedef struct {
    DWORD eventId;                 /* ID unico del evento (generado por el sensor) */
    DWORD sensorId;                /* ID del sensor (0-255) */
    DWORD sensorType;             /* Tipo de sensor (vease SensorType) */
    LARGE_INTEGER timestamp;       /* Marca de tiempo de alta resolucion */
    DWORD priority;               /* Prioridad del evento */
    DWORD payloadSize;            /* Tamano real del payload en bytes */
    char payload[MAX_PAYLOAD_SIZE]; /* Datos aleatorios del sensor */
} SensorEvent;
#pragma pack(pop)

/*-----------------------------------------------------------------------------
 * Estructura del buffer circular
 *
 * Esta estructura se aloja en memoria compartida (File Mapping).
 * Contiene el buffer circular propiamente dicho y las variables de control.
 *-----------------------------------------------------------------------------*/
typedef struct {
    /* Indice de la siguiente posicion de escritura */
    volatile LONG writePos;

    /* Indice de la siguiente posicion de lectura */
    volatile LONG readPos;

    /* Numero de ranuras disponibles para escribir */
    volatile LONG availableSlots;

    /* Numero de eventos disponibles para leer */
    volatile LONG availableData;

    /* Contador de eventos procesados total */
    volatile LONG eventsProcessed;

    /* Bandera para indicar si el sistema esta en modo debug */
    volatile BOOL debugMode;

    /* Bandera para indicar apagado ordenado */
    volatile BOOL shutdownRequested;

    /* Numero de sensores activos conectados */
    volatile LONG activeSensors;

    /* Tamano del buffer (numero de slots) */
    DWORD bufferSize;

    /* Los datos del buffer circular (arreglo de eventos) */
    SensorEvent events[BUFFER_SIZE];

} CircularBuffer;

/*-----------------------------------------------------------------------------
 * Estructura para parametros de thread de worker
 *-----------------------------------------------------------------------------*/
typedef struct {
    int workerId;                  /* ID del worker (0-3) */
    HANDLE hBufferMapping;         /* Handle al archivo de mapeo */
    HANDLE hMutex;                 /* Handle al mutex del buffer */
    HANDLE hSemSlots;              /* Handle al semaforo de slots */
    HANDLE hSemData;               /* Handle al semaforo de datos */
    HANDLE hLogFile;               /* Handle al archivo de log */
    HANDLE hShutdownEvent;         /* Handle al evento de apagado */
} WorkerParams;

/*-----------------------------------------------------------------------------
 * Estructura para parametros de thread receptor del Broker
 *-----------------------------------------------------------------------------*/
typedef struct {
    int sensorId;                  /* ID del sensor conectado */
    HANDLE hPipe;                  /* Handle al pipe de este sensor */
    HANDLE hBufferMapping;         /* Handle al archivo de mapeo */
    HANDLE hMutex;                 /* Handle al mutex del buffer */
    HANDLE hSemSlots;              /* Handle al semaforo de slots */
    HANDLE hSemData;               /* Handle al semaforo de datos */
} ReceiverParams;

/* ============================================================================
 * FUNCIONES DE UTILIDAD
 * ============================================================================ */

/*-----------------------------------------------------------------------------
 * generarPayload - Genera datos aleatorios para el payload del sensor
 *
 * Genera datos aleatorios simulando lectura de un sensor real.
 *-----------------------------------------------------------------------------*/
void generarPayload(char* buffer, DWORD bufferSize, SensorType type);

/*-----------------------------------------------------------------------------
 * imprimirEvento - Imprime un evento en formato legible (para debug)
 *-----------------------------------------------------------------------------*/
void imprimirEvento(const SensorEvent* event);

/*-----------------------------------------------------------------------------
 * obtenerNombreSensor - Retorna el nombre textual del tipo de sensor
 *-----------------------------------------------------------------------------*/
const char* obtenerNombreSensor(SensorType type);

/*-----------------------------------------------------------------------------
 * obtenerNombrePrioridad - Retorna el nombre textual de la prioridad
 *-----------------------------------------------------------------------------*/
const char* obtenerNombrePrioridad(EventPriority priority);

/*-----------------------------------------------------------------------------
 * obtenerTimestampActual - Obtiene timestamp de alta resolucion
 *-----------------------------------------------------------------------------*/
void obtenerTimestampActual(LARGE_INTEGER* timestamp);

#endif /* COMMON_H */
