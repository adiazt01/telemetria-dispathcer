/*
 * MODULO 4: Monitor del Sistema y Dashboard de Control
 * RESPONSABLE: Jose Marquez
 *
 * Proceso independiente que se conecta al buffer circular (Memoria Compartida)
 * del Broker en modo solo-lectura de estadisticas, y se coordina con el
 * mediante Eventos de Windows (CreateEvent/SetEvent) para activar debug
 * o pedir un apagado controlado del sistema.
 */

#include "common.h"

/* ---- Estado global del modulo ---- */
static HANDLE g_hBufferMapping = NULL;   /* Handle al File Mapping del buffer */
static CircularBuffer* g_pBuffer = NULL; /* Puntero a la memoria compartida */
static HANDLE g_hDebugEvent = NULL;      /* Evento para avisar al Broker: toggle debug */
static HANDLE g_hShutdownEvent = NULL;   /* Evento para avisar al Broker: apagar */
static BOOL g_running = TRUE;            /* Bandera que controla los hilos */
static DWORD g_tickCount = 0;            /* Contador de refrescos del dashboard */

/* Cierra y libera todos los recursos del sistema operativo en orden seguro:
   primero desmapea la vista de memoria, luego cierra cada HANDLE abierto. */
void cleanup() {
    printf("\n[Monitor] Limpiando recursos...\n");
    g_running = FALSE;

    if (g_pBuffer != NULL) { UnmapViewOfFile(g_pBuffer); g_pBuffer = NULL; }
    if (g_hBufferMapping != NULL) { CloseHandle(g_hBufferMapping); g_hBufferMapping = NULL; }
    if (g_hDebugEvent != NULL) { CloseHandle(g_hDebugEvent); g_hDebugEvent = NULL; }
    if (g_hShutdownEvent != NULL) { CloseHandle(g_hShutdownEvent); g_hShutdownEvent = NULL; }

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
   a un puntero local (g_pBuffer) para poder leer el buffer circular. */
BOOL conectarAlBuffer() {
    printf("[Monitor] Conectando al buffer circular...\n");

    g_hBufferMapping = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, BUFFER_NAME);
    if (g_hBufferMapping == NULL) {
        printf("[Monitor] Error al abrir FileMapping: %d\n", GetLastError());
        printf("[Monitor] Asegurese de que el Broker este ejecutandose.\n");
        return FALSE;
    }

    g_pBuffer = (CircularBuffer*)MapViewOfFile(
        g_hBufferMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CircularBuffer));
    if (g_pBuffer == NULL) {
        printf("[Monitor] Error al mapear vista: %d\n", GetLastError());
        CloseHandle(g_hBufferMapping);
        g_hBufferMapping = NULL;
        return FALSE;
    }

    printf("[Monitor] Conectado al buffer.\n");
    return TRUE;
}

/* Abre (no crea) los dos eventos con nombre que el Broker ya declaro,
   para poder dispararlos (SetEvent) y notificarle acciones del operador. */
