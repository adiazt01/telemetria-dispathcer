/*
 * ============================================================================
 * MODULO 3: Distribuidor y Pool de Workers de Procesamiento
 * RESPONSABLE: Armando Diaz
 * ============================================================================
 *
 * dispatcher.c - Distribuidor y workers de procesamiento
 *
 * Este modulo tiene dos funciones principales:
 *
 * 1. DISPATCHER (hilo principal):
 *    - Consume eventos del buffer circular (memoria compartida)
 *    - Analiza la prioridad del evento
 *    - Redirige a los workers a traves de una cola de mensajes
 *
 * 2. POOL DE WORKERS (hilos trabajadores):
 *    - Procesan los eventos (simulan carga de computo)
 *    - Escriben los resultados en el archivo de log
 *    - Usan LockFileEx para escritura concurrente segura
 *
 * Comunicacion:
 *   - Recepcion: Memoria compartida (lee del buffer circular)
 *   - Workers: Hilos con cola de mensajes interna
 *   - Persistencia: Archivo de log con bloqueo de archivo
 * ============================================================================
 */

#include "common.h"

/* Handle al archivo de mapeo del buffer circular */
static HANDLE g_hBufferMapping = NULL;

/* Puntero al buffer circular */
static CircularBuffer* g_pBuffer = NULL;

/* Handle al mutex */
static HANDLE g_hMutex = NULL;

/* Handle al semaforo de slots disponibles */
static HANDLE g_hSemSlots = NULL;

/* Handle al semaforo de datos disponibles */
static HANDLE g_hSemData = NULL;

/* Handle al archivo de log */
static HANDLE g_hLogFile = NULL;

/* Handle al evento de apagado */
static HANDLE g_hShutdownEvent = NULL;

/* Handles a los hilos worker */
static HANDLE g_workerThreads[WORKER_POOL_SIZE] = { NULL };

/* Bandera de ejecucion */
static BOOL g_running = TRUE;

/* ============================================================================
 * LIMPIEZA DE RECURSOS
 * ============================================================================ */

void cleanup() {
    printf("\n[Dispatcher] Iniciando apagado ordenado...\n");

    g_running = FALSE;

    /* Esperar a que todos los workers terminen */
    printf("[Dispatcher] Esperando workers...\n");
    for (int i = 0; i < WORKER_POOL_SIZE; i++) {
        if (g_workerThreads[i] != NULL) {
            WaitForSingleObject(g_workerThreads[i], 2000);
            CloseHandle(g_workerThreads[i]);
            g_workerThreads[i] = NULL;
        }
    }

    /* Cerrar archivo de log */
    if (g_hLogFile != NULL) {
        CloseHandle(g_hLogFile);
        g_hLogFile = NULL;
    }

    /* Desmapear vista de memoria */
    if (g_pBuffer != NULL) {
        UnmapViewOfFile(g_pBuffer);
        g_pBuffer = NULL;
    }

    /* Cerrar handles */
    if (g_hBufferMapping != NULL) {
        CloseHandle(g_hBufferMapping);
        g_hBufferMapping = NULL;
    }

    if (g_hMutex != NULL) {
        CloseHandle(g_hMutex);
        g_hMutex = NULL;
    }

    if (g_hSemSlots != NULL) {
        CloseHandle(g_hSemSlots);
        g_hSemSlots = NULL;
    }

    if (g_hSemData != NULL) {
        CloseHandle(g_hSemData);
        g_hSemData = NULL;
    }

    if (g_hShutdownEvent != NULL) {
        CloseHandle(g_hShutdownEvent);
        g_hShutdownEvent = NULL;
    }

    printf("[Dispatcher] Recursos liberados.\n");
}

/* ============================================================================
 * MANEJO DE SENALES
 * ============================================================================ */

BOOL ctrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        printf("\n[Dispatcher] Ctrl+C recibido.\n");
        cleanup();
        exit(0);
        return TRUE;
    }
    return FALSE;
}

/* ============================================================================
 * CONEXION AL BUFFER COMPARTIDO (BROKER)
 * ============================================================================ */

BOOL conectarAlBufferCompartido() {
    printf("[Dispatcher] Conectando al buffer circular del Broker...\n");

    /* Abrir el archivo de mapeo existente */
    g_hBufferMapping = OpenFileMapping(
        FILE_MAP_ALL_ACCESS,     /* Permisos de lectura y escritura */
        FALSE,                   /* No heredar */
        BUFFER_NAME              /* Nombre del objeto */
    );

    if (g_hBufferMapping == NULL) {
        printf("[Dispatcher] Error al abrir FileMapping: %d\n", GetLastError());
        printf("[Dispatcher] Asegurese de que el Broker este ejecutandose.\n");
        return FALSE;
    }

    /* Mapear la vista del archivo */
    g_pBuffer = (CircularBuffer*)MapViewOfFile(
        g_hBufferMapping,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        sizeof(CircularBuffer)
    );

    if (g_pBuffer == NULL) {
        printf("[Dispatcher] Error al mapear vista: %d\n", GetLastError());
        CloseHandle(g_hBufferMapping);
        g_hBufferMapping = NULL;
        return FALSE;
    }

    printf("[Dispatcher] Conectado al buffer circular.\n");
    return TRUE;
}

