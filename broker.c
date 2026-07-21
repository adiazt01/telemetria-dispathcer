/* broker.c - Broker Ingestor Central (Jesus Guzman)
/* broker.c - Broker Ingestor Central (Jesus Guzman)
/* broker.c - Broker Ingestor Central (Jesus Guzman)
 * Crea buffer circular en memoria compartida, acepta sensores via Named Pipes,
 * y deposita eventos en el buffer usando semaforos/mutex (sin busy waiting). */

#include "../common.h"

#ifndef PIPE_NAME
#define PIPE_NAME "\\\\.\\pipe\\telemetria_dispatcher"
#endif

static HANDLE g_hBufferMapping = NULL;
static CircularBuffer* g_pBuffer = NULL;
static HANDLE g_hMutex = NULL;
static HANDLE g_hSemSlots = NULL;
static HANDLE g_hSemData = NULL;
static HANDLE g_hDebugEvent = NULL;
static HANDLE g_hShutdownEvent = NULL;
static HANDLE g_receiverThreads[256] = { NULL };
static int g_activeSensors = 0;
static BOOL g_running = TRUE;

void cleanup() {
    printf("\n[Broker] Iniciando apagado ordenado...\n");
    g_running = FALSE;

    printf("[Broker] Esperando hilos receptores...\n");
    for (int i = 0; i < 256; i++) {
        if (g_receiverThreads[i] != NULL) {
            WaitForSingleObject(g_receiverThreads[i], 1000);
            CloseHandle(g_receiverThreads[i]);
            g_receiverThreads[i] = NULL;
        }
    }

    if (g_pBuffer) { UnmapViewOfFile(g_pBuffer); g_pBuffer = NULL; }
    if (g_hBufferMapping) { CloseHandle(g_hBufferMapping); g_hBufferMapping = NULL; }
    if (g_hMutex) { CloseHandle(g_hMutex); g_hMutex = NULL; }
    if (g_hSemSlots) { CloseHandle(g_hSemSlots); g_hSemSlots = NULL; }
    if (g_hSemData) { CloseHandle(g_hSemData); g_hSemData = NULL; }
    if (g_hDebugEvent) { CloseHandle(g_hDebugEvent); g_hDebugEvent = NULL; }
    if (g_hShutdownEvent) { CloseHandle(g_hShutdownEvent); g_hShutdownEvent = NULL; }

    printf("[Broker] Todos los recursos liberados.\n");
}

BOOL ctrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        printf("\n[Broker] Ctrl+C recibido, iniciando apagado...\n");
        cleanup();
        exit(0);
        return TRUE;
    }
    return FALSE;
}

BOOL inicializarBufferCircular() {
    printf("[Broker] Creando buffer circular en memoria compartida...\n");

    g_hBufferMapping = CreateFileMapping(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
        0, (DWORD)sizeof(CircularBuffer), BUFFER_NAME
    );

    if (g_hBufferMapping == NULL) {
        printf("[Broker] Error al crear FileMapping: %d\n", GetLastError());
        return FALSE;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
        printf("[Broker] El buffer ya existe (posible ejecucion anterior)\n");

    g_pBuffer = (CircularBuffer*)MapViewOfFile(
        g_hBufferMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CircularBuffer)
    );

    if (g_pBuffer == NULL) {
        printf("[Broker] Error al mapear vista: %d\n", GetLastError());
        CloseHandle(g_hBufferMapping);
        g_hBufferMapping = NULL;
        return FALSE;
    }

    memset(g_pBuffer, 0, sizeof(CircularBuffer));
    g_pBuffer->bufferSize = BUFFER_SIZE;
    g_pBuffer->writePos = 0;
    g_pBuffer->readPos = 0;
    g_pBuffer->availableSlots = BUFFER_SIZE;
    g_pBuffer->availableData = 0;
    g_pBuffer->eventsProcessed = 0;
    g_pBuffer->debugMode = FALSE;
    g_pBuffer->shutdownRequested = FALSE;
    g_pBuffer->activeSensors = 0;

    printf("[Broker] Buffer circular creado: %d slots\n", BUFFER_SIZE);
    return TRUE;
}

BOOL inicializarSincronizacion() {
    printf("[Broker] Creando objetos de sincronizacion...\n");

    g_hMutex = CreateMutex(NULL, FALSE, MUTEX_NAME);
    if (g_hMutex == NULL) {
        printf("[Broker] Error al crear mutex: %d\n", GetLastError());
        return FALSE;
    }

    g_hSemSlots = CreateSemaphore(NULL, BUFFER_SIZE, BUFFER_SIZE, SEMAPHORE_SLOTS_NAME);
    if (g_hSemSlots == NULL) {
        printf("[Broker] Error al crear semaforo de slots: %d\n", GetLastError());
        return FALSE;
    }

    g_hSemData = CreateSemaphore(NULL, 0, BUFFER_SIZE, SEMAPHORE_DATA_NAME);
    if (g_hSemData == NULL) {
        printf("[Broker] Error al crear semaforo de datos: %d\n", GetLastError());
        return FALSE;
    }

    g_hDebugEvent = CreateEvent(NULL, TRUE, FALSE, EVENT_DEBUG_NAME);
    if (g_hDebugEvent == NULL) {
        printf("[Broker] Error al crear evento de debug: %d\n", GetLastError());
        return FALSE;
    }

    g_hShutdownEvent = CreateEvent(NULL, TRUE, FALSE, EVENT_SHUTDOWN_NAME);
    if (g_hShutdownEvent == NULL) {
        printf("[Broker] Error al crear evento de apagado: %d\n", GetLastError());
        return FALSE;
    }

    printf("[Broker] Sincronizacion inicializada.\n");
    return TRUE;
}

