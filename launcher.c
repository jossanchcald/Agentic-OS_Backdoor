/* launcher.c
 Proceso launcher: crea y monitorea ventanas graficas, se comunica con IALearner via Sockets.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>

#include "protocoloComms.h"

// Librerias para manejar ventanas y eventos de teclado en X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>



typedef enum {
    VENTANA_ACTIVA,
    VENTANA_CERRADA
} EstadoVentana;

typedef struct {
    int id_local; // id de cada ventana
    pid_t pid; // PID de cada ventana (procesos hijo) 
    EstadoVentana estado;
    char tipo_documento[64];
} InfoVentana;

typedef struct {
    char host[256];
    int puerto;
} ConfigConexionLauncher;

// Arreglo dinamico para almacenar ref a ventanas
InfoVentana *ventanas = NULL;
size_t num_ventanas = 0;
size_t cap_ventanas = 16;
int siguiente_id = 1; // ID local incremental
static int num_activas = 0;

pthread_mutex_t mutex_ventanas = PTHREAD_MUTEX_INITIALIZER;

int socket_control = -1;
int launcher_corriendo = 1;
static int id_launcher_actual = -1; // PID propio para identificar el launcher unico

static ConfigConexionLauncher cfg_global;



/* Elimina espacios y saltos de linea al inicio y final de un string. */
static void trim(char *s) {
    char *inicio = s;
    while (*inicio && isspace((unsigned char)*inicio)) inicio++;

    char *fin = inicio + strlen(inicio);
    while (fin > inicio && isspace((unsigned char)*(fin - 1))) fin--;
    *fin = '\0';

    /* Si habia espacios a la izquierda mueve el puntero original*/
    if (inicio != s) memmove(s, inicio, fin - inicio + 1);
}

int parsearLauncherConf(const char *ruta, ConfigConexionLauncher *cfg) {
    FILE *f = fopen(ruta, "r");
    if (!f) {
        fprintf(stderr, "[launcher] No se pudo abrir '%s'.\n\tVerificar integridad de archivo de configuración.\n", ruta);
        return -1;
    }

    cfg->host[0] = '\0';
    cfg->puerto  = 0;

    char linea[256];
    int num_linea = 0;

    while (fgets(linea, sizeof(linea), f)) {
        num_linea++;
        trim(linea);

        if (linea[0] == '\0' || linea[0] == '#') continue;

        if (strncmp(linea, "IALEARNER_HOST ", 15) == 0) {
            strncpy(cfg->host, linea + 15, sizeof(cfg->host) - 1);
            cfg->host[sizeof(cfg->host) - 1] = '\0';
            trim(cfg->host);
            continue;
        }

        if (strncmp(linea, "IALEARNER_PORT ", 15) == 0) {
            cfg->puerto = atoi(linea + 15);

            if (cfg->puerto <= 0 || cfg->puerto > 65535) {
                fprintf(stderr, "[launcher.conf] Linea %d: puerto '%s' invalido\n", num_linea, linea + 15);
                fclose(f);
                return -1;
            }
            continue;
        }
        fprintf(stderr, "[launcher.conf] Linea %d: clave desconocida '%s'\n", num_linea, linea);
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

/* Crea un socket TCP IPv4 y se conecta a la dirección indicada. 
 Retorna el descriptor del socket en éxito, -1 en caso de error. */
int crearConexion(const char *host, int puerto) {
    struct sockaddr_in server_address;

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(puerto);

    /* Convertimos la IP en formato texto a formato binario */
    if (inet_pton(AF_INET, host, &server_address.sin_addr) <= 0) {
        fprintf(stderr, "[launcher] La IP '%s' no es válida.\n", host);
        return -1;
    }

    /* Creamos el socket TCP */
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("[launcher] Error al crear socket");
        return -1;
    }

    /* Intentamos establecer la conexión */
    if (connect(socket_fd,
                (struct sockaddr *)&server_address,
                sizeof(server_address)) < 0) {
        perror("[launcher] Error al conectar");
        close(socket_fd);
        return -1;
    }

    return socket_fd;
}

/* Crea una ventana gráfica usando X11 */
int createWindow(int socket_fd, int id_launcher, int id_ventana) {
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "[ventana %d] Cannot open display\n", id_ventana);
        return 1;
    }

    int screen = DefaultScreen(display);

    Window window = XCreateSimpleWindow(
        display,
        RootWindow(display, screen),
        10, 10, 400, 200, 1,
        BlackPixel(display, screen),
        WhitePixel(display, screen)
    );

    int pid_actual = (int)getpid();
    char titulo[64];
    sprintf(titulo, "Ventana #%d (PID %d)", id_ventana, pid_actual);
    XStoreName(display, window, titulo);

    Atom wmDelete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wmDelete, 1);

    XSelectInput(display, window, ExposureMask | KeyPressMask);
    XMapWindow(display, window);

    // Saludo inicial para identifica esta ventana ante ialearner, una sola vez
    Mensaje saludo;
    memset(&saludo, 0, sizeof(saludo));
    saludo.tipo_mensaje = TMSG_HELLO_VENTANA;
    saludo.id_launcher  = id_launcher;
    saludo.id_ventana   = id_ventana;
    saludo.pid_ventana  = pid_actual;
    if (send(socket_fd, &saludo, sizeof(Mensaje), 0) < 0) {
        perror("[ventana] Error enviando saludo a IALearner");
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        return 1;
    }

    int cerrar = 0;
    XEvent event;

    while (!cerrar) {
        XNextEvent(display, &event);

        Mensaje mensaje;
        memset(&mensaje, 0, sizeof(mensaje));

        if (event.type == ClientMessage && (Atom)event.xclient.data.l[0] == wmDelete) {
            mensaje.tipo_mensaje = TMSG_CIERRE;
            if (send(socket_fd, &mensaje, sizeof(Mensaje), 0) < 0)
                perror("[ventana] Error enviando cierre");
            cerrar = 1;
            continue;
        }

        if (event.type != KeyPress) continue;

        KeySym keysym;
        char buffer_tecla[8];
        int n = XLookupString(&event.xkey, buffer_tecla, sizeof(buffer_tecla) - 1, &keysym, NULL);
        buffer_tecla[n] = '\0';

        if (event.xkey.state & ControlMask) continue;

        if (keysym == XK_Escape) {
            mensaje.tipo_mensaje = TMSG_CIERRE;
            if (send(socket_fd, &mensaje, sizeof(Mensaje), 0) < 0) {
                perror("[ventana] Error enviando cierre");
            } else {
                printf("[ventana] Ventana #%d finalizada correctamente\n", id_ventana);
            }
            cerrar = 1;
            continue;
        }

        if (keysym == XK_Return) {
            mensaje.tipo_mensaje = TMSG_FIN_ORACION;
        } else if (keysym == XK_BackSpace) {
            mensaje.tipo_mensaje = TMSG_BACKSPACE;
        } else if (n > 0) {
            mensaje.tipo_mensaje = TMSG_TECLA;
            mensaje.tecla = buffer_tecla[0];
        } else {
            continue;
        }

        if (send(socket_fd, &mensaje, sizeof(Mensaje), 0) < 0) {
            perror("[ventana] Error enviando mensaje");
        }
    }

    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 0;
}

