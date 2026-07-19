/*
 * ============================================================================
 * MODULO 4: Monitor del Sistema y Dashboard de Control
 * RESPONSABLE: Jose Marquez
 * ============================================================================
 *
 * monitor.c - Consola de administracion en tiempo real
 *
 * Este modulo es un proceso completamente independiente que actua como la
 * consola de administracion del sistema. Sus funciones son:
 *
 * 1. Leer estadisticas del buffer circular compartido:
 *    - Eventos procesados
 *    - Tasa de ocupacion del buffer
 *    - Numero de sensores activos
 *
 * 2. Coordinarse con el Broker mediante Eventos de Windows:
 *    - Activar/desactivar modo debug
 *    - Iniciar protocolo de apagado controlado
 *
 * 3. Mostrar dashboard en tiempo real con informacion del sistema.
 *
 * Comunicacion:
 *   - Lectura: Memory-Mapped File (buffer compartido)
 *   - Control: Eventos de Windows (CreateEvent, SetEvent)
 * ============================================================================
 */

#include "common.h"

/* Handle al archivo de mapeo del buffer */
static HANDLE g_hBufferMapping = NULL;

/* Puntero al buffer circular */
static CircularBuffer* g_pBuffer = NULL;

/* Handle al evento de debug */
static HANDLE g_hDebugEvent = NULL;

/* Handle al evento de apagado */
static HANDLE g_hShutdownEvent = NULL;

/* Bandera de ejecucion */
static BOOL g_running = TRUE;

/* Contador de ticks del dashboard */
static DWORD g_tickCount = 0;

/* ============================================================================
 * LIMPIEZA DE RECURSOS
 * ============================================================================ */

void cleanup() {
    printf("\n[Monitor] Limpiando recursos...\n");

    g_running = FALSE;

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

    if (g_hDebugEvent != NULL) {
        CloseHandle(g_hDebugEvent);
        g_hDebugEvent = NULL;
    }

    if (g_hShutdownEvent != NULL) {
        CloseHandle(g_hShutdownEvent);
        g_hShutdownEvent = NULL;
    }

    printf("[Monitor] Recursos liberados.\n");
}

/* ============================================================================
 * MANEJO DE SENALES
 * ============================================================================ */

BOOL ctrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        printf("\n[Monitor] Ctrl+C recibido.\n");
        cleanup();
        exit(0);
        return TRUE;
    }
    return FALSE;
}

/* ============================================================================
 * CONEXION AL BUFFER COMPARTIDO
 * ============================================================================ */

BOOL conectarAlBuffer() {
    printf("[Monitor] Conectando al buffer circular...\n");

    /* Abrir el archivo de mapeo existente */
    g_hBufferMapping = OpenFileMapping(
        FILE_MAP_ALL_ACCESS,
        FALSE,
        BUFFER_NAME
    );

    if (g_hBufferMapping == NULL) {
        printf("[Monitor] Error al abrir FileMapping: %d\n", GetLastError());
        printf("[Monitor] Asegurese de que el Broker este ejecutandose.\n");
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
        printf("[Monitor] Error al mapear vista: %d\n", GetLastError());
        CloseHandle(g_hBufferMapping);
        g_hBufferMapping = NULL;
        return FALSE;
    }

    printf("[Monitor] Conectado al buffer.\n");
    return TRUE;
}

/* ============================================================================
 * CONEXION A EVENTOS DEL BROKER
 * ============================================================================ */

BOOL conectarEventos() {
    printf("[Monitor] Conectando a eventos del Broker...\n");

    /* Abrir evento de debug */
    g_hDebugEvent = OpenEvent(
        EVENT_ALL_ACCESS,
        FALSE,
        EVENT_DEBUG_NAME
    );

    if (g_hDebugEvent == NULL) {
        printf("[Monitor] Error al abrir evento de debug: %d\n", GetLastError());
        return FALSE;
    }

    /* Abrir evento de apagado */
    g_hShutdownEvent = OpenEvent(
        EVENT_ALL_ACCESS,
        FALSE,
        EVENT_SHUTDOWN_NAME
    );

    if (g_hShutdownEvent == NULL) {
        printf("[Monitor] Error al abrir evento de apagado: %d\n", GetLastError());
        return FALSE;
    }

    printf("[Monitor] Eventos conectados.\n");
    return TRUE;
}

