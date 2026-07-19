/*
 * ============================================================================
 * MODULO 2: Broker Ingestor Central
 * RESPONSABLE: Jesus Guzman
 * ============================================================================
 *
 * broker.c - Nucleo receptor de telemetria
 *
 * Este modulo es el nucleo central del sistema. Es un proceso multihilo que:
 *   1. Crea un buffer circular en memoria compartida (File Mapping)
 *   2. Acepta conexiones de sensores via Named Pipes
 *   3. Por cada sensor conectado, crea un hilo receptor dedicado
 *   4. Los hilos receptores depositan eventos en el buffer circular
 *   5. Protege el buffer con semaforos y mutex (SIN busy waiting)
 *
 * Comunicacion:
 *   - Recepcion: Named Pipes (servidor)
 *   - Almacenamiento: Memory-Mapped File + Buffer Circular
 *   - Sincronizacion: Semaforos + Mutex
 * ============================================================================
 */

#include "../common.h"

/* Handle al archivo de mapeo del buffer circular */
static HANDLE g_hBufferMapping = NULL;

/* Puntero al buffer circular en memoria compartida */
static CircularBuffer* g_pBuffer = NULL;

/* Handle al mutex del buffer */
static HANDLE g_hMutex = NULL;

/* Handle al semaforo de slots disponibles (escritores) */
static HANDLE g_hSemSlots = NULL;

/* Handle al semaforo de datos disponibles (lectores) */
static HANDLE g_hSemData = NULL;

/* Handle al evento de debug (para que Monitor controle) */
static HANDLE g_hDebugEvent = NULL;

/* Handle al evento de apagado */
static HANDLE g_hShutdownEvent = NULL;

/* Arreglo de handles de hilos receptores */
static HANDLE g_receiverThreads[256] = { NULL };

/* Contador de sensores activos */
static int g_activeSensors = 0;

/* Bandera de ejecucion */
static BOOL g_running = TRUE;

/* ============================================================================
 * LIMPIEZA DE RECURSOS
 * ============================================================================ */

void cleanup() {
    printf("\n[Broker] Iniciando apagado ordenado...\n");

    g_running = FALSE;

    /* Esperar a que todos los hilos receptores terminen */
    printf("[Broker] Esperando hilos receptores...\n");
    for (int i = 0; i < 256; i++) {
        if (g_receiverThreads[i] != NULL) {
            WaitForSingleObject(g_receiverThreads[i], 1000);
            CloseHandle(g_receiverThreads[i]);
            g_receiverThreads[i] = NULL;
        }
    }

    /* Desmapear la vista de memoria */
    if (g_pBuffer != NULL) {
        UnmapViewOfFile(g_pBuffer);
        g_pBuffer = NULL;
    }

    /* Cerrar handles de sincronizacion */
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

    if (g_hDebugEvent != NULL) {
        CloseHandle(g_hDebugEvent);
        g_hDebugEvent = NULL;
    }

    if (g_hShutdownEvent != NULL) {
        CloseHandle(g_hShutdownEvent);
        g_hShutdownEvent = NULL;
    }

    printf("[Broker] Todos los recursos liberados.\n");
}

/* ============================================================================
 * MANEJO DE SENALES DE TERMINACION
 * ============================================================================ */

BOOL ctrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        printf("\n[Broker] Ctrl+C recibido, iniciando apagado...\n");
        cleanup();
        exit(0);
        return TRUE;
    }
    return FALSE;
}

/* ============================================================================
 * INICIALIZACION DEL BUFFER CIRCULAR EN MEMORIA COMPARTIDA
 * ============================================================================ */

