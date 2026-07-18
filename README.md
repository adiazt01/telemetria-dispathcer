# Sistema de Telemetría y Control Concurrente - Grupo 8

## Integrantes

| Módulo | Integrante |
|--------|------------|
| Sensor.exe | Carlos Brito |
| Broker.exe | Jesus Guzman |
| Dispatcher.exe | Armando Diaz |
| Monitor.exe | Jose Marquez |

## Arquitectura

```
┌─────────────┐     Named Pipe     ┌─────────────┐
│  sensor.exe │ ──────────────────►│             │
│  (N veces)  │                    │   broker    │
└─────────────┘                    │    .exe     │
                                   │             │
┌─────────────┐     Named Pipe     │  ┌───────┐  │
│  sensor.exe │ ──────────────────►│  │ Hilos │  │
└─────────────┘                    │  │Receptor  │
                                   │  └───────┘  │
                                   └──────┬──────┘
                                          │
                                   Memory-Mapped File
                                   (Buffer Circular)
                                          │
                    ┌─────────────────────┼─────────────────────┐
                    │                     │                     │
                    ▼                     ▼                     ▼
            ┌─────────────┐       ┌─────────────┐       ┌─────────────┐
            │ dispatcher  │       │  monitor    │       │   (libre)   │
            │   .exe     │       │   .exe      │       │             │
            │             │       │             │       │             │
            │ ┌─────────┐ │       │ Dashboard   │       │             │
            │ │ Workers │ │       │ en tiempo   │       │             │
            │ │ (Pool)  │ │       │   real      │       │             │
            │ └────┬────┘ │       └─────────────┘       │             │
            └───────┼──────┘                             │
                    │                                     │
                    ▼                                     │
            ┌─────────────┐                               │
            │  log.txt    │                               │
            └─────────────┘                               │
```

## Compilación

### En Linux (MinGW-w64)

```bash
# Instalar compilador cruzado
sudo apt install mingw-w64

# Compilar todo
make all

# Ver información
make info

# Limpiar
make clean
```

### En Windows (Visual Studio)

```batch
# Abrir "Developer Command Prompt for VS"
build.bat
```

## Ejecución

** IMPORTANTE: El orden de ejecución es obligatorio **

1. **Broker.exe** (primero - crea los recursos compartidos)
   ```
   build\broker.exe
   ```

2. **Dispatcher.exe** (segundo - se conecta al buffer)
   ```
   build\dispatcher.exe
   ```

3. **Monitor.exe** (tercero - panel de control)
   ```
   build\monitor.exe
   ```

4. **Sensor.exe** (N veces - últimos)
   ```
   build\sensor.exe <id_sensor> <tipo_sensor>
   ```

### Tipos de Sensor
- `0` = Motor
- `1` = Neumáticos
- `2` = Frenos
- `3` = GPS

### Ejemplo de Ejecución Rápida
```batch
start broker.exe
start dispatcher.exe
start monitor.exe
start sensor.exe 0 0
start sensor.exe 1 1
start sensor.exe 2 2
start sensor.exe 3 3
```

## Controles del Monitor

| Tecla | Acción |
|-------|--------|
| `D` | Alternar modo debug (envía señal al Broker) |
| `S` | Solicitar apagado del sistema |
| `R` | Refrescar estadísticas |
| `ESC` | Salir del monitor |

## Controles del Sensor

| Tecla | Acción |
|-------|--------|
| `ESC` | Detener el sensor |

## Características Técnicas

### Sincronización (SIN busy waiting)
- **Mutex**: Protección del buffer circular
- **Semaforo Slots**: Cuenta espacios disponibles para escribir
- **Semaforo Data**: Cuenta datos disponibles para leer
- **Eventos**: Coordinación entre Broker y Monitor

### Backpressure (Tolerancia al Desbordamiento)
Cuando el buffer está lleno:
1. El semáforo de slots llega a 0
2. El hilo del sensor se bloquea en WaitForSingleObject
3. El Named Pipe retiene el flujo naturalmente
4. No hay pérdida de datos

### Limpieza de Handles
Al presionar Ctrl+C o ESC en cualquier módulo:
1. Se cierra el handle de manera ordenada
2. Se liberan los recursos
3. Los hilos esperan a que terminen sus tareas

## Depuración

### VerificarHandles Abiertos
```powershell
Get-Process | Select-Object Handles | Sort Handles -Descending | Select-Object -First 10
```

### Ver Objetos del Sistema
```powershell
Get-Process | Where-Object {$_.Handles -gt 500} | Format-Table Name, Handles
```

## Problemas Comunes

| Problema | Solución |
|----------|----------|
| "No se pudo conectar al buffer" | Ejecutar broker.exe primero |
| "No se pudieron abrir los eventos" | Ejecutar broker.exe primero |
| Error 2 (ERROR_FILE_NOT_FOUND) | Verificar que el Broker esté corriendo |
| Buffer siempre lleno | Normal - los workers procesan más lento |
