/*
 * dispatcher.c - Modulo 3 (Armando Diaz)
 *
 * Lee eventos del buffer circular compartido y los reparte entre 4 workers.
 * Cada worker procesa segun prioridad y escribe resultados en el log usando
 * LockFileEx para que no se pisen entre hilos.
 */

#include "common.h"

static HANDLE bufferMapping = NULL;
static CircularBuffer* sharedBuffer = NULL;
static HANDLE bufferMutex = NULL;
static HANDLE semaphoreEmpty = NULL;
static HANDLE semaphoreFull = NULL;
static HANDLE logFile = NULL;
static HANDLE shutdownEvent = NULL;
static HANDLE workerThreads[WORKER_POOL_SIZE] = { NULL };
static BOOL isRunning = TRUE;

/* --- limpieza --- */

void cleanup() {
    printf("\n[Dispatcher] Iniciando apagado ordenado...\n");

    isRunning = FALSE;

    /* esperar workers */
    printf("[Dispatcher] Esperando workers...\n");
    for (int i = 0; i < WORKER_POOL_SIZE; i++) {
        if (workerThreads[i] != NULL) {
            WaitForSingleObject(workerThreads[i], 2000);
            CloseHandle(workerThreads[i]);
            workerThreads[i] = NULL;
        }
    }

    if (logFile != NULL) { CloseHandle(logFile); logFile = NULL; }
    if (sharedBuffer != NULL) { UnmapViewOfFile(sharedBuffer); sharedBuffer = NULL; }
    if (bufferMapping != NULL) { CloseHandle(bufferMapping); bufferMapping = NULL; }
    if (bufferMutex != NULL) { CloseHandle(bufferMutex); bufferMutex = NULL; }
    if (semaphoreEmpty != NULL) { CloseHandle(semaphoreEmpty); semaphoreEmpty = NULL; }
    if (semaphoreFull != NULL) { CloseHandle(semaphoreFull); semaphoreFull = NULL; }
    if (shutdownEvent != NULL) { CloseHandle(shutdownEvent); shutdownEvent = NULL; }

    printf("[Dispatcher] Recursos liberados.\n");
}

/* --- Ctrl+C --- */

BOOL ctrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        printf("\n[Dispatcher] Ctrl+C recibido.\n");
        cleanup();
        exit(0);
        return TRUE;
    }
    return FALSE;
}

/* --- conectar al buffer del broker --- */

BOOL conectarAlBufferCompartido() {
    printf("[Dispatcher] Conectando al buffer circular del Broker...\n");

    bufferMapping = OpenFileMapping(
        FILE_MAP_ALL_ACCESS, FALSE, BUFFER_NAME
    );

    if (bufferMapping == NULL) {
        printf("[Dispatcher] Error al abrir FileMapping: %lu\n", GetLastError());
        printf("[Dispatcher] Asegurese de que el Broker este ejecutandose.\n");
        return FALSE;
    }

    sharedBuffer = (CircularBuffer*)MapViewOfFile(
        bufferMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CircularBuffer)
    );

    if (sharedBuffer == NULL) {
        printf("[Dispatcher] Error al mapear vista: %lu\n", GetLastError());
        CloseHandle(bufferMapping);
        bufferMapping = NULL;
        return FALSE;
    }

    printf("[Dispatcher] Conectado al buffer circular.\n");
    return TRUE;
}

/* --- conectar sincronizacion --- */

BOOL conectarSincronizacion() {
    printf("[Dispatcher] Conectando a objetos de sincronizacion...\n");

    bufferMutex = OpenMutex(MUTEX_ALL_ACCESS, FALSE, MUTEX_NAME);
    if (bufferMutex == NULL) {
        printf("[Dispatcher] Error al abrir mutex: %lu\n", GetLastError());
        return FALSE;
    }

    semaphoreEmpty = OpenSemaphore(SEMAPHORE_ALL_ACCESS, FALSE, SEMAPHORE_SLOTS_NAME);
    if (semaphoreEmpty == NULL) {
        printf("[Dispatcher] Error al abrir semaforo de slots: %lu\n", GetLastError());
        return FALSE;
    }

    semaphoreFull = OpenSemaphore(SEMAPHORE_ALL_ACCESS, FALSE, SEMAPHORE_DATA_NAME);
    if (semaphoreFull == NULL) {
        printf("[Dispatcher] Error al abrir semaforo de datos: %lu\n", GetLastError());
        return FALSE;
    }

    shutdownEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, EVENT_SHUTDOWN_NAME);
    if (shutdownEvent == NULL) {
        printf("[Dispatcher] Error al abrir evento de apagado: %lu\n", GetLastError());
        return FALSE;
    }

    printf("[Dispatcher] Sincronizacion conectada.\n");
    return TRUE;
}

