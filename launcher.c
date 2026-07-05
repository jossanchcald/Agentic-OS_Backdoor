#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <ctype.h>
#include "protocoloComms.h"

// Librerias para manejar ventanas y eventos de teclado en X11 que uso el profe
#include <X11/Xutil.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

typedef struct {
    char host[256];
    int puerto;
} ConfigLauncher;

int parsearLauncherConf(const char *ruta, ConfigLauncher *cfg) {
    FILE *f = fopen(ruta, "r");
    if (!f) {
        fprintf(stderr, "No se pudo abrir '%s'. Verificar integridad de archivo de configuración\n", ruta);
        return -1;
    }

    cfg->host[0] = '\0';
    cfg->puerto = 0;

    char linea[256];
    int num_linea = 0;
    while (fgets(linea, sizeof(linea), f)) {
        num_linea++;

        // Hacemos trim a la linea leida
        char *lineaT = linea;
        while (*lineaT && isspace((unsigned char)*lineaT)) {
            lineaT++;
        }

        char *fin = lineaT + strlen(lineaT) - 1;
        while (fin > lineaT && isspace((unsigned char)*fin)) {
            *fin-- = '\0';
        }
        if (*lineaT == '\0' || *lineaT == '#') continue;

        if (strncmp(lineaT, "IALEARNER_HOST ", 15) == 0) {
            strncpy(cfg->host, lineaT + 15, sizeof(cfg->host) - 1);

        } else if (strncmp(lineaT, "IALEARNER_PORT ", 15) == 0) {
            cfg->puerto = atoi(lineaT + 15);

            if (cfg->puerto <= 0 || cfg->puerto > 65535) {
                fprintf(stderr, "[launcher.conf] Linea %d: puerto invalido\n", num_linea);
                fclose(f);
                return -1;
            }
        }
    }
    fclose(f);

    if (cfg->host[0] == '\0') {
        fprintf(stderr, "[launcher.conf] Falta IALEARNER_HOST\n");
        return -1;
    }
    if (cfg->puerto == 0) {
        fprintf(stderr, "[launcher.conf] Falta IALEARNER_PORT\n");
        return -1;
    }
    return 0;
}

int createWindow(int socket_fd) {
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Cannot open display\n");
        return 1;
    }

    int screen = DefaultScreen(display);

    Window window = XCreateSimpleWindow(
        display,
        RootWindow(display, screen),
        10, 10, 400, 200,
        1,
        BlackPixel(display, screen),
        WhitePixel(display, screen)
    );

    XSelectInput(display, window, ExposureMask | KeyPressMask);
    XMapWindow(display, window);

    XEvent event;

    while (1) {
        XNextEvent(display, &event);

        // Preparar y enviar el mensaje estructurado
        Mensaje mensaje;

        mensaje.tipo_mensaje = TMSG_TECLA; 
        mensaje.pid_ventana = getpid();

        if (event.type == KeyPress) {
            KeySym keysym;
            char buffer_tecla[32];

            // La funcion XLookupString traduce un evento de tecla al texto real que debería producir, presionas ',', da ',' en el buffer
            int n = XLookupString(&event.xkey, buffer_tecla, sizeof(buffer_tecla) - 1, &keysym, NULL);
            buffer_tecla[n] = '\0'; // aseguramos el string, n es el numero de bytes guardado en el buffer

            if (keysym == XK_Escape) {
                mensaje.tipo_mensaje = TMSG_CIERRE;
                if (send(socket_fd, &mensaje, sizeof(Mensaje), 0) < 0) {
                    perror("Error al enviar el mensaje de cierre");
                } else {
                    printf("Ventana finalizada correctamente\n");
                }
                break;
            } else if (keysym == XK_Return) {
                mensaje.tipo_mensaje = TMSG_FIN_ORACION;
            } else if (keysym == XK_BackSpace) {
                mensaje.tipo_mensaje = TMSG_BACKSPACE;
            } else if (n > 0) {
                // Agregamos la tecla que se presiona, solo nos interesan letras normales asi que solo 1 char
                mensaje.tecla = buffer_tecla[0];
            } else if (n == 0) { // Se presiona una tecla que no genera texto, como shift, mayus, f1, etc
                continue;
            }

            if (send(socket_fd, &mensaje, sizeof(Mensaje), 0) < 0) {
                perror("Error al enviar el mensaje");
            } else {
                printf("Mensaje enviado a la red.\n");
            }
        }        
    }

    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 0;
}

int main(int argc, char *argv[]) {

    // Determinar dinámicamente la ruta del archivo .conf
    const char *ruta_conf = "/config/launcher.conf"; // Ruta por defecto
    if (argc > 1) {
        ruta_conf = argv[1]; // Si se pasó un argumento, usamos ese archivo
    }

    // Parsear el archivo de configuración
    ConfigLauncher config;
    if (parsearLauncherConf(ruta_conf, &config) != 0) {
        fprintf(stderr, "[launcher] Error: No se pudo cargar la configuración desde '%s'\n", ruta_conf);
        return 1;
    }

    // Solicitar número de ventanas
    int n;
    printf("Ingrese el numero de ventanas a crear: ");
    if (scanf("%d", &n) != 1 || n <= 0) {
        fprintf(stderr, "Número de ventanas inválido.\n");
        return 1;
    }

    // Configurar la dirección del servidor usando los datos leídos del archivo
    struct sockaddr_in server_address; 
    server_address.sin_family = AF_INET; // IPv4
    server_address.sin_port = htons(config.puerto); // Usamos el puerto del .conf

    // Configurado a localhost (127.0.0.1). Si lo aplicamos a la realidad cambiará a la IP remota.
    // La ocnvertimos a binario y ponemos en server_address.sin_addr
    // inet_pton = Pointer tp network, convierte una dirección IP de texto a formato binario numérico.
    // Soporta IPv4 e IPv6
    if (inet_pton(AF_INET, config.host, &server_address.sin_addr) <= 0) {
        fprintf(stderr, "Error: La IP '%s' especificada en %s no es válida.\n", config.host, ruta_conf);
        return 1;
    }


    for (int i = 0; i < n; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("Error con el fork");
            exit(1);
        }
        else if (pid == 0) {
            // Creamos un socket por ventana, asi cada conexion con ialearner representa un proceso y ventana unicos
            int socket_fd; // Descriptor de archivos del socket que vamos a crear

            // Creamos el socket TCP (para la comunicacion en la red, ahora es el mismo equipo, pero como el proyecto indica
            // el proceso ialearner de IBM es remoto, asi que es lo que se usaria en realidad)
            if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                perror("Error al crear socket");
                return 1;
            }

            // Nos conectamos al programa receptor (ialearner de IBM)
            if (connect(socket_fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
                perror("Conexión fallida. Verificar estado del receptor");
                return 1;
            }

            createWindow(socket_fd);
            close(socket_fd);
            exit(0);
        }
    }

    // Clean up
    for (int i = 0; i < n; i++) {
        wait(NULL);
    }

    return 0;
}
