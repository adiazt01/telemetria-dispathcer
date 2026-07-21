/*
 * monitor.c - Modulo 4 (Jose Marquez)
 *
 * Se conecta al buffer compartido del Broker en modo lectura para mostrar
 * estadisticas en tiempo real. Usa eventos de Windows para activar/desactivar
 * el modo debug y enviar senal de apagado.
 */

#include "common.h"

static HANDLE bufferMapping = NULL;
static CircularBuffer* sharedBuffer = NULL;
static HANDLE debugEvent = NULL;
static HANDLE shutdownEvent = NULL;
static BOOL isRunning = TRUE;
static DWORD tickCount = 0;

/* --- limpieza --- */
void cleanup() {
    printf("\n[Monitor] Limpiando recursos...\n");
    isRunning = FALSE;

    if (sharedBuffer != NULL) { UnmapViewOfFile(sharedBuffer); sharedBuffer = NULL; }
    if (bufferMapping != NULL) { CloseHandle(bufferMapping); bufferMapping = NULL; }
    if (debugEvent != NULL) { CloseHandle(debugEvent); debugEvent = NULL; }
    if (shutdownEvent != NULL) { CloseHandle(shutdownEvent); shutdownEvent = NULL; }

    printf("[Monitor] Recursos liberados.\n");
}

/* Handler de Ctrl+C: garantiza que aunque el usuario cierre a la fuerza,
   los handles se liberen igual (cierre limpio exigido por el proyecto). */
BOOL ctrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        printf("\n[Monitor] Ctrl+C recibido.\n");
        cleanup();
        exit(0);
    }
    return FALSE;
}

/* Abre el File Mapping que el Broker ya creo, y mapea su contenido
   a un puntero local (sharedBuffer) para poder leer el buffer circular. */
BOOL conectarAlBuffer() {
    printf("[Monitor] Conectando al buffer circular...\n");

    bufferMapping = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, BUFFER_NAME);
    if (bufferMapping == NULL) {
        printf("[Monitor] Error al abrir FileMapping: %lu\n", GetLastError());
        printf("[Monitor] Asegurese de que el Broker este ejecutandose.\n");
        return FALSE;
    }

    sharedBuffer = (CircularBuffer*)MapViewOfFile(
        bufferMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CircularBuffer));
    if (sharedBuffer == NULL) {
        printf("[Monitor] Error al mapear vista: %lu\n", GetLastError());
        CloseHandle(bufferMapping);
        bufferMapping = NULL;
        return FALSE;
    }

    printf("[Monitor] Conectado al buffer.\n");
    return TRUE;
}

/* Abre (no crea) los dos eventos con nombre que el Broker ya declaro,
   para poder dispararlos (SetEvent) y notificarle acciones del operador. */
BOOL conectarEventos() {
    printf("[Monitor] Conectando a eventos del Broker...\n");

    debugEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, EVENT_DEBUG_NAME);
    if (debugEvent == NULL) {
        printf("[Monitor] Error al abrir evento de debug: %lu\n", GetLastError());
        return FALSE;
    }

    shutdownEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, EVENT_SHUTDOWN_NAME);
    if (shutdownEvent == NULL) {
        printf("[Monitor] Error al abrir evento de apagado: %lu\n", GetLastError());
        return FALSE;
    }

    printf("[Monitor] Eventos conectados.\n");
    return TRUE;
}

/* Dibuja una barra tipo [====----] con el porcentaje al lado. */
void imprimirBarra(float porcentaje, int ancho) {
    int llenos = (int)(porcentaje / 100.0f * ancho);
    printf("[");
    for (int i = 0; i < ancho; i++) printf(i < llenos ? "=" : "-");
    printf("] %5.1f%%", porcentaje);
}

/* Renderiza el dashboard completo en consola: estadisticas leidas
   directamente de la memoria compartida (sharedBuffer), sin modificarla. */
void imprimirDashboard() {
    printf("\n\x1B[2J");  /* ANSI: limpiar pantalla */
    printf("============================================\n");
    printf("   MONITOR - Panel de Control en Tiempo Real\n");
    printf("   Responsabilidad: Jose Marquez\n");
    printf("============================================\n\n");

    if (sharedBuffer == NULL) {
        printf("   [ERROR] No conectado al buffer circular.\n");
        return;
    }

    /* --- Estadisticas generales del sistema --- */
    printf("   ESTADISTICAS DEL SISTEMA\n   ------------------------\n");
    printf("   Eventos procesados:     %lu\n", sharedBuffer->eventsProcessed);
    printf("   Sensores activos:       %lu\n", sharedBuffer->activeSensors);
    printf("   Modo debug:            %s\n", sharedBuffer->debugMode ? "ACTIVO" : "inactivo");
    printf("   Apagado solicitado:    %s\n\n", sharedBuffer->shutdownRequested ? "SI" : "NO");

    /* --- Ocupacion del buffer circular (dos barras: usado / libre) --- */
    float ocupacion = 100.0f - (float)sharedBuffer->availableSlots / BUFFER_SIZE * 100.0f;
    float libres = (float)sharedBuffer->availableSlots / BUFFER_SIZE * 100.0f;

    printf("   ESTADO DEL BUFFER CIRCULAR\n   --------------------------\n");
    printf("   Slots en uso:   %3lu / %d\n   ", BUFFER_SIZE - sharedBuffer->availableSlots, BUFFER_SIZE);
    imprimirBarra(ocupacion, 30);
    printf("\n   Slots libres:   %3lu / %d\n   ", sharedBuffer->availableSlots, BUFFER_SIZE);
    imprimirBarra(libres, 30);
    printf("\n\n");

    /* --- Punteros internos del buffer (utiles para depurar) --- */
    printf("   INDICES INTERNOS\n   ----------------\n");
    printf("   Write Pos: %lu\n   Read Pos:  %lu\n\n", sharedBuffer->writePos, sharedBuffer->readPos);

    /* --- Ayuda de teclas disponibles --- */
    printf("   CONTROLES\n   ----------\n");
    printf("   [D] Alternar modo debug (envia senal al Broker)\n");
    printf("   [S] Solicitar apagado del sistema\n");
    printf("   [R] Refrescar estadisticas\n");
    printf("   [ESC] Salir del monitor\n\n");
    printf("   Tick: %lu\n", tickCount++);
}