/* --- abrir archivo de log --- */

BOOL abrirLog() {
    printf("[Dispatcher] Abriendo archivo de log...\n");

    logFile = CreateFile(
        LOG_FILE, GENERIC_WRITE, FILE_SHARE_READ, NULL,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL
    );

    if (logFile == INVALID_HANDLE_VALUE) {
        printf("[Dispatcher] Error al abrir log: %lu\n", GetLastError());
        return FALSE;
    }

    SetFilePointer(logFile, 0, NULL, FILE_END);
    printf("[Dispatcher] Log abierto: %s\n", LOG_FILE);
    return TRUE;
}

/* --- escribir en el log con LockFileEx --- */

void escribirLog(const SensorEvent* evento, int workerId) {
    OVERLAPPED ov = {0};
    char buffer[1024];
    DWORD bytesWritten, resultado;

    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (ov.hEvent == NULL) return;

    SYSTEMTIME st;
    FileTimeToSystemTime((FILETIME*)&evento->timestamp, &st);

    int len = snprintf(buffer, sizeof(buffer),
        "[%02d:%02d:%02d.%03d] "
        "Worker-%d | EventoID=%lu | SensorID=%lu | Tipo=%s | Priority=%s | "
        "Payload[0]=%02X Payload[1]=%02X Payload[2]=%02X\n",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        workerId, evento->eventId, evento->sensorId,
        obtenerNombreSensor(evento->sensorType),
        obtenerNombrePrioridad(evento->priority),
        (unsigned char)evento->payload[0],
        (unsigned char)evento->payload[1],
        (unsigned char)evento->payload[2]
    );

    resultado = LockFileEx(logFile, LOCKFILE_EXCLUSIVE_LOCK,
                           0, 0, 0, &ov);
    if (!resultado) {
        printf("[Worker-%d] Error en LockFileEx: %lu\n", workerId, GetLastError());
        CloseHandle(ov.hEvent);
        return;
    }

    DWORD bytesTransferred;
    GetOverlappedResult(logFile, &ov, &bytesTransferred, TRUE);

    if (!WriteFile(logFile, buffer, len, &bytesWritten, NULL)) {
        printf("[Worker-%d] Error en WriteFile: %lu\n", workerId, GetLastError());
    } else {
        FlushFileBuffers(logFile);
    }

    UnlockFileEx(logFile, 0, 0, 0, &ov);
    CloseHandle(ov.hEvent);
}

/* --- procesar un evento (simula carga) --- */

void procesarEvento(SensorEvent* evento, int workerId) {
    DWORD checksum = 0;
    for (DWORD i = 0; i < evento->payloadSize; i++)
        checksum += (unsigned char)evento->payload[i];
    checksum = checksum % 256;

    DWORD delay = 0;
    switch (evento->priority) {
        case PRIORITY_CRITICAL: delay = 1;  break;
        case PRIORITY_HIGH:     delay = 5;  break;
        case PRIORITY_NORMAL:   delay = 10; break;
        case PRIORITY_LOW:      delay = 20; break;
    }
    Sleep(delay);

    escribirLog(evento, workerId);

    if (sharedBuffer != NULL)
        InterlockedIncrement(&sharedBuffer->eventsProcessed);
}

/* --- hilo worker --- */

DWORD WINAPI hiloWorker(LPVOID lpParam) {
    WorkerParams* params = (WorkerParams*)lpParam;
    int workerId = params->workerId;

    printf("[Worker-%d] Hilo worker iniciado.\n", workerId);

    while (isRunning) {
        SensorEvent evento;

        /* esperar datos en el buffer (timeout 100ms para no trabarse) */
        DWORD waitResult = WaitForSingleObject(semaphoreFull, 100);

        if (waitResult == WAIT_TIMEOUT) continue;

        if (waitResult != WAIT_OBJECT_0) {
            printf("[Worker-%d] Error esperando datos: %lu\n", workerId, GetLastError());
            break;
        }

        /* seccion critica: leer del buffer */
        waitResult = WaitForSingleObject(bufferMutex, INFINITE);
        if (waitResult != WAIT_OBJECT_0) {
            printf("[Worker-%d] Error esperando mutex: %lu\n", workerId, GetLastError());
            break;
        }

        memcpy(&evento, &sharedBuffer->events[sharedBuffer->readPos], sizeof(SensorEvent));
        sharedBuffer->readPos = (sharedBuffer->readPos + 1) % BUFFER_SIZE;

        InterlockedDecrement(&sharedBuffer->availableData);
        InterlockedIncrement(&sharedBuffer->availableSlots);

        ReleaseMutex(bufferMutex);
        ReleaseSemaphore(semaphoreEmpty, 1, NULL);

        procesarEvento(&evento, workerId);

        /* chulear shutdown */
        if (WaitForSingleObject(shutdownEvent, 0) == WAIT_OBJECT_0) {
            printf("[Worker-%d] Senal de apagado recibida.\n", workerId);
            break;
        }
    }

    free(params);
    printf("[Worker-%d] Worker terminado.\n", workerId);
    return 0;
}

