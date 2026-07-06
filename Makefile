CC       = gcc
CFLAGS   = -Wall -Wextra -g
LDFLAGS_IALEARNER = -pthread
LDFLAGS_LAUNCHER  = -pthread -lX11

.PHONY: all clean

all: ialearner launcher

# --- ialearner ---
ialearner: ialearner.o config.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS_IALEARNER)

ialearner.o: ialearner.c protocoloComms.h config.h
	$(CC) $(CFLAGS) -c ialearner.c -o $@

config.o: config.c config.h
	$(CC) $(CFLAGS) -c config.c -o $@

# --- launcher ---
launcher: launcher.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS_LAUNCHER)

launcher.o: launcher.c protocoloComms.h
	$(CC) $(CFLAGS) -c launcher.c -o $@

clean:
	rm -f *.o ialearner launcher