/*
 * ============================================================================
 * MODULO 1: Subsistema de Sensores
 * RESPONSABLE: Carlos Brito
 * ============================================================================
 *
 * sensor.c - Simulador de sensores del vehiculo
 *
 * Este modulo representa un sensor fisico del vehiculo (Motor, Neumaticos,
 * Frenos o GPS). Puede instanciarse N veces en paralelo, cada vez simulando
 * un sensor diferente. Genera eventos con datos aleatorios y los envia al
 * Broker a traves de un Named Pipe en modo bloqueante.
 *
 * Comunicacion: Named Pipe cliente -> Broker
 * ============================================================================
 */

#include "common.h"

/* Identificador unico de este sensor */
static DWORD g_sensorId = 0;
/* Tipo de sensor simulado */
static SensorType g_sensorType = SENSOR_MOTOR;
/* Nombre del pipe de este sensor */
static char g_pipeName[MAX_PATH];
/* Handle al pipe */
static HANDLE g_hPipe = INVALID_HANDLE_VALUE;
/* Bandera de ejecucion */
static BOOL g_running = TRUE;
/* Contador de eventos enviados */
static DWORD g_eventsSent = 0;

/* ============================================================================
 * IMPRESION DE USO
 * ============================================================================ */

void imprimirUso(const char* p) {
    printf("========================================\n  SENSOR - Modulo 1 (Carlos Brito)\n========================================\n\nUso: %s <id_sensor> <tipo_sensor>\n\n  id_sensor:    Identificador unico del sensor (0-255)\n  tipo_sensor:  Tipo de sensor a simular:\n                0 = Motor\n                1 = Neumaticos\n                2 = Frenos\n                3 = GPS\n\nEjemplo: %s 1 0\n         Crea un sensor de MOTOR con ID=1\n\n", p, p);
}

/* ============================================================================
 * CONEXION AL NAMED PIPE
 * ============================================================================ */