/* ============================================================================
 * IMPRIMIR BARRA DE PROGRESO
 * ============================================================================ */

void imprimirBarra(float porcentaje, int ancho) {
    int caracteres = (int)(porcentaje / 100.0f * ancho);
    printf("[");
    for (int i = 0; i < ancho; i++) {
        if (i < caracteres) {
            printf("=");
        } else {
            printf("-");
        }
    }
    printf("] %5.1f%%", porcentaje);
}

/* ============================================================================
 * IMPRIMIR DASHBOARD
 * ============================================================================ */

void imprimirDashboard() {
    /* Limpiar pantalla (simulado con nuevas lineas) */
    printf("\n");
    printf("\x1B[2J");  /* ANSI: clear screen */

    printf("============================================\n");
    printf("   MONITOR - Panel de Control en Tiempo Real\n");
    printf("   Responsabilidad: Jose Marquez\n");
    printf("============================================\n");
    printf("\n");

    if (g_pBuffer == NULL) {
        printf("   [ERROR] No conectado al buffer circular.\n");
        return;
    }

    /* Estadisticas principales */
    printf("   ESTADISTICAS DEL SISTEMA\n");
    printf("   ------------------------\n");
    printf("   Eventos procesados:     %lu\n", g_pBuffer->eventsProcessed);
    printf("   Sensores activos:       %lu\n", g_pBuffer->activeSensors);
    printf("   Modo debug:            %s\n",
           g_pBuffer->debugMode ? "ACTIVO" : "inactivo");
    printf("   Apagado solicitado:    %s\n",
           g_pBuffer->shutdownRequested ? "SI" : "NO");
    printf("\n");

    /* Estado del buffer circular */
    printf("   ESTADO DEL BUFFER CIRCULAR\n");
    printf("   --------------------------\n");

    float ocupacion = 100.0f - (float)g_pBuffer->availableSlots / BUFFER_SIZE * 100.0f;
    float espaciosLibres = (float)g_pBuffer->availableSlots / BUFFER_SIZE * 100.0f;

    printf("   Slots en uso:   %3lu / %d\n",
           BUFFER_SIZE - g_pBuffer->availableSlots, BUFFER_SIZE);
    printf("   ");
    imprimirBarra(ocupacion, 30);
    printf("\n");

    printf("   Slots libres:   %3lu / %d\n",
           g_pBuffer->availableSlots, BUFFER_SIZE);
    printf("   ");
    imprimirBarra(espaciosLibres, 30);
    printf("\n");
    printf("\n");

    /* Indice de escritura/lectura (debug) */
    printf("   INDICES INTERNOS\n");
    printf("   ----------------\n");
    printf("   Write Pos: %lu\n", g_pBuffer->writePos);
    printf("   Read Pos:  %lu\n", g_pBuffer->readPos);
    printf("\n");

    /* Leyenda de controles */
    printf("   CONTROLES\n");
    printf("   ----------\n");
    printf("   [D] Alternar modo debug (envia seal al Broker)\n");
    printf("   [S] Solicitar apagado del sistema\n");
    printf("   [R] Refrescar estadisticas\n");
    printf("   [ESC] Salir del monitor\n");
    printf("\n");

    /* Contador de ticks */
    printf("   Tick: %lu\n", g_tickCount++);
}

/* ============================================================================
 * PROCESAR COMANDO DEL USUARIO
 * ============================================================================ */

void procesarComando(char comando) {
    switch (comando) {
        case 'd':
        case 'D':
            /* Alternar modo debug */
            if (g_pBuffer != NULL) {
                g_pBuffer->debugMode = !g_pBuffer->debugMode;
                SetEvent(g_hDebugEvent);  /* Notificar al Broker */
                printf("\n[Monitor] Modo debug: %s\n",
                       g_pBuffer->debugMode ? "ACTIVADO" : "DESACTIVADO");
            }
            break;

        case 's':
        case 'S':
            /* Solicitar apagado */
            printf("\n[Monitor] Enviando senal de apagado...\n");
            if (g_pBuffer != NULL) {
                g_pBuffer->shutdownRequested = TRUE;
            }
            SetEvent(g_hShutdownEvent);
            break;

        case 'r':
        case 'R':
            /* Forzar refresh */
            printf("\n[Monitor] Refrescando...\n");
            break;

        case 27:  /* ESC */
            printf("\n[Monitor] Saliendo...\n");
            g_running = FALSE;
            break;

        default:
            printf("\n[Monitor] Comando desconocido: %c\n", comando);
            break;
    }
}