BOOL inicializarBufferCircular() {
    printf("[Broker] Creando buffer circular en memoria compartida...\n");

    /* Calcular tamano total necesario para el buffer circular */
    SIZE_T bufferSize = sizeof(CircularBuffer);

    /* Crear el archivo de mapeo (usa el archivo de paginacion de Windows) */
    g_hBufferMapping = CreateFileMapping(
        INVALID_HANDLE_VALUE,    /* Usar archivo de paginacion */
        NULL,                    /* Atributos de seguridad default */
        PAGE_READWRITE,          /* Permisos de lectura y escritura */
        0,                       /* Alto (0 = usar Low) */
        (DWORD)bufferSize,       /* Tamano del buffer */
        BUFFER_NAME              /* Nombre del objeto compartido */
    );

    if (g_hBufferMapping == NULL) {
        printf("[Broker] Error al crear FileMapping: %d\n", GetLastError());
        return FALSE;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        printf("[Broker] El buffer ya existe (posible ejecucion anterior)\n");
    }

    /* Mapear la vista del archivo en nuestro espacio de direcciones */
    g_pBuffer = (CircularBuffer*)MapViewOfFile(
        g_hBufferMapping,            /* Handle al archivo de mapeo */
        FILE_MAP_ALL_ACCESS,         /* Permisos de lectura y escritura */
        0,                           /* Offset alto (0) */
        0,                           /* Offset bajo (0) */
        sizeof(CircularBuffer)       /* Numero de bytes a mapear */
    );

    if (g_pBuffer == NULL) {
        printf("[Broker] Error al mapear vista: %d\n", GetLastError());
        CloseHandle(g_hBufferMapping);
        g_hBufferMapping = NULL;
        return FALSE;
    }

    /* Inicializar el buffer circular */
    memset(g_pBuffer, 0, sizeof(CircularBuffer));
    g_pBuffer->bufferSize = BUFFER_SIZE;
    g_pBuffer->writePos = 0;
    g_pBuffer->readPos = 0;
    g_pBuffer->availableSlots = BUFFER_SIZE;  /* Todos los slots disponibles */
    g_pBuffer->availableData = 0;            /* No hay datos inicialmente */
    g_pBuffer->eventsProcessed = 0;
    g_pBuffer->debugMode = FALSE;
    g_pBuffer->shutdownRequested = FALSE;
    g_pBuffer->activeSensors = 0;

    printf("[Broker] Buffer circular creado: %d slots\n", BUFFER_SIZE);
    return TRUE;
}

/* ============================================================================
 * INICIALIZACION DE SINCRONIZACION
 * ============================================================================ */

BOOL inicializarSincronizacion() {
    printf("[Broker] Creando objetos de sincronizacion...\n");

    /* Crear mutex para exclusion mutua en el buffer */
    /* Inicialmente no pertenecera a ningun hilo (FALSE) */
    g_hMutex = CreateMutex(
        NULL,               /* Atributos de seguridad default */
        FALSE,              /* No pertenece a ninguna hilo inicialmente */
        MUTEX_NAME          /* Nombre del mutex */
    );

    if (g_hMutex == NULL) {
        printf("[Broker] Error al crear mutex: %d\n", GetLastError());
        return FALSE;
    }

    /* Crear semaforo de slots disponibles */
    /* Representa cuantos espacios LIBRES hay en el buffer */
    g_hSemSlots = CreateSemaphore(
        NULL,                       /* Atributos de seguridad default */
        BUFFER_SIZE,                 /* Conde inicial = todos los slots */
        BUFFER_SIZE,                 /* Conde maximo = todos los slots */
        SEMAPHORE_SLOTS_NAME         /* Nombre del semaforo */
    );

    if (g_hSemSlots == NULL) {
        printf("[Broker] Error al crear semaforo de slots: %d\n", GetLastError());
        return FALSE;
    }

    /* Crear semaforo de datos disponibles */
    /* Representa cuentos datos LISTOS para leer hay */
    g_hSemData = CreateSemaphore(
        NULL,               /* Atributos de seguridad default */
        0,                  /* Conde inicial = 0 (sin datos) */
        BUFFER_SIZE,        /* Conde maximo = todos los slots */
        SEMAPHORE_DATA_NAME /* Nombre del semaforo */
    );

    if (g_hSemData == NULL) {
        printf("[Broker] Error al crear semaforo de datos: %d\n", GetLastError());
        return FALSE;
    }

    /* Crear evento de debug (manual-reset, inicial no senalizado) */
    g_hDebugEvent = CreateEvent(
        NULL,               /* Atributos de seguridad default */
        TRUE,               /* Manual reset: necesita ResetEvent */
        FALSE,              /* Estado inicial: no senalizado */
        EVENT_DEBUG_NAME    /* Nombre del evento */
    );

    if (g_hDebugEvent == NULL) {
        printf("[Broker] Error al crear evento de debug: %d\n", GetLastError());
        return FALSE;
    }

    /* Crear evento de apagado (manual-reset, inicial no senalizado) */
    g_hShutdownEvent = CreateEvent(
        NULL,               /* Atributos de seguridad default */
        TRUE,               /* Manual reset */
        FALSE,              /* Estado inicial: no senalizado */
        EVENT_SHUTDOWN_NAME /* Nombre del evento */
    );

    if (g_hShutdownEvent == NULL) {
        printf("[Broker] Error al crear evento de apagado: %d\n", GetLastError());
        return FALSE;
    }

    printf("[Broker] Sincronizacion inicializada.\n");
    return TRUE;
}