/* ============================================================================
 * CONEXION A OBJETOS DE SINCRONIZACION
 * ============================================================================ */

BOOL conectarSincronizacion() {
    printf("[Dispatcher] Conectando a objetos de sincronizacion...\n");

    /* Abrir mutex */
    g_hMutex = OpenMutex(
        MUTEX_ALL_ACCESS,
        FALSE,
        MUTEX_NAME
    );

    if (g_hMutex == NULL) {
        printf("[Dispatcher] Error al abrir mutex: %d\n", GetLastError());
        return FALSE;
    }

    /* Abrir semaforo de slots (espacios disponibles) */
    g_hSemSlots = OpenSemaphore(
        SEMAPHORE_ALL_ACCESS,
        FALSE,
        SEMAPHORE_SLOTS_NAME
    );

    if (g_hSemSlots == NULL) {
        printf("[Dispatcher] Error al abrir semaforo de slots: %d\n", GetLastError());
        return FALSE;
    }

    /* Abrir semaforo de datos (datos disponibles) */
    g_hSemData = OpenSemaphore(
        SEMAPHORE_ALL_ACCESS,
        FALSE,
        SEMAPHORE_DATA_NAME
    );

    if (g_hSemData == NULL) {
        printf("[Dispatcher] Error al abrir semaforo de datos: %d\n", GetLastError());
        return FALSE;
    }

    /* Abrir evento de apagado */
    g_hShutdownEvent = OpenEvent(
        EVENT_ALL_ACCESS,
        FALSE,
        EVENT_SHUTDOWN_NAME
    );

    if (g_hShutdownEvent == NULL) {
        printf("[Dispatcher] Error al abrir evento de apagado: %d\n", GetLastError());
        return FALSE;
    }

    printf("[Dispatcher] Sincronizacion conectada.\n");
    return TRUE;
}

/* ============================================================================
 * APERTURA DEL ARCHIVO DE LOG
 * ============================================================================ */