/* Agrega una ventana al array. Retorna su indice o -1 si falla malloc. */
static int agregarVentana(pid_t pid, int id_local) {
    if (num_ventanas >= cap_ventanas) {
        int nueva_cap = cap_ventanas * 2;
        InfoVentana *tmp = realloc(ventanas, nueva_cap * sizeof(InfoVentana));
        if (!tmp) {
            fprintf(stderr, "[launcher] Error realloc array de ventanas\n");
            return -1;
        }
        ventanas = tmp;
        cap_ventanas = nueva_cap;
    }

    ventanas[num_ventanas].id_local = id_local;
    ventanas[num_ventanas].pid = pid;
    ventanas[num_ventanas].estado = VENTANA_ACTIVA;
    strncpy(ventanas[num_ventanas].tipo_documento, "Sin clasificar", sizeof(ventanas[num_ventanas].tipo_documento) - 1);
    ventanas[num_ventanas].tipo_documento[sizeof(ventanas[num_ventanas].tipo_documento) - 1] = '\0';
    num_activas++;
    return num_ventanas++;
}
/* Busca una ventana por ID local. Retorna puntero o NULL. - O(n)*/
static InfoVentana *buscarPorId(int id) {
    int idx = id - 1; // los IDs empiezan en 1, los indices en 0
    if (idx < 0 || (size_t)idx >= num_ventanas) return NULL;
    return &ventanas[idx];
}

/* Busca una ventana por PID. Retorna puntero o NULL. - O(n) */
static InfoVentana *buscarPorPid(pid_t pid) {
    for (size_t i = 0; i < num_ventanas; i++) {
        if (ventanas[i].pid == pid)
            return &ventanas[i];
    }
    return NULL;
}

