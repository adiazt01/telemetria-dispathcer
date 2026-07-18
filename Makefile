# ============================================================================
# Makefile para Sistema de Telemetría - Grupo 8
# ============================================================================
#
# Compilacion: make all
# Limpieza:    make clean
#
# Requiere: MinGW-w64 (x86_64-w64-mingw32-gcc)
# Install:    sudo apt install mingw-w64
#
# Ejecucion en Windows:
#   1. broker.exe (primero)
#   2. dispatcher.exe (segundo)
#   3. monitor.exe (tercero)
#   4. sensor.exe N veces (ultimo)
#
# ============================================================================

# Compilador MinGW para Windows
CC = x86_64-w64-mingw32-gcc

# Banderas de compilacion
CFLAGS = -Wall -Wextra -std=c99 -pedantic
CFLAGS += -D_WIN32_WINNT=0x0501
CFLAGS += -O2

# Directorio de salida
BUILD_DIR = build

# Archivos fuente
COMMON_SRC = common.c
SENSOR_SRC = sensor.c
BROKER_SRC = broker.c
DISPATCHER_SRC = dispatcher.c
MONITOR_SRC = monitor.c

# Archivos header
HEADERS = common.h

# Ejecutables
TARGETS = $(BUILD_DIR)/sensor.exe \
          $(BUILD_DIR)/broker.exe \
          $(BUILD_DIR)/dispatcher.exe \
          $(BUILD_DIR)/monitor.exe

# ============================================================================
# Reglas
# ============================================================================

all: $(BUILD_DIR) $(TARGETS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Sensor - Carlos Brito
$(BUILD_DIR)/sensor.exe: $(SENSOR_SRC) $(COMMON_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(SENSOR_SRC) $(COMMON_SRC) -lws2_32

# Broker - Jesus Guzman
$(BUILD_DIR)/broker.exe: $(BROKER_SRC) $(COMMON_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(BROKER_SRC) $(COMMON_SRC) -lws2_32

# Dispatcher - Armando Diaz
$(BUILD_DIR)/dispatcher.exe: $(DISPATCHER_SRC) $(COMMON_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(DISPATCHER_SRC) $(COMMON_SRC) -lws2_32

# Monitor - Jose Marquez
$(BUILD_DIR)/monitor.exe: $(MONITOR_SRC) $(COMMON_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(MONITOR_SRC) $(COMMON_SRC) -lws2_32

clean:
	rm -rf $(BUILD_DIR)

rebuild: clean all

# ============================================================================
# Informacion
# ============================================================================

info:
	@echo "============================================"
	@echo "  Sistema de Telemetria - Grupo 8"
	@echo "============================================"
	@echo ""
	@echo "Modulos:"
	@echo "  1. sensor.exe     -> Carlos Brito"
	@echo "  2. broker.exe    -> Jesus Guzman"
	@echo "  3. dispatcher.exe -> Armando Diaz"
	@echo "  4. monitor.exe    -> Jose Marquez"
	@echo ""
	@echo "Orden de ejecucion:"
	@echo "  1. broker.exe"
	@echo "  2. dispatcher.exe"
	@echo "  3. monitor.exe"
	@echo "  4. sensor.exe (N instancias)"
	@echo ""
	@echo "Ejemplo de ejecucion:"
	@echo "  start broker.exe"
	@echo "  start dispatcher.exe"
	@echo "  start monitor.exe"
	@echo "  start sensor.exe 0 0"
	@echo "  start sensor.exe 1 1"
	@echo "  start sensor.exe 2 2"
	@echo "============================================"

.PHONY: all clean rebuild info