/* ============================================================================
 * HILO RECEPTOR DE UN SENSOR
 *
 * Este hilo lee eventos del Named Pipe y los deposita en el buffer circular.
 * Se crea un hilo por cada sensor conectado.
 * ============================================================================ */

DWORD WINAPI hiloReceptor(LPVOID lpParam) {
    ReceiverParams* params = (ReceiverParams*)lpParam;
    SensorEvent evento;
    DWORD bytesRead;
    BOOL resultado;

    printf("[Broker] Hilo receptor iniciado para sensor %d\n", params->sensorId);

    /* Bucle de lectura de eventos */
    while (g_running) {
        /* Leer evento del pipe */
        /* ReadFile es bloqueante si no hay datos disponibles */
        resultado = ReadFile(
            params->hPipe,            /* Handle al pipe */
            &evento,                  /* Buffer donde guardar */
            sizeof(SensorEvent),      /* Tamano a leer */
            &bytesRead,               /* Bytes leidos (output) */
            NULL                      /* No overlapped */
        );

        if (!resultado) {
            DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE) {
                printf("[Broker] Sensor %d desconectado.\n", params->sensorId);
            } else {
                printf("[Broker] Error al leer del pipe: %d\n", error);
            }
            break;
        }

        if (bytesRead != sizeof(SensorEvent)) {
            printf("[Broker] Tamano de evento incorrecto: %lu vs %lu\n",
                   bytesRead, sizeof(SensorEvent));
            continue;
        }

        /* -------------------------------------------------
         * DEPOSITAR EVENTO EN BUFFER CIRCULAR
         * ------------------------------------------------- */

        /* 1. Esperar slot disponible (espacio para escribir) */
        /* Esta llamada BLOQUEA si no hay slots disponibles */
        /* Esto es backpressure natural: si el buffer esta lleno,
           el hilo del sensor se bloquea, retrayendo el flujo */
        resultado = WaitForSingleObject(params->hSemSlots, INFINITE);
        if (resultado != WAIT_OBJECT_0) {
            printf("[Broker] Error esperando slot: %d\n", GetLastError());
            break;
        }

        /* 2. Entrar a seccion critica para acceder al buffer */
        resultado = WaitForSingleObject(params->hMutex, INFINITE);
        if (resultado != WAIT_OBJECT_0) {
            printf("[Broker] Error esperando mutex: %d\n", GetLastError());
            ReleaseSemaphore(params->hSemSlots, 1, NULL);
            break;
        }

        /* 3. Escribir evento en el buffer circular */
        memcpy(&params->hBufferMapping[params->hBufferMapping],
               &evento, sizeof(SensorEvent));

        /* Copiar el evento al buffer usando el indice de escritura */
        CircularBuffer* pBuf = (CircularBuffer*)params->hBufferMapping;
        memcpy(&pBuf->events[pBuf->writePos], &evento, sizeof(SensorEvent));

        /* Avanzar indice de escritura (circular) */
        pBuf->writePos = (pBuf->writePos + 1) % BUFFER_SIZE;

        /* 4. Salir de seccion critica */
        ReleaseMutex(params->hMutex);

        /* 5. Senalar que hay nuevos datos disponibles */
        /* Esto despierta al Dispatcher si estaba esperando */
        ReleaseSemaphore(params->hSemData, 1, NULL);

        /* Imprimir si esta en modo debug */
        if (g_pBuffer->debugMode) {
            printf("[Broker] Sensor %d -> Buffer[slot %d] Evento ID=%lu\n",
                   params->sensorId, (params->hBufferMapping != NULL) ?
                   ((CircularBuffer*)params->hBufferMapping)->writePos : -1,
                   evento.eventId);
        }
    }

    /* Desconectar el pipe */
    DisconnectNamedPipe(params->hPipe);
    CloseHandle(params->hPipe);

    /* Decrementar contador de sensores activos */
    InterlockedDecrement(&g_activeSensors);
    if (g_pBuffer != NULL) {
        InterlockedDecrement(&g_pBuffer->activeSensors);
    }

    /* Liberar memoria de parametros */
    free(params);

    printf("[Broker] Hilo receptor del sensor %d terminado.\n", params->sensorId);
    return 0;
}

/* ============================================================================
 * ESPERA DE CONEXIONES DE SENSORES
 * ============================================================================ */