/* Interpreta la tecla presionada por el operador y actua en consecuencia.
   D y S modifican el estado compartido y disparan un Evento de Windows
   para que el Broker reaccione sin usar espera activa (busy waiting). */
void procesarComando(char comando) {
    switch (comando) {
        case 'd': case 'D':
            if (sharedBuffer != NULL) {
                sharedBuffer->debugMode = !sharedBuffer->debugMode;
                SetEvent(debugEvent);
                printf("\n[Monitor] Modo debug: %s\n", sharedBuffer->debugMode ? "ACTIVADO" : "DESACTIVADO");
            }
            break;
        case 's': case 'S':
            printf("\n[Monitor] Enviando senal de apagado...\n");
            if (sharedBuffer != NULL) sharedBuffer->shutdownRequested = TRUE;
            SetEvent(shutdownEvent);
            break;
        case 'r': case 'R':
            printf("\n[Monitor] Refrescando...\n");
            break;
        case 27:  /* ESC */
            printf("\n[Monitor] Saliendo...\n");
            isRunning = FALSE;
            break;
        default:
            printf("\n[Monitor] Comando desconocido: %c\n", comando);
    }
}

/* Hilo dedicado a leer el teclado. Usa WaitForSingleObject con timeout de
   100ms en vez de un while(1) puro: asi el hilo cede CPU entre revisiones
   y puede terminar limpiamente apenas isRunning pase a FALSE. */
DWORD WINAPI hiloTeclado(LPVOID lpParam) {
    (void)lpParam;
    INPUT_RECORD input;
    DWORD numEvents;

    while (isRunning) {
        if (WaitForSingleObject(GetStdHandle(STD_INPUT_HANDLE), 100) == WAIT_OBJECT_0) {
            ReadConsoleInput(GetStdHandle(STD_INPUT_HANDLE), &input, 1, &numEvents);
            if (input.EventType == KEY_EVENT && input.Event.KeyEvent.bKeyDown)
                procesarComando(input.Event.KeyEvent.wVirtualKeyCode);
        }
    }
    return 0;
}

/* Hilo dedicado a refrescar el dashboard cada segundo. Corre en paralelo
   al hilo de teclado para que la UI no se congele esperando una tecla. */
DWORD WINAPI hiloDashboard(LPVOID lpParam) {
    (void)lpParam;
    while (isRunning) {
        imprimirDashboard();
        Sleep(1000);
    }
    return 0;
}

int main() {
    HANDLE keyboardThread, dashboardThread;
    DWORD threadId;

    printf("========================================\n");
    printf("  MONITOR - Modulo 4\n  Responsabilidad: Jose Marquez\n");
    printf("========================================\n\n");

    /* Ctrl+C tambien debe liberar recursos, no solo el flujo normal */
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)ctrlHandler, TRUE);

    HANDLE consoleHandle = GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleMode(consoleHandle, ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);

    /* Sin buffer ni eventos del Broker, el Monitor no tiene nada que hacer */
    if (!conectarAlBuffer() || !conectarEventos()) {
        printf("[Monitor] Asegurese de que el Broker este ejecutandose.\n");
        return 1;
    }

    /* Dos hilos independientes: uno escucha teclado, otro refresca UI */
    keyboardThread = CreateThread(NULL, 0, hiloTeclado, NULL, 0, &threadId);
    dashboardThread = CreateThread(NULL, 0, hiloDashboard, NULL, 0, &threadId);
    if (keyboardThread == NULL || dashboardThread == NULL) {
        printf("[Monitor] Error al crear hilos: %lu\n", GetLastError());
        cleanup();
        return 1;
    }

    printf("[Monitor] Sistema de monitoreo iniciado.\n");
    printf("[Monitor] Presione ESC para salir, D para debug, S para apagar.\n\n");

    /* El programa principal vive hasta que el hilo de teclado detecte ESC */
    WaitForSingleObject(keyboardThread, INFINITE);

    isRunning = FALSE;
    Sleep(200);  /* Da tiempo a que hiloDashboard note el cambio y termine */

    CloseHandle(keyboardThread);
    CloseHandle(dashboardThread);
    cleanup();

    printf("[Monitor] Monitor finalizado.\n");
    return 0;
}