DWORD WINAPI hiloReceptor(LPVOID lpParam) {
    ReceiverParams* params = (ReceiverParams*)lpParam;
    int sensorId = params->sensorId;
    SensorEvent evento;
    DWORD bytesRead;

    printf("[Broker] Hilo receptor iniciado para sensor %d\n", sensorId);

    while (g_running) {
        if (!ReadFile(params->hPipe, &evento, sizeof(SensorEvent), &bytesRead, NULL)) {
            DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE)
                printf("[Broker] Sensor %d desconectado.\n", sensorId);
            else
                printf("[Broker] Error al leer del pipe: %d\n", error);
            break;
        }

        if (bytesRead != sizeof(SensorEvent)) {
            printf("[Broker] Tamano de evento incorrecto: %lu vs %lu\n",
                   bytesRead, sizeof(SensorEvent));
            continue;
        }

        if (WaitForSingleObject(params->hSemSlots, INFINITE) != WAIT_OBJECT_0) {
            printf("[Broker] Error esperando slot: %d\n", GetLastError());
            break;
        }
        if (WaitForSingleObject(params->hMutex, INFINITE) != WAIT_OBJECT_0) {
            printf("[Broker] Error esperando mutex: %d\n", GetLastError());
            ReleaseSemaphore(params->hSemSlots, 1, NULL);
            break;
        }

		/* Actualizar estadisticas del dashboard */
        memcpy(&g_pBuffer->events[g_pBuffer->writePos], &evento, sizeof(SensorEvent));
        g_pBuffer->writePos = (g_pBuffer->writePos + 1) % BUFFER_SIZE;
        
        InterlockedDecrement(&g_pBuffer->availableSlots);
        InterlockedIncrement(&g_pBuffer->availableData);

        ReleaseMutex(params->hMutex);
        ReleaseSemaphore(params->hSemData, 1, NULL);

        if (g_pBuffer->debugMode)
            printf("[Broker] Sensor %d -> Buffer[slot %d] Evento ID=%lu\n",
                   sensorId, g_pBuffer->writePos, evento.eventId);
    }

    DisconnectNamedPipe(params->hPipe);
    CloseHandle(params->hPipe);
    InterlockedDecrement(&g_activeSensors);
    if (g_pBuffer) InterlockedDecrement(&g_pBuffer->activeSensors);

    printf("[Broker] Hilo receptor del sensor %d terminado.\n", sensorId);
    free(params);
    return 0;
}

void esperarConexiones() {
    HANDLE hPipe;
    DWORD threadId;
    int nextSensorId = 0;

    printf("[Broker] Esperando conexiones de sensores...\n");
    printf("[Broker] Presione Ctrl+C para detener\n\n");

    while (g_running) {

		hPipe = CreateNamedPipe(
            PIPE_NAME, PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            sizeof(SensorEvent), sizeof(SensorEvent), TIMEOUT_MS, NULL
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            printf("[Broker] Error al crear pipe: %d\n", GetLastError());
            Sleep(1000);
            continue;
        }

        printf("[Broker] Esperando conexion en %s...\n", PIPE_NAME);
        if (!ConnectNamedPipe(hPipe, NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_PIPE_CONNECTED) {
                printf("[Broker] Error en ConnectNamedPipe: %d\n", error);
                CloseHandle(hPipe);
                continue;
            }
        }

        printf("[Broker] Sensor %d conectado!\n", nextSensorId);

        ReceiverParams* params = (ReceiverParams*)malloc(sizeof(ReceiverParams));
        if (params == NULL) {
            printf("[Broker] Error: no se pudo asignar memoria\n");
            CloseHandle(hPipe);
            continue;
        }

        params->sensorId = nextSensorId;
        params->hPipe = hPipe;
        params->hBufferMapping = g_hBufferMapping;
        params->hMutex = g_hMutex;
        params->hSemSlots = g_hSemSlots;
        params->hSemData = g_hSemData;

        InterlockedIncrement(&g_activeSensors);
        if (g_pBuffer) InterlockedIncrement(&g_pBuffer->activeSensors);

        HANDLE hThread = CreateThread(NULL, 0, hiloReceptor, params, 0, &threadId);
        if (hThread == NULL) {
            printf("[Broker] Error al crear hilo receptor: %d\n", GetLastError());
            CloseHandle(hPipe);
            free(params);
            continue;
        }

        g_receiverThreads[nextSensorId] = hThread;
        CloseHandle(hThread);
        nextSensorId = (nextSensorId + 1) % 256;
    }
}

int main() {
    printf("========================================\n");
    printf("  BROKER - Modulo 2\n");
    printf("  Responsabilidad: Jesus Guzman\n");
    printf("========================================\n\n");

    SetConsoleCtrlHandler((PHANDLER_ROUTINE)ctrlHandler, TRUE);

    if (!inicializarBufferCircular()) {
        printf("[Broker] Error al inicializar buffer circular.\n");
        return 1;
    }
    if (!inicializarSincronizacion()) {
        printf("[Broker] Error al inicializar sincronizacion.\n");
        cleanup();
        return 1;
    }

    esperarConexiones();
    cleanup();
    return 0;
}