/* Hilo que verifica SIGCHLD que envia el kernel cuando termina una ventana */
void *hiloMonitor(void *arg) {
    (void)arg;

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);

    int sig_recibida;

    while (launcher_corriendo) {
        int error = sigwait(&set, &sig_recibida);
        if (error != 0) continue;

        if (!launcher_corriendo) break;

        pthread_mutex_lock(&mutex_ventanas);
        int ventanas_activas = num_activas;
        int habia_activas = (ventanas_activas > 0);

        int status;
        pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            InfoVentana *v = buscarPorPid(pid);
            if (v) {
                v->estado = VENTANA_CERRADA;
                printf("\n[hiloMonitor] Ventana #%d (PID %d) terminada.\n", v->id_local, (int)pid);
                num_activas--;
                ventanas_activas--;
                fflush(stdout);
            } else {
                fprintf(stderr, "\n[hiloMonitor] Aviso: PID %d termino pero no esta registrado\n", (int)pid);
            }
        }

        pthread_mutex_unlock(&mutex_ventanas);

        if (habia_activas && ventanas_activas == 0 && socket_control >= 0) {
            Mensaje msg;
            memset(&msg, 0, sizeof(msg));
            msg.tipo_mensaje = TMSG_CALC_USER;
            msg.id_launcher  = id_launcher_actual;
            if (send(socket_control, &msg, sizeof(Mensaje), 0) < 0) {
                perror("[hiloMonitor] Error enviando MSG_CALC_USER");
            } else {
                printf("\n[hiloMonitor] Todas las ventanas cerradas. Resultado enviado a IALearner.\n");
            }
        }

        printf("launcher> ");
    }
    return NULL;
}

/* Hilo que recibe notificaciones de ialearner (tipo de ventana clasificada) */
void *hiloReceptorControl(void *arg) {
    (void)arg;

    Mensaje msg;
    int bytes;
    while ((bytes = recv(socket_control, &msg, sizeof(Mensaje), 0)) > 0) {
        if (bytes != sizeof(Mensaje)) {
            fprintf(stderr, "[launcher] Mensaje incompleto de IALearner, se ignora\n");
            continue;
        }

        if (msg.tipo_mensaje == TMSG_RESULTADO_VENTANA) {
            pthread_mutex_lock(&mutex_ventanas);
            InfoVentana *v = buscarPorId(msg.id_ventana);
            if (v) {
                strncpy(v->tipo_documento, msg.nombre_tipo,
                        sizeof(v->tipo_documento) - 1);
                v->tipo_documento[sizeof(v->tipo_documento) - 1] = '\0';
                printf("\n[Launcher #%d ← IALearner] Ventana #%d clasificada como: %s\n",
                       id_launcher_actual, msg.id_ventana, msg.nombre_tipo);
                printf("launcher> ");
                fflush(stdout);
            }
            pthread_mutex_unlock(&mutex_ventanas);

        } else if (msg.tipo_mensaje == TMSG_CONTEXTO_USUARIO) {
            printf("\n");
            printf("  ╔══════════════════════════════════════════════╗\n");
            printf("  ║  CONTEXTO DE USUARIO — Launcher #%d\n", id_launcher_actual);
            printf("  ║  Tipo de usuario inferido: %s\n", msg.nombre_tipo);
            printf("  ╚══════════════════════════════════════════════╝\n\n");
            printf("launcher> ");
            fflush(stdout);
        }
    }
    return NULL;
}

/* Crea n ventanas nuevas */
static void comandoCrear(int n) {
    if (n <= 0) {
        printf("  Numero de ventanas invalido: debe ser > 0\n");
        return;
    }

    for (int i = 0; i < n; i++) {
        int id_local = siguiente_id++;

        pid_t pid = fork();
        if (pid < 0) {
            perror("  [launcher] Error en fork");
            continue;
        }

        if (pid == 0) {
            int socket_fd = crearConexion(cfg_global.host, cfg_global.puerto);
            if (socket_fd < 0) {
                fprintf(stderr, "[ventana %d] No se pudo conectar a IALearner\n", id_local);
                exit(1);
            }
            int ret = createWindow(socket_fd, id_launcher_actual, id_local);
            close(socket_fd);
            exit(ret);
        }

        pthread_mutex_lock(&mutex_ventanas);
        int idx = agregarVentana(pid, id_local);
        pthread_mutex_unlock(&mutex_ventanas);

        if (idx < 0) {
            fprintf(stderr, "  [launcher] Error registrando ventana\n");
            kill(pid, SIGKILL);
            continue;
        }
        printf("  Ventana #%d creada (PID %d)\n", id_local, (int)pid);
    }
}
/* Muestra el estado de todas las ventanas que hayan sido creadas*/
static void comandoEstado(void) {
    pthread_mutex_lock(&mutex_ventanas);

    if (num_ventanas == 0) {
        printf("  No se han creado ventanas.\n");
        pthread_mutex_unlock(&mutex_ventanas);
        return;
    }
    pthread_mutex_unlock(&mutex_ventanas);

    printf("\n  %-6s %-10s %-12s %s\n", "ID", "PID", "Estado", "Tipo doc.");
    printf("  %-6s %-10s %-12s %s\n", "------", "----------", "------------", "---------");

    pthread_mutex_lock(&mutex_ventanas);
    for (size_t i = 0; i < num_ventanas; i++) {

        const char *estado_str;
        if (ventanas[i].estado == VENTANA_ACTIVA) {
            estado_str = "Activa";
        } else {
            estado_str = "Cerrada";
        }
        
        printf("  %-6d %-10d %-12s %s\n", ventanas[i].id_local, (int)ventanas[i].pid, estado_str, ventanas[i].tipo_documento); // directamente el nombre
    }
    pthread_mutex_unlock(&mutex_ventanas);
}