BOOL abrirLog() {
    printf("[Dispatcher] Abriendo archivo de log...\n");

    /* Abrir archivo de log en modo append (agregar al final) */
    g_hLogFile = CreateFile(
        LOG_FILE,
        GENERIC_WRITE,
        FILE_SHARE_READ,  /* Otros pueden leer, pero no escribir */
        NULL,
        OPEN_ALWAYS,      /* Crear si no existe */
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (g_hLogFile == INVALID_HANDLE_VALUE) {
        printf("[Dispatcher] Error al abrir log: %d\n", GetLastError());
        return FALSE;
    }

    /* Ir al final del archivo (append) */
    SetFilePointer(g_hLogFile, 0, NULL, FILE_END);

    printf("[Dispatcher] Log abierto: %s\n", LOG_FILE);
    return TRUE;
}

/* ============================================================================
 * ESCRITURA SEGURA EN LOG CON LOCKFILEEX
 * ============================================================================ */

void escribirLog(const SensorEvent* evento, int workerId) {
    OVERLAPPED ov = {0};
    char buffer[1024];
    DWORD bytesWritten;
    DWORD resultado;

    /* Crear evento para operacion overlapped */
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (ov.hEvent == NULL) {
        return;
    }

    /* Formatear linea de log */
    SYSTEMTIME st;
    FileTimeToSystemTime((FILETIME*)&evento->timestamp, &st);

    int len = snprintf(buffer, sizeof(buffer),
        "[%02d:%02d:%02d.%03d] "
        "Worker-%d | EventoID=%lu | SensorID=%lu | Tipo=%s | Priority=%s | "
        "Payload[0]=%02X Payload[1]=%02X Payload[2]=%02X\n",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        workerId,
        evento->eventId,
        evento->sensorId,
        obtenerNombreSensor(evento->sensorType),
        obtenerNombrePrioridad(evento->priority),
        (unsigned char)evento->payload[0],
        (unsigned char)evento->payload[1],
        (unsigned char)evento->payload[2]
    );

    /* Bloquear region del archivo (exclusivo para escritura) */
    /* LockFileEx asegura que solo un worker escriba a la vez */
    resultado = LockFileEx(
        g_hLogFile,                        /* Handle al archivo */
        LOCKFILE_EXCLUSIVE_LOCK,          /* Bloqueo exclusivo */
        0,                                /* Flags (reservado) */
        0,                                /* Offset bajo a bloquear */
        0,                                /* Offset alto a bloquear */
        &ov                               /* Estructura overlapped */
    );

    if (!resultado) {
        printf("[Worker-%d] Error en LockFileEx: %d\n", workerId, GetLastError());
        CloseHandle(ov.hEvent);
        return;
    }

    /* Esperar a que termine el bloqueo */
    DWORD bytesTransferred;
    GetOverlappedResult(g_hLogFile, &ov, &bytesTransferred, TRUE);

    /* Escribir en el archivo */
    WriteFile(
        g_hLogFile,
        buffer,
        len,
        &bytesWritten,
        NULL
    );

    /* Flush para asegurar que se escriba inmediatamente */
    FlushFileBuffers(g_hLogFile);

    /* Desbloquear el archivo */
    UnlockFileEx(g_hLogFile, 0, 0, 0, &ov);

    CloseHandle(ov.hEvent);
}

/* ============================================================================
 * PROCESAMIENTO DE EVENTO (simula carga de computo)
 * ============================================================================ */

void procesarEvento(SensorEvent* evento, int workerId) {
    /* Simular procesamiento de datos */
    /* Por ejemplo, calcular checksum, validar datos, etc. */

    DWORD checksum = 0;
    for (DWORD i = 0; i < evento->payloadSize; i++) {
        checksum += (unsigned char)evento->payload[i];
    }
    checksum = checksum % 256;

    /* Simular delay de procesamiento (carga de computo) */
    /* Prioridad alta = menos delay, prioridad baja = mas delay */
    DWORD delay = 0;
    switch (evento->priority) {
        case PRIORITY_CRITICAL: delay = 1; break;
        case PRIORITY_HIGH:      delay = 5; break;
        case PRIORITY_NORMAL:    delay = 10; break;
        case PRIORITY_LOW:       delay = 20; break;
    }

    Sleep(delay);

    /* Escribir en log */
    escribirLog(evento, workerId);

    /* Actualizar contador de eventos procesados */
    if (g_pBuffer != NULL) {
        InterlockedIncrement(&g_pBuffer->eventsProcessed);
    }
}

/* ============================================================================
 * HILO WORKER
 *
 * Cada worker espera eventos de la cola y los procesa.
 * Los eventos se distribuyen de forma round-robin por prioridad.
 * ============================================================================ */

DWORD WINAPI hiloWorker(LPVOID lpParam) {
    WorkerParams* params = (WorkerParams*)lpParam;
    int workerId = params->workerId;

    printf("[Worker-%d] Hilo worker iniciado.\n", workerId);

    /* Bucle principal del worker */
    while (g_running) {
        SensorEvent evento;

        /*---------------------------------------
         * ESPERAR DATOS DISPONIBLES EN BUFFER
         *---------------------------------------*/

        /* Esperar a que haya datos para leer */
        /* Esto bloquea el hilo si el buffer esta vacio */
        DWORD waitResult = WaitForSingleObject(g_hSemData, 100);

        if (waitResult == WAIT_TIMEOUT) {
            /* No hay datos, continue revisando */
            continue;
        }

        if (waitResult != WAIT_OBJECT_0) {
            printf("[Worker-%d] Error esperando datos: %d\n", workerId, GetLastError());
            break;
        }

        /*---------------------------------------
         * LEER EVENTO DEL BUFFER CIRCULAR
         *---------------------------------------*/

        /* Entrar a seccion critica para leer del buffer */
        waitResult = WaitForSingleObject(g_hMutex, INFINITE);
        if (waitResult != WAIT_OBJECT_0) {
            printf("[Worker-%d] Error esperando mutex: %d\n", workerId, GetLastError());
            break;
        }

		/* Leer evento y actualizar estadisticas del dashboard */
        memcpy(&evento, &g_pBuffer->events[g_pBuffer->readPos], sizeof(SensorEvent));
        g_pBuffer->readPos = (g_pBuffer->readPos + 1) % BUFFER_SIZE;
        
        InterlockedDecrement(&g_pBuffer->availableData);
        InterlockedIncrement(&g_pBuffer->availableSlots);

        /* Salir de seccion critica */
        ReleaseMutex(g_hMutex);

        /* Senalar que hay un slot disponible (espacio libre) */
        ReleaseSemaphore(g_hSemSlots, 1, NULL);

        /*---------------------------------------
         * PROCESAR EVENTO
         *---------------------------------------*/

        procesarEvento(&evento, workerId);

        /* Verificar si hay solicitud de apagado */
        if (waitResult == WAIT_OBJECT_0) {
            HANDLE handles[1] = { g_hShutdownEvent };
            waitResult = WaitForSingleObject(g_hShutdownEvent, 0);
            if (waitResult == WAIT_OBJECT_0) {
                printf("[Worker-%d] Senal de apagado recibida.\n", workerId);
                break;
            }
        }
    }

    free(params);
    printf("[Worker-%d] Worker terminado.\n", workerId);
    return 0;
}

/* ============================================================================
 * CREAR POOL DE WORKERS
 * ============================================================================ */

BOOL crearPoolWorkers() {
    printf("[Dispatcher] Creando pool de %d workers...\n", WORKER_POOL_SIZE);

    for (int i = 0; i < WORKER_POOL_SIZE; i++) {
        /* Crear parametros del worker */
        WorkerParams* params = (WorkerParams*)malloc(sizeof(WorkerParams));
        if (params == NULL) {
            printf("[Dispatcher] Error: no se pudo asignar memoria para worker %d\n", i);
            return FALSE;
        }

        params->workerId = i;
        params->hBufferMapping = g_hBufferMapping;
        params->hMutex = g_hMutex;
        params->hSemSlots = g_hSemSlots;
        params->hSemData = g_hSemData;
        params->hLogFile = g_hLogFile;
        params->hShutdownEvent = g_hShutdownEvent;

        /* Crear hilo worker */
        DWORD threadId;
        g_workerThreads[i] = CreateThread(
            NULL,
            0,
            hiloWorker,
            params,
            0,
            &threadId
        );

        if (g_workerThreads[i] == NULL) {
            printf("[Dispatcher] Error al crear worker %d: %d\n", i, GetLastError());
            free(params);
            return FALSE;
        }

        printf("[Dispatcher] Worker-%d creado (Thread ID: %lu)\n", i, threadId);
    }

    printf("[Dispatcher] Pool de workers creado.\n");
    return TRUE;
}

/* ============================================================================
 * HILO PRINCIPAL DEL DISPATCHER (MONITOR DE ESTADISTICAS)
 * ============================================================================ */

DWORD WINAPI hiloMonitorEstadisticas(LPVOID lpParam) {
    (void)lpParam;  /* Parametro no usado */

    while (g_running) {
        if (g_pBuffer != NULL) {
            /* Calcular porcentaje de ocupacion del buffer */
            float ocupacion = 100.0f - (float)g_pBuffer->availableSlots / BUFFER_SIZE * 100.0f;

            printf("\r[Dispatcher] Buffer: %5.1f%% | "
                   "Eventos procesados: %lu | "
                   "Sensores activos: %lu | "
                   "Slots libres: %lu/%d    ",
                   ocupacion,
                   g_pBuffer->eventsProcessed,
                   g_pBuffer->activeSensors,
                   g_pBuffer->availableSlots,
                   BUFFER_SIZE);
        }

        Sleep(500);  /* Actualizar cada 500ms */
    }

    return 0;
}

/* ============================================================================
 * PUNTO DE ENTRADA PRINCIPAL
 * ============================================================================ */

int main() {
    HANDLE hMonitorThread;
    DWORD monitorThreadId;

    printf("========================================\n");
    printf("  DISPATCHER - Modulo 3\n");
    printf("  Responsabilidad: Armando Diaz\n");
    printf("========================================\n\n");

    /* Registrar manejador de Ctrl+C */
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)ctrlHandler, TRUE);

    /* Conectar al buffer compartido del Broker */
    if (!conectarAlBufferCompartido()) {
        printf("[Dispatcher] Error al conectar al buffer.\n");
        return 1;
    }

    /* Conectar a los objetos de sincronizacion */
    if (!conectarSincronizacion()) {
        printf("[Dispatcher] Error al conectar sincronizacion.\n");
        return 1;
    }

    /* Abrir archivo de log */
    if (!abrirLog()) {
        printf("[Dispatcher] Error al abrir log.\n");
        return 1;
    }

    /* Crear pool de workers */
    if (!crearPoolWorkers()) {
        printf("[Dispatcher] Error al crear pool de workers.\n");
        return 1;
    }

    /* Crear hilo para mostrar estadisticas */
    hMonitorThread = CreateThread(NULL, 0, hiloMonitorEstadisticas, NULL, 0, &monitorThreadId);

    printf("[Dispatcher] Ejecutando. Presione Ctrl+C para detener.\n\n");

    /* Esperar senal de apagado o Ctrl+C */
    WaitForSingleObject(g_hShutdownEvent, INFINITE);

    printf("\n[Dispatcher] Senal de apagado recibida.\n");

    /* Detener monitor de estadisticas */
    g_running = FALSE;
    WaitForSingleObject(hMonitorThread, 1000);
    CloseHandle(hMonitorThread);

    /* Limpieza */
    cleanup();

    printf("[Dispatcher] Finalizado.\n");
    return 0;
}