BOOL conectarEventos() {
    printf("[Monitor] Conectando a eventos del Broker...\n");

    g_hDebugEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, EVENT_DEBUG_NAME);
    if (g_hDebugEvent == NULL) {
        printf("[Monitor] Error al abrir evento de debug: %d\n", GetLastError());
        return FALSE;
    }

    g_hShutdownEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, EVENT_SHUTDOWN_NAME);
    if (g_hShutdownEvent == NULL) {
        printf("[Monitor] Error al abrir evento de apagado: %d\n", GetLastError());
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
   directamente de la memoria compartida (g_pBuffer), sin modificarla. */
void imprimirDashboard() {
    printf("\n\x1B[2J");  /* ANSI: limpiar pantalla */
    printf("============================================\n");
    printf("   MONITOR - Panel de Control en Tiempo Real\n");
    printf("   Responsabilidad: Jose Marquez\n");
    printf("============================================\n\n");

    if (g_pBuffer == NULL) {
        printf("   [ERROR] No conectado al buffer circular.\n");
        return;
    }

    /* --- Estadisticas generales del sistema --- */
    printf("   ESTADISTICAS DEL SISTEMA\n   ------------------------\n");
    printf("   Eventos procesados:     %lu\n", g_pBuffer->eventsProcessed);
    printf("   Sensores activos:       %lu\n", g_pBuffer->activeSensors);
    printf("   Modo debug:            %s\n", g_pBuffer->debugMode ? "ACTIVO" : "inactivo");
    printf("   Apagado solicitado:    %s\n\n", g_pBuffer->shutdownRequested ? "SI" : "NO");

    /* --- Ocupacion del buffer circular (dos barras: usado / libre) --- */
    float ocupacion = 100.0f - (float)g_pBuffer->availableSlots / BUFFER_SIZE * 100.0f;
    float libres = (float)g_pBuffer->availableSlots / BUFFER_SIZE * 100.0f;

    printf("   ESTADO DEL BUFFER CIRCULAR\n   --------------------------\n");
    printf("   Slots en uso:   %3lu / %d\n   ", BUFFER_SIZE - g_pBuffer->availableSlots, BUFFER_SIZE);
    imprimirBarra(ocupacion, 30);
    printf("\n   Slots libres:   %3lu / %d\n   ", g_pBuffer->availableSlots, BUFFER_SIZE);
    imprimirBarra(libres, 30);
    printf("\n\n");

    /* --- Punteros internos del buffer (utiles para depurar) --- */
    printf("   INDICES INTERNOS\n   ----------------\n");
    printf("   Write Pos: %lu\n   Read Pos:  %lu\n\n", g_pBuffer->writePos, g_pBuffer->readPos);

    /* --- Ayuda de teclas disponibles --- */
    printf("   CONTROLES\n   ----------\n");
    printf("   [D] Alternar modo debug (envia senal al Broker)\n");
    printf("   [S] Solicitar apagado del sistema\n");
    printf("   [R] Refrescar estadisticas\n");
    printf("   [ESC] Salir del monitor\n\n");
    printf("   Tick: %lu\n", g_tickCount++);
}

/* Interpreta la tecla presionada por el operador y actua en consecuencia.
   D y S modifican el estado compartido y disparan un Evento de Windows
   para que el Broker reaccione sin usar espera activa (busy waiting). */
void procesarComando(char comando) {
    switch (comando) {
        case 'd': case 'D':
            if (g_pBuffer != NULL) {
                g_pBuffer->debugMode = !g_pBuffer->debugMode;
                SetEvent(g_hDebugEvent);
                printf("\n[Monitor] Modo debug: %s\n", g_pBuffer->debugMode ? "ACTIVADO" : "DESACTIVADO");
            }
            break;
        case 's': case 'S':
            printf("\n[Monitor] Enviando senal de apagado...\n");
            if (g_pBuffer != NULL) g_pBuffer->shutdownRequested = TRUE;
            SetEvent(g_hShutdownEvent);
            break;
        case 'r': case 'R':
            printf("\n[Monitor] Refrescando...\n");
            break;
        case 27:  /* ESC */
            printf("\n[Monitor] Saliendo...\n");
            g_running = FALSE;
            break;
        default:
            printf("\n[Monitor] Comando desconocido: %c\n", comando);
    }
}

/* Hilo dedicado a leer el teclado. Usa WaitForSingleObject con timeout de
   100ms en vez de un while(1) puro: asi el hilo cede CPU entre revisiones
   y puede terminar limpiamente apenas g_running pase a FALSE. */
DWORD WINAPI hiloTeclado(LPVOID lpParam) {
    (void)lpParam;
    INPUT_RECORD input;
    DWORD numEvents;

    while (g_running) {
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
    while (g_running) {
        imprimirDashboard();
        Sleep(1000);
    }
    return 0;
}

int main() {
    HANDLE hTecladoThread, hDashboardThread;
    DWORD threadId;

    printf("========================================\n");
    printf("  MONITOR - Modulo 4\n  Responsabilidad: Jose Marquez\n");
    printf("========================================\n\n");

    /* Ctrl+C tambien debe liberar recursos, no solo el flujo normal */
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)ctrlHandler, TRUE);

    HANDLE hConsole = GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleMode(hConsole, ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);

    /* Sin buffer ni eventos del Broker, el Monitor no tiene nada que hacer */
    if (!conectarAlBuffer() || !conectarEventos()) {
        printf("[Monitor] Asegurese de que el Broker este ejecutandose.\n");
        return 1;
    }

    /* Dos hilos independientes: uno escucha teclado, otro refresca UI */
    hTecladoThread = CreateThread(NULL, 0, hiloTeclado, NULL, 0, &threadId);
    hDashboardThread = CreateThread(NULL, 0, hiloDashboard, NULL, 0, &threadId);
    if (hTecladoThread == NULL || hDashboardThread == NULL) {
        printf("[Monitor] Error al crear hilos: %d\n", GetLastError());
        cleanup();
        return 1;
    }

    printf("[Monitor] Sistema de monitoreo iniciado.\n");
    printf("[Monitor] Presione ESC para salir, D para debug, S para apagar.\n\n");

    /* El programa principal vive hasta que el hilo de teclado detecte ESC */
    WaitForSingleObject(hTecladoThread, INFINITE);

    g_running = FALSE;
    Sleep(200);  /* Da tiempo a que hiloDashboard note el cambio y termine */

    CloseHandle(hTecladoThread);
    CloseHandle(hDashboardThread);
    cleanup();

    printf("[Monitor] Monitor finalizado.\n");
    return 0;
}