/* --- crear pool de workers --- */

BOOL crearPoolWorkers() {
    printf("[Dispatcher] Creando pool de %d workers...\n", WORKER_POOL_SIZE);

    for (int i = 0; i < WORKER_POOL_SIZE; i++) {
        WorkerParams* params = (WorkerParams*)malloc(sizeof(WorkerParams));
        if (params == NULL) {
            printf("[Dispatcher] Error: no se pudo asignar memoria para worker %d\n", i);
            return FALSE;
        }

        params->workerId = i;
        params->hBufferMapping = bufferMapping;
        params->hMutex = bufferMutex;
        params->hSemSlots = semaphoreEmpty;
        params->hSemData = semaphoreFull;
        params->hLogFile = logFile;
        params->hShutdownEvent = shutdownEvent;

        DWORD threadId;
        workerThreads[i] = CreateThread(NULL, 0, hiloWorker, params, 0, &threadId);

        if (workerThreads[i] == NULL) {
            printf("[Dispatcher] Error al crear worker %d: %lu\n", i, GetLastError());
            free(params);
            return FALSE;
        }

        printf("[Dispatcher] Worker-%d creado (Thread ID: %lu)\n", i, threadId);
    }

    printf("[Dispatcher] Pool de workers creado.\n");
    return TRUE;
}

/* --- hilo de estadisticas en consola --- */

DWORD WINAPI hiloMonitorEstadisticas(LPVOID lpParam) {
    (void)lpParam;

    while (isRunning) {
        if (sharedBuffer != NULL) {
            float ocupacion = 100.0f - (float)sharedBuffer->availableSlots / BUFFER_SIZE * 100.0f;
            printf("\r[Dispatcher] Buffer: %5.1f%% | "
                   "Eventos procesados: %lu | "
                   "Sensores activos: %lu | "
                   "Slots libres: %lu/%d    ",
                   ocupacion,
                   sharedBuffer->eventsProcessed,
                   sharedBuffer->activeSensors,
                   sharedBuffer->availableSlots,
                   BUFFER_SIZE);
        }
        Sleep(500);
    }
    return 0;
}

/* --- main --- */

int main() {
    HANDLE monitorThread;
    DWORD monitorThreadId;

    printf("========================================\n");
    printf("  DISPATCHER - Modulo 3\n");
    printf("  Responsabilidad: Armando Diaz\n");
    printf("========================================\n\n");

    SetConsoleCtrlHandler((PHANDLER_ROUTINE)ctrlHandler, TRUE);

    if (!conectarAlBufferCompartido()) {
        printf("[Dispatcher] Error al conectar al buffer.\n");
        return 1;
    }
    if (!conectarSincronizacion()) {
        printf("[Dispatcher] Error al conectar sincronizacion.\n");
        return 1;
    }
    if (!abrirLog()) {
        printf("[Dispatcher] Error al abrir log.\n");
        return 1;
    }
    if (!crearPoolWorkers()) {
        printf("[Dispatcher] Error al crear pool de workers.\n");
        return 1;
    }

    monitorThread = CreateThread(NULL, 0, hiloMonitorEstadisticas, NULL, 0, &monitorThreadId);

    printf("[Dispatcher] Ejecutando. Presione Ctrl+C para detener.\n\n");

    WaitForSingleObject(shutdownEvent, INFINITE);

    printf("\n[Dispatcher] Senal de apagado recibida.\n");

    isRunning = FALSE;
    WaitForSingleObject(monitorThread, 1000);
    CloseHandle(monitorThread);

    cleanup();
    printf("[Dispatcher] Finalizado.\n");
    return 0;
}