BOOL conectarAlBroker() {
    printf("[Sensor %d] Conectando al Broker...\n", g_sensorId);
    /* Intentamos conectarnos al pipe del sensor especifico */
    /* Esperamos hasta que el Broker cree el pipe */
    while (g_running) {
        g_hPipe = CreateFile(g_pipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (g_hPipe != INVALID_HANDLE_VALUE) { printf("[Sensor %d] Conectado al Broker exitosamente!\n", g_sensorId); return TRUE; }
        /* Si no existe el pipe, esperamos un poco y reintentamos */
        if (GetLastError() != ERROR_FILE_NOT_FOUND) { printf("[Sensor %d] Error al conectar: %d\n", g_sensorId, GetLastError()); Sleep(100); }
        /* Evitar saturar la CPU */
        Sleep(50);
    }
    return FALSE;
}

/* ============================================================================
 * ENVIO DE EVENTO AL BROKER
 * ============================================================================ */

BOOL enviarEvento(const SensorEvent* evento) {
    DWORD bytesWritten = 0;
    /* Escribimos el evento en el pipe */
    /* WriteFile es bloqueante porque el pipe esta en modo PIPE_WAIT */
    if (!WriteFile(g_hPipe, evento, sizeof(SensorEvent), &bytesWritten, NULL) || bytesWritten != sizeof(SensorEvent)) {
        printf("[Sensor %d] Error al enviar evento: %d\n", g_sensorId, GetLastError());
        return FALSE;
    }
    g_eventsSent++;
    return TRUE;
}

/* ============================================================================
 * GENERACION DE EVENTO
 * ============================================================================ */

void generarEvento(SensorEvent* evento) {
    static DWORD g_eventCounter = 0;
    /* ID unico incremental */
    evento->eventId = g_eventCounter++;
    /* ID y tipo de este sensor */
    evento->sensorId = g_sensorId; evento->sensorType = g_sensorType;
    /* Timestamp de alta resolucion */
    obtenerTimestampActual(&evento->timestamp);
    /* Prioridad basada en el tipo de sensor */
    /* Motor y Frenos son criticos, GPS y Neumaticos son normales */
    switch (g_sensorType) {
        case SENSOR_MOTOR: case SENSOR_FRENOS: evento->priority = PRIORITY_HIGH; break;
        case SENSOR_NEMATICOS: evento->priority = PRIORITY_NORMAL; break;
        case SENSOR_GPS: evento->priority = PRIORITY_LOW; break;
        default: evento->priority = PRIORITY_NORMAL;
    }
    /* Generamos el payload con datos aleatorios */
    evento->payloadSize = MAX_PAYLOAD_SIZE; generarPayload(evento->payload, evento->payloadSize, g_sensorType);
}

/* ============================================================================
 * MANEJO DE SEÑALES DE TECLADO
 * ============================================================================ */

BOOL controlarTecla(DWORD waitTime) {
    /* Verificamos si hay tecla presionada sin bloquear */
    if (WaitForSingleObject(GetStdHandle(STD_INPUT_HANDLE), waitTime) == WAIT_OBJECT_0) {
        INPUT_RECORD input; DWORD numEvents;
        ReadConsoleInput(GetStdHandle(STD_INPUT_HANDLE), &input, 1, &numEvents);
        if (input.EventType == KEY_EVENT && input.Event.KeyEvent.bKeyDown && input.Event.KeyEvent.wVirtualKeyCode == VK_ESCAPE) {
            /* ESC para salir */
            printf("\n[Sensor %d] Solicitud de apagado recibida...\n", g_sensorId); g_running = FALSE; return FALSE;
        }
    }
    return TRUE;
}

/* ============================================================================
 * LIMPIEZA DE RECURSOS
 * ============================================================================ */

void limpiarRecursos() {
    printf("[Sensor %d] Limpiando recursos...\n", g_sensorId);
    /* Cerrar el pipe si esta abierto */
    if (g_hPipe != INVALID_HANDLE_VALUE) { CloseHandle(g_hPipe); g_hPipe = INVALID_HANDLE_VALUE; }
    printf("[Sensor %d] Recursos liberados. Eventos enviados: %lu\n", g_sensorId, g_eventsSent);
}

/* ============================================================================
 * Punto de entrada principal
 * ============================================================================ */

int main(int argc, char* argv[]) {
    SensorEvent evento; DWORD sleepTime;
    printf("========================================\n  SENSOR - Modulo 1\n  Responsabilidad: Carlos Brito\n========================================\n\n");
    /* Validar argumentos de linea de comandos */
    if (argc != 3) { imprimirUso(argv[0]); return 1; }
    /* Parsear ID del sensor */
    if ((g_sensorId = atoi(argv[1])) > 255) { printf("Error: ID del sensor debe ser 0-255\n"); return 1; }
    /* Parsear tipo de sensor */
    g_sensorType = atoi(argv[2]);
    if (g_sensorType < 0 || g_sensorType > 3) { printf("Error: Tipo de sensor debe ser 0-3\n"); return 1; }
	/* Usar el nombre estandar del pipe */
    strncpy(g_pipeName, PIPE_NAME, MAX_PATH);
    printf("[Sensor %d] Tipo: %s\n[Sensor %d] Pipe: %s\n", g_sensorId, obtenerNombreSensor(g_sensorType), g_sensorId, g_pipeName);
    /* Configurar modo de consola para detectar ESC */
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    /* Conectar al Broker */
    if (!conectarAlBroker()) { printf("[Sensor %d] No se pudo conectar al Broker\n", g_sensorId); return 1; }
    /* Configurar semilla aleatoria */
    srand((unsigned int)(GetTickCount() + g_sensorId * 1000));
    /* Bucle principal de envio de eventos */
    printf("[Sensor %d] Iniciando envio de eventos...\n[Sensor %d] Presione ESC para detener\n", g_sensorId, g_sensorId);
    while (g_running) {
        /* Generar evento */
        generarEvento(&evento);
        /* Imprimir evento (debug) */
        printf("[Sensor %d] Enviando evento ID=%lu Priority=%s\n", g_sensorId, evento.eventId, obtenerNombrePrioridad(evento.priority));
        /* Enviar al Broker */
        if (!enviarEvento(&evento)) {
            printf("[Sensor %d] Error al enviar, reintentando...\n", g_sensorId);
            /* Intentar reconectar */
            CloseHandle(g_hPipe); g_hPipe = INVALID_HANDLE_VALUE;
            if (!conectarAlBroker()) break;
            continue;
        }
        /* Tiempo aleatorio entre envios (100-500ms) para simular diferentes frecuencias de muestreo de sensores reales */
        sleepTime = 100 + rand() % 400;
        /* Verificar si se presiono ESC mientras dormimos */
        if (!controlarTecla(sleepTime)) break;
    }
    /* Limpieza final */
    limpiarRecursos();
    printf("[Sensor %d] Sensor finalizado.\n", g_sensorId);
    return 0;
}