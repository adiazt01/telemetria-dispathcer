/*
 * sensor.c - Modulo 1 (Carlos Brito)
 *
 * Simula sensores del vehiculo (Motor, Neumaticos, Frenos, GPS).
 * Se pueden lanzar varias instancias, cada una genera eventos con datos
 * aleatorios y los envia al Broker por un Named Pipe.
 */

#include "common.h"

static DWORD sensorId = 0;
static SensorType sensorType = SENSOR_MOTOR;
static char pipeName[MAX_PATH];
static HANDLE pipeHandle = INVALID_HANDLE_VALUE;
static BOOL isRunning = TRUE;
static DWORD eventsSent = 0;

/* --- uso --- */

void imprimirUso(const char* p) {
    printf("========================================\n  SENSOR - Modulo 1 (Carlos Brito)\n========================================\n\nUso: %s <id_sensor> <tipo_sensor>\n\n  id_sensor:    Identificador unico del sensor (0-255)\n  tipo_sensor:  Tipo de sensor a simular:\n                0 = Motor\n                1 = Neumaticos\n                2 = Frenos\n                3 = GPS\n\nEjemplo: %s 1 0\n         Crea un sensor de MOTOR con ID=1\n\n", p, p);
}

/* --- conectar al broker --- */

BOOL conectarAlBroker() {
    printf("[Sensor %lu] Conectando al Broker...\n", sensorId);
    while (isRunning) {
        pipeHandle = CreateFile(pipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (pipeHandle != INVALID_HANDLE_VALUE) { printf("[Sensor %lu] Conectado al Broker exitosamente!\n", sensorId); return TRUE; }
        if (GetLastError() != ERROR_FILE_NOT_FOUND) { printf("[Sensor %lu] Error al conectar: %lu\n", sensorId, GetLastError()); Sleep(100); }
        Sleep(50);
    }
    return FALSE;
}

/* --- enviar evento por el pipe --- */

BOOL enviarEvento(const SensorEvent* evento) {
    DWORD bytesWritten = 0;
    if (!WriteFile(pipeHandle, evento, sizeof(SensorEvent), &bytesWritten, NULL) || bytesWritten != sizeof(SensorEvent)) {
        printf("[Sensor %lu] Error al enviar evento: %lu\n", sensorId, GetLastError());
        return FALSE;
    }
    eventsSent++;
    return TRUE;
}

/* --- generar evento con datos aleatorios --- */

void generarEvento(SensorEvent* evento) {
    static DWORD eventCounter = 0;
    evento->eventId = eventCounter++;
    evento->sensorId = sensorId; evento->sensorType = sensorType;
    obtenerTimestampActual(&evento->timestamp);

    switch (sensorType) {
        case SENSOR_MOTOR: case SENSOR_FRENOS: evento->priority = PRIORITY_HIGH; break;
        case SENSOR_NEMATICOS: evento->priority = PRIORITY_NORMAL; break;
        case SENSOR_GPS: evento->priority = PRIORITY_LOW; break;
        default: evento->priority = PRIORITY_NORMAL;
    }

    evento->payloadSize = MAX_PAYLOAD_SIZE; generarPayload(evento->payload, evento->payloadSize, sensorType);
}

/* --- detectar ESC sin bloquear --- */

BOOL controlarTecla(DWORD waitTime) {
    if (WaitForSingleObject(GetStdHandle(STD_INPUT_HANDLE), waitTime) == WAIT_OBJECT_0) {
        INPUT_RECORD input; DWORD numEvents;
        ReadConsoleInput(GetStdHandle(STD_INPUT_HANDLE), &input, 1, &numEvents);
        if (input.EventType == KEY_EVENT && input.Event.KeyEvent.bKeyDown && input.Event.KeyEvent.wVirtualKeyCode == VK_ESCAPE) {
            printf("\n[Sensor %lu] Solicitud de apagado recibida...\n", sensorId); isRunning = FALSE; return FALSE;
        }
    }
    return TRUE;
}

/* --- limpiar recursos --- */

void limpiarRecursos() {
    printf("[Sensor %lu] Limpiando recursos...\n", sensorId);
    if (pipeHandle != INVALID_HANDLE_VALUE) { CloseHandle(pipeHandle); pipeHandle = INVALID_HANDLE_VALUE; }
    printf("[Sensor %lu] Recursos liberados. Eventos enviados: %lu\n", sensorId, eventsSent);
}

/* --- main --- */

int main(int argc, char* argv[]) {
    SensorEvent evento; DWORD sleepTime;
    printf("========================================\n  SENSOR - Modulo 1\n  Responsabilidad: Carlos Brito\n========================================\n\n");

    if (argc != 3) { imprimirUso(argv[0]); return 1; }
    if ((sensorId = atoi(argv[1])) > 255) { printf("Error: ID del sensor debe ser 0-255\n"); return 1; }
    sensorType = atoi(argv[2]);
    if (sensorType < 0 || sensorType > 3) { printf("Error: Tipo de sensor debe ser 0-3\n"); return 1; }

    strncpy(pipeName, PIPE_NAME, MAX_PATH);
    printf("[Sensor %lu] Tipo: %s\n[Sensor %lu] Pipe: %s\n", sensorId, obtenerNombreSensor(sensorType), sensorId, pipeName);

    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);

    if (!conectarAlBroker()) { printf("[Sensor %lu] No se pudo conectar al Broker\n", sensorId); return 1; }

    srand((unsigned int)(GetTickCount() + sensorId * 1000));

    printf("[Sensor %lu] Iniciando envio de eventos...\n[Sensor %lu] Presione ESC para detener\n", sensorId, sensorId);
    while (isRunning) {
        generarEvento(&evento);
        printf("[Sensor %lu] Enviando evento ID=%lu Priority=%s\n", sensorId, evento.eventId, obtenerNombrePrioridad(evento.priority));
        if (!enviarEvento(&evento)) {
            printf("[Sensor %lu] Error al enviar, reintentando...\n", sensorId);
            CloseHandle(pipeHandle); pipeHandle = INVALID_HANDLE_VALUE;
            if (!conectarAlBroker()) break;
            continue;
        }
        sleepTime = 100 + rand() % 400;
        if (!controlarTecla(sleepTime)) break;
    }

    limpiarRecursos();
    printf("[Sensor %lu] Sensor finalizado.\n", sensorId);
    return 0;
}