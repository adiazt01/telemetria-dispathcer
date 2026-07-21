/* broker.c - Broker Ingestor Central (Jesus Guzman)
 * Crea buffer circular en memoria compartida, acepta sensores via Named Pipes,
 * y deposita eventos en el buffer usando semaforos/mutex (sin busy waiting). */

#include "common.h"

static HANDLE bufferMapping = NULL;
static CircularBuffer* sharedBuffer = NULL;
static HANDLE bufferMutex = NULL;
static HANDLE semaphoreEmpty = NULL;
static HANDLE semaphoreFull = NULL;
static HANDLE debugEvent = NULL;
static HANDLE shutdownEvent = NULL;
static HANDLE receiverThreads[256] = { NULL };
static volatile LONG activeSensors = 0;
static BOOL isRunning = TRUE;

void cleanup() {
    printf("\n[Broker] Iniciando apagado ordenado...\n");
    isRunning = FALSE;

    printf("[Broker] Esperando hilos receptores...\n");
    for (int i = 0; i < 256; i++) {
        if (receiverThreads[i] != NULL) {
            WaitForSingleObject(receiverThreads[i], 1000);
            CloseHandle(receiverThreads[i]);
            receiverThreads[i] = NULL;
        }
    }

    if (sharedBuffer) { UnmapViewOfFile(sharedBuffer); sharedBuffer = NULL; }
    if (bufferMapping) { CloseHandle(bufferMapping); bufferMapping = NULL; }
    if (bufferMutex) { CloseHandle(bufferMutex); bufferMutex = NULL; }
    if (semaphoreEmpty) { CloseHandle(semaphoreEmpty); semaphoreEmpty = NULL; }
    if (semaphoreFull) { CloseHandle(semaphoreFull); semaphoreFull = NULL; }
    if (debugEvent) { CloseHandle(debugEvent); debugEvent = NULL; }
    if (shutdownEvent) { CloseHandle(shutdownEvent); shutdownEvent = NULL; }

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

    bufferMapping = CreateFileMapping(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
        0, (DWORD)sizeof(CircularBuffer), BUFFER_NAME
    );

    if (bufferMapping == NULL) {
        printf("[Broker] Error al crear FileMapping: %lu\n", GetLastError());
        return FALSE;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
        printf("[Broker] El buffer ya existe (posible ejecucion anterior)\n");

    sharedBuffer = (CircularBuffer*)MapViewOfFile(
        bufferMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CircularBuffer)
    );

    if (sharedBuffer == NULL) {
        printf("[Broker] Error al mapear vista: %lu\n", GetLastError());
        CloseHandle(bufferMapping);
        bufferMapping = NULL;
        return FALSE;
    }

    memset(sharedBuffer, 0, sizeof(CircularBuffer));
    sharedBuffer->bufferSize = BUFFER_SIZE;
    sharedBuffer->writePos = 0;
    sharedBuffer->readPos = 0;
    sharedBuffer->availableSlots = BUFFER_SIZE;
    sharedBuffer->availableData = 0;
    sharedBuffer->eventsProcessed = 0;
    sharedBuffer->debugMode = FALSE;
    sharedBuffer->shutdownRequested = FALSE;
    sharedBuffer->activeSensors = 0;

    printf("[Broker] Buffer circular creado: %d slots\n", BUFFER_SIZE);
    return TRUE;
}