/* Cierra una ventana por ID: manda SIGTERM al proceso hijo.
 El hilo monitor detectara la terminacion y actualizara el estado. */
static int comandoCerrarId(int id) {
    pthread_mutex_lock(&mutex_ventanas);
    InfoVentana *v = buscarPorId(id);

    if (!v || v->estado == VENTANA_CERRADA) {
        printf("  No existe ventana activa con ID %d\n", id);
        pthread_mutex_unlock(&mutex_ventanas);
        return -1;
    }
    pid_t pidToKill = v->pid;
    pthread_mutex_unlock(&mutex_ventanas);

    kill(pidToKill, SIGTERM);
    
    printf("  Señal de cierre enviada a ventana #%d (PID %d)\n", id, (int)pidToKill);
    return 0;
}

/* Cierra una ventana por PID: manda SIGTERM al proceso hijo.
 El hilo monitor detectara la terminacion y actualizara el estado. */
static int comandoCerrarPid(pid_t pid) {
    pthread_mutex_lock(&mutex_ventanas);
    InfoVentana *v = buscarPorPid(pid);
    if (!v || v->estado == VENTANA_CERRADA) {
        printf("  No existe ventana activa con PID %d\n", (int)pid);
        pthread_mutex_unlock(&mutex_ventanas);
        return -1;
    }
    int id = v->id_local;
    kill(pid, SIGTERM);
    pthread_mutex_unlock(&mutex_ventanas);
    printf("  Señal de cierre enviada a ventana #%d (PID %d)\n", id, (int)pid);
    return 0;
}

/* Cierra todas las ventanas activas */
static void comandoCerrarTodas(void) {

    pthread_mutex_lock(&mutex_ventanas);
    if (num_activas <= 0) {
        printf("  No existen ventanas activas.\n");
        pthread_mutex_unlock(&mutex_ventanas);
        return;
    }
    
    int enviadas = 0;
    for (size_t i = 0; i < num_ventanas; i++) {
        if (ventanas[i].estado == VENTANA_ACTIVA) {
            kill(ventanas[i].pid, SIGTERM);
            enviadas++;
        }
    }
    pthread_mutex_unlock(&mutex_ventanas);
    printf("  Señal de cierre enviada a %d ventana(s) activa(s).\n", enviadas);
}

/* Muestra todos los comandos que se puede usar en la terminal de launcher */
static void comandoAyuda(void) {
    printf("  Comandos disponibles:\n");
    printf("    crear <n>          Crear n ventanas nuevas\n");
    printf("    estado             Listar todas las ventanas con su estado\n");
    printf("    cerrar <id>        Cerrar ventana por ID local\n");
    printf("    cerrar pid <pid>   Cerrar ventana por PID\n");
    printf("    cerrar todas       Cerrar todas las ventanas activas\n");
    printf("    ayuda | help       Mostrar esta ayuda\n");
    printf("    salir              Cerrar el launcher (muestra resumen final)\n");
}

/* Muestra las claseificaciones finales cuando se cierra launcher */
static void mostrarResumenFinal(void) {
    printf("\n Proceso launcher terminado, inferencias finales... \n");
    int total = 0, activas = 0, cerradas = 0;
    for (size_t i = 0; i < num_ventanas; i++) {
        total++;
        if (ventanas[i].estado == VENTANA_ACTIVA) activas++;
        else cerradas++;
    }
    printf("  Ventanas creadas en esta sesion : %d\n", total);
    printf("  Ventanas cerradas               : %d\n", cerradas);
    printf("  Ventanas aun activas al salir   : %d\n", activas);

    if (total > 0) comandoEstado();
    printf("================================\n");
}