void esperarConexiones() {
    HANDLE hPipe;
    DWORD threadId;
    char pipeName[MAX_PATH];
    int nextSensorId = 0;

    printf("[Broker] Esperando conexiones de sensores...\n");
    printf("[Broker] Presione Ctrl+C para detener\n\n");

    while (g_running) {
        /* Construir nombre del pipe para el proximo sensor */
        snprintf(pipeName, MAX_PATH, PIPE_NAME_FORMAT, nextSensorId);

        /* Crear instancia del named pipe */
        /* Cada sensor tiene su propio pipe */
        hPipe = CreateNamedPipe(
            pipeName,                             /* Nombre del pipe */
            PIPE_ACCESS_DUPLEX,                   /* Lectura y escritura */
            PIPE_TYPE_BYTE |                      /* Modo byte (no mensaje) */
            PIPE_READMODE_BYTE |                  /* Lectura en modo byte */
            PIPE_WAIT,                            /* Modo bloqueante */
            PIPE_UNLIMITED_INSTANCES,             /* Instancias ilimitadas */
            sizeof(SensorEvent),                  /* Tamano del buffer de salida */
            sizeof(SensorEvent),                  /* Tamano del buffer de entrada */
            TIMEOUT_MS,                           /* Timeout de espera */
            NULL                                  /* Atributos de seguridad default */
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            printf("[Broker] Error al crear pipe: %d\n", GetLastError());
            Sleep(1000);
            continue;
        }

        /* Esperar conexion de un cliente (sensor) */
        /* Esta llamada BLOQUEA hasta que un cliente se conecte */
        printf("[Broker] Esperando conexion en %s...\n", pipeName);

        if (!ConnectNamedPipe(hPipe, NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_PIPE_CONNECTED) {
                printf("[Broker] Error en ConnectNamedPipe: %d\n", error);
                CloseHandle(hPipe);
                continue;
            }
        }

        printf("[Broker] Sensor %d conectado!\n", nextSensorId);

        /* Crear parametros para el hilo receptor */
        ReceiverParams* params = (ReceiverParams*)malloc(sizeof(ReceiverParams));
        if (params == NULL) {
            printf("[Broker] Error: no se pudo asignar memoria\n");
            CloseHandle(hPipe);
            continue;
        }

        params->sensorId = nextSensorId;
        params->hPipe = hPipe;

        /* Obtener handles al buffer compartido */
        /* Estos handles seran heredados por el hilo hijo */
        params->hBufferMapping = g_hBufferMapping;
        params->hMutex = g_hMutex;
        params->hSemSlots = g_hSemSlots;
        params->hSemData = g_hSemData;

        /* Incrementar contador de sensores activos */
        InterlockedIncrement(&g_activeSensors);
        if (g_pBuffer != NULL) {
            InterlockedIncrement(&g_pBuffer->activeSensors);
        }

        /* Crear hilo receptor dedicado para este sensor */
        HANDLE hThread = CreateThread(
            NULL,               /* Atributos de seguridad default */
            0,                  /* Tamano de stack default */
            hiloReceptor,       /* Funcion del hilo */
            params,             /* Parametros del hilo */
            0,                  /* Banderas de creacion (ejecutar inmediatamente) */
            &threadId           /* ID del hilo (output) */
        );

        if (hThread == NULL) {
            printf("[Broker] Error al crear hilo receptor: %d\n", GetLastError());
            CloseHandle(hPipe);
            free(params);
            continue;
        }

        /* Guardar handle del hilo (para limpieza posterior) */
        g_receiverThreads[nextSensorId] = hThread;
        CloseHandle(hThread);  /* No necesitamos el handle, solo lo usamos para esperar */

        /* Siguiente ID de sensor */
        nextSensorId = (nextSensorId + 1) % 256;
    }
}

/* ============================================================================
 * PUNTO DE ENTRADA PRINCIPAL
 * ============================================================================ */

int main() {
    printf("========================================\n");
    printf("  BROKER - Modulo 2\n");
    printf("  Responsabilidad: Jesus Guzman\n");
    printf("========================================\n\n");

    /* Registrar manejador de Ctrl+C */
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)ctrlHandler, TRUE);

    /* Inicializar buffer circular en memoria compartida */
    if (!inicializarBufferCircular()) {
        printf("[Broker] Error al inicializar buffer circular.\n");
        return 1;
    }

    /* Inicializar objetos de sincronizacion */
    if (!inicializarSincronizacion()) {
        printf("[Broker] Error al inicializar sincronizacion.\n");
        cleanup();
        return 1;
    }

    /* Esperar conexiones de sensores */
    esperarConexiones();

    /* Limpieza final */
    cleanup();

    return 0;
}