BOOL inicializarSincronizacion() {
    printf("[Broker] Creando objetos de sincronizacion...\n");

    bufferMutex = CreateMutex(NULL, FALSE, MUTEX_NAME);
    if (bufferMutex == NULL) {
        printf("[Broker] Error al crear mutex: %lu\n", GetLastError());
        return FALSE;
    }

    semaphoreEmpty = CreateSemaphore(NULL, BUFFER_SIZE, BUFFER_SIZE, SEMAPHORE_SLOTS_NAME);
    if (semaphoreEmpty == NULL) {
        printf("[Broker] Error al crear semaforo de slots: %lu\n", GetLastError());
        return FALSE;
    }

    semaphoreFull = CreateSemaphore(NULL, 0, BUFFER_SIZE, SEMAPHORE_DATA_NAME);
    if (semaphoreFull == NULL) {
        printf("[Broker] Error al crear semaforo de datos: %lu\n", GetLastError());
        return FALSE;
    }

    debugEvent = CreateEvent(NULL, TRUE, FALSE, EVENT_DEBUG_NAME);
    if (debugEvent == NULL) {
        printf("[Broker] Error al crear evento de debug: %lu\n", GetLastError());
        return FALSE;
    }

    shutdownEvent = CreateEvent(NULL, TRUE, FALSE, EVENT_SHUTDOWN_NAME);
    if (shutdownEvent == NULL) {
        printf("[Broker] Error al crear evento de apagado: %lu\n", GetLastError());
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

    while (isRunning) {
        if (!ReadFile(params->pipeHandle, &evento, sizeof(SensorEvent), &bytesRead, NULL)) {
            DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE)
                printf("[Broker] Sensor %d desconectado.\n", sensorId);
            else
                printf("[Broker] Error al leer del pipe: %lu\n", error);
            break;
        }

        if (bytesRead != sizeof(SensorEvent)) {
            printf("[Broker] Tamano de evento incorrecto: %lu vs %llu\n",
                   bytesRead, sizeof(SensorEvent));
            continue;
        }

        if (WaitForSingleObject(params->hSemSlots, INFINITE) != WAIT_OBJECT_0) {
            printf("[Broker] Error esperando slot: %lu\n", GetLastError());
            break;
        }
        if (WaitForSingleObject(params->hMutex, INFINITE) != WAIT_OBJECT_0) {
            printf("[Broker] Error esperando mutex: %lu\n", GetLastError());
            ReleaseSemaphore(params->hSemSlots, 1, NULL);
            break;
        }

		/* Actualizar estadisticas del dashboard */
        memcpy(&sharedBuffer->events[sharedBuffer->writePos], &evento, sizeof(SensorEvent));
        sharedBuffer->writePos = (sharedBuffer->writePos + 1) % BUFFER_SIZE;

        InterlockedDecrement(&sharedBuffer->availableSlots);
        InterlockedIncrement(&sharedBuffer->availableData);

        ReleaseMutex(params->hMutex);
        ReleaseSemaphore(params->hSemData, 1, NULL);

        if (sharedBuffer->debugMode)
             printf("[Broker] Sensor %d -> Buffer[slot %ld] Evento ID=%lu\n",
                   sensorId, sharedBuffer->writePos, evento.eventId);
    }

    DisconnectNamedPipe(params->pipeHandle);
    CloseHandle(params->pipeHandle);
    InterlockedDecrement(&activeSensors);
    if (sharedBuffer) InterlockedDecrement(&sharedBuffer->activeSensors);

    printf("[Broker] Hilo receptor del sensor %d terminado.\n", sensorId);
    free(params);
    return 0;
}

void esperarConexiones() {
    HANDLE pipeHandle;
    DWORD threadId;
    int nextSensorId = 0;

    printf("[Broker] Esperando conexiones de sensores...\n");
    printf("[Broker] Presione Ctrl+C para detener\n\n");

    while (isRunning) {

		pipeHandle = CreateNamedPipe(
            PIPE_NAME, PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            sizeof(SensorEvent), sizeof(SensorEvent), TIMEOUT_MS, NULL
        );

        if (pipeHandle == INVALID_HANDLE_VALUE) {
            printf("[Broker] Error al crear pipe: %lu\n", GetLastError());
            Sleep(1000);
            continue;
        }

        printf("[Broker] Esperando conexion en %s...\n", PIPE_NAME);
        if (!ConnectNamedPipe(pipeHandle, NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_PIPE_CONNECTED) {
                printf("[Broker] Error en ConnectNamedPipe: %lu\n", error);
                CloseHandle(pipeHandle);
                continue;
            }
        }

        printf("[Broker] Sensor %d conectado!\n", nextSensorId);

        ReceiverParams* params = (ReceiverParams*)malloc(sizeof(ReceiverParams));
        if (params == NULL) {
            printf("[Broker] Error: no se pudo asignar memoria\n");
            CloseHandle(pipeHandle);
            continue;
        }

        params->sensorId = nextSensorId;
        params->pipeHandle = pipeHandle;
        params->hBufferMapping = bufferMapping;
        params->hMutex = bufferMutex;
        params->hSemSlots = semaphoreEmpty;
        params->hSemData = semaphoreFull;

        InterlockedIncrement(&activeSensors);
        if (sharedBuffer) InterlockedIncrement(&sharedBuffer->activeSensors);

        HANDLE threadHandle = CreateThread(NULL, 0, hiloReceptor, params, 0, &threadId);
        if (threadHandle == NULL) {
            printf("[Broker] Error al crear hilo receptor: %lu\n", GetLastError());
            CloseHandle(pipeHandle);
            free(params);
            continue;
        }

        receiverThreads[nextSensorId] = threadHandle;
        CloseHandle(threadHandle);
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