/* Bucle del hilo main, para la terminal interactiva */
static void bucleComandos(void) {
    char linea[256];

    printf("\nLauncher listo. Escribe 'ayuda' o help'' para ver los comandos.\n");
    comandoAyuda();

    while (1) {
        printf("launcher> ");
        fflush(stdout);

        if (!fgets(linea, sizeof(linea), stdin)) {
            /* EOF (Ctrl+D): salir limpiamente */
            printf("\n");
            break;
        }
        trim(linea);
        if (linea[0] == '\0') continue;

        if (strncmp(linea, "crear ", 6) == 0) {
            int n = atoi(linea + 6);
            if (n <= 0) {
                printf("  Uso: crear <n>  (n debe ser un entero positivo)\n");
            } else {
                comandoCrear(n);
            }

        } else if (strcmp(linea, "estado") == 0) {
            comandoEstado();

        } else if (strncmp(linea, "cerrar pid ", 11) == 0) {
            pid_t pid = (pid_t)atoi(linea + 11);
            if (pid <= 0) {
                printf("  Uso: cerrar pid <pid>  (pid debe ser un entero positivo)\n");
            } else {
                comandoCerrarPid(pid);
            }

        } else if (strcmp(linea, "cerrar todas") == 0) {
            comandoCerrarTodas();

        } else if (strncmp(linea, "cerrar ", 7) == 0) {
            int id = atoi(linea + 7);
            if (id <= 0) {
                printf("  Uso: cerrar <id>  (id debe ser un entero positivo)\n");
            } else {
                comandoCerrarId(id);
            }

        } else if (strcmp(linea, "ayuda") == 0 || strcmp(linea, "help") == 0) {
            comandoAyuda();

        } else if (strcmp(linea, "salir") == 0) {
            mostrarResumenFinal();
            break;

        } else {
            printf("  Comando desconocido: '%s'. Escribe 'ayuda' o 'help' para ver los comandos.\n", linea);
        }
    }
}

int main(void) {

    if (parsearLauncherConf("./config/launcher.conf", &cfg_global) != 0) {
        return 1;
    }

    ventanas = malloc(cap_ventanas * sizeof(InfoVentana));
    if (!ventanas) {
        fprintf(stderr, "[launcher] Error malloc array de ventanas\n");
        return 1;
    }

    // Intentamos conectar el socket de control a ialearner
    socket_control = crearConexion(cfg_global.host, cfg_global.puerto);
    if (socket_control < 0) {
        fprintf(stderr, "[launcher] No se pudo establecer conexion de control con IALearner.\n\tVerifique que IALearner este en ejecucion.\n");
        free(ventanas);
        return 1;
    }

    id_launcher_actual = (int)getpid();

    Mensaje saludo;
    memset(&saludo, 0, sizeof(saludo));
    saludo.tipo_mensaje = TMSG_HELLO_CONTROL;
    saludo.id_launcher  = id_launcher_actual;
    if (send(socket_control, &saludo, sizeof(Mensaje), 0) < 0) {
        perror("[launcher] Error enviando saludo de control a IALearner");
        close(socket_control);
        free(ventanas);
        return 1;
    }

    printf("[launcher] Conexion de control establecida con IALearner (%s:%d), id_launcher=%d\n", cfg_global.host, cfg_global.puerto, id_launcher_actual);

    // Bloquear SIGCHLD inmediatamente en el main.
    // Garantizamos que el kernel no interrumpirá al main de forma asíncrona.
    sigset_t mascara;
    sigemptyset(&mascara);
    sigaddset(&mascara, SIGCHLD);
    pthread_sigmask(SIG_BLOCK, &mascara, NULL); 

    pthread_t hilo_monitor;

    if (pthread_create(&hilo_monitor, NULL, hiloMonitor, NULL) != 0) {
        perror("[launcher] Error creando hilo monitor");
        close(socket_control);
        free(ventanas);
        return 1;
    }

    pthread_t hilo_receptor;
    if (pthread_create(&hilo_receptor, NULL, hiloReceptorControl, NULL) != 0) {
        perror("[launcher] Error creando hilo receptor de control");
        // no es fatal, el launcher puede funcionar sin esto
    } else {
        pthread_detach(hilo_receptor); // se limpia solo
    }

    bucleComandos();

    launcher_corriendo = 0;
    
    // Para despertar al hiloMonitor si se queda atrapado en sigwait() durante el cierre,
    // le enviamos un SIGCHLD artificial nosotros mismos.
    pthread_kill(hilo_monitor, SIGCHLD);

    comandoCerrarTodas();
    pthread_join(hilo_monitor, NULL);

    close(socket_control);
    free(ventanas);

    return 0;
}