/* ============================================================================
 * HILO DE LECTURA DE TECLADO (no bloqueante)
 * ============================================================================ */

DWORD WINAPI hiloTeclado(LPVOID lpParam) {
    (void)lpParam;
    INPUT_RECORD input;
    DWORD numEvents;

    while (g_running) {
        /* Verificar si hay entrada sin bloquear */
        if (WaitForSingleObject(GetStdHandle(STD_INPUT_HANDLE), 100) == WAIT_OBJECT_0) {
            ReadConsoleInput(GetStdHandle(STD_INPUT_HANDLE), &input, 1, &numEvents);

            if (input.EventType == KEY_EVENT && input.KeyEvent.bKeyDown) {
                procesarComando(input.KeyEvent.wVirtualKeyCode);
            }
        }
    }

    return 0;
}

/* ============================================================================
 * HILO PRINCIPAL DE ACTUALIZACION DEL DASHBOARD
 * ============================================================================ */

DWORD WINAPI hiloDashboard(LPVOID lpParam) {
    (void)lpParam;

    while (g_running) {
        /* Imprimir dashboard */
        imprimirDashboard();

        /* Esperar 1 segundo */
        Sleep(1000);
    }

    return 0;
}

/* ============================================================================
 * PUNTO DE ENTRADA PRINCIPAL
 * ============================================================================ */

int main() {
    HANDLE hTecladoThread;
    HANDLE hDashboardThread;
    DWORD threadId;

    printf("========================================\n");
    printf("  MONITOR - Modulo 4\n");
    printf("  Responsabilidad: Jose Marquez\n");
    printf("========================================\n\n");

    /* Registrar manejador de Ctrl+C */
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)ctrlHandler, TRUE);

    /* Configurar consola para entrada sin espera */
    HANDLE hConsole = GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleMode(hConsole, ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);

    /* Conectar al buffer compartido */
    if (!conectarAlBuffer()) {
        printf("[Monitor] No se pudo conectar al buffer.\n");
        printf("[Monitor] Asegurese de que el Broker este ejecutandose.\n");
        return 1;
    }

    /* Conectar a los eventos del Broker */
    if (!conectarEventos()) {
        printf("[Monitor] No se pudieron abrir los eventos.\n");
        printf("[Monitor] Asegurese de que el Broker este ejecutandose.\n");
        return 1;
    }

    /* Crear hilo para lectura de teclado (no bloqueante) */
    hTecladoThread = CreateThread(NULL, 0, hiloTeclado, NULL, 0, &threadId);
    if (hTecladoThread == NULL) {
        printf("[Monitor] Error al crear hilo de teclado: %d\n", GetLastError());
        cleanup();
        return 1;
    }

    /* Crear hilo del dashboard */
    hDashboardThread = CreateThread(NULL, 0, hiloDashboard, NULL, 0, &threadId);
    if (hDashboardThread == NULL) {
        printf("[Monitor] Error al crear hilo de dashboard: %d\n", GetLastError());
        cleanup();
        return 1;
    }

    printf("[Monitor] Sistema de monitoreo iniciado.\n");
    printf("[Monitor] Presione ESC para salir, D para debug, S para apagar.\n\n");

    /* Esperar a que el usuario presione ESC */
    /* El hilo del dashboard se encarga de la actualizacion */

    /* Esperar a que alguienEnvie senal de apagado o Ctrl+C */
    WaitForSingleObject(hTecladoThread, INFINITE);

    /* Limpiar */
    g_running = FALSE;
    Sleep(200);  /* Dar tiempo a los otros hilos para terminar */

    CloseHandle(hTecladoThread);
    CloseHandle(hDashboardThread);

    cleanup();

    printf("[Monitor] Monitor finalizado.\n");
    return 0;
}
