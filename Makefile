# Compilador y banderas
CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -g
LDFLAGS =
LIBS_X11 = -lX11
LIBS_PTHREAD = -lpthread

# Ejecutables
TARGETS = launcher ialearner

# Archivos fuente
LAUNCHER_SRC = launcher.c
IALEARNER_SRC = ialearner.c config.c

# Objetos
LAUNCHER_OBJ = $(LAUNCHER_SRC:.c=.o)
IALEARNER_OBJ = $(IALEARNER_SRC:.c=.o)

# Regla por defecto
all: $(TARGETS)

# Launcher
launcher: $(LAUNCHER_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS_X11)

# IALearner
ialearner: $(IALEARNER_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS_PTHREAD)

# Compilación de objetos
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Dependencias
launcher.o: protocoloComms.h

ialearner.o: protocoloComms.h config.h

config.o: config.h

# Limpieza
clean:
	rm -f *.o $(TARGETS)

.PHONY: all clean