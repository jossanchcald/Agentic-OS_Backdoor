/* ialearner.c
 Proceso receptor/clasificador: recibe teclas de las ventanas graficas,
 aplica Bag of Words para clasificar documentos e inferir tipo de usuario.
 Autor: Josue Sanchez C.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "protocoloComms.h"
#include "config.h"

pthread_t *arrHilos = NULL; // Array para almacenar los identificadores de los hilos
size_t totalHilos = 0; // Tamaño real del arreglo de ids de hilos
size_t capacidadArrHilos = 16; // El espacio reservado para el arreglo de ids de hilos

pthread_mutex_t mutex_resultados = PTHREAD_MUTEX_INITIALIZER;

/* Una sesion representa un launcher conectado, con su propio conteo de tipos de ventana.
 asi se pueden conectar muchos launches a ialearner */
typedef struct {
    int id_launcher;
    int socket_control;
    int *contadores_tipos; // contadores_tipos[i] = num. ventanas del tipo i
    pthread_mutex_t mutex; // mutex para contadores_tipos que es global por launcher
    int activa; // 0 si el launcher termina o desconecta
} SesionLauncher;

SesionLauncher **sesiones = NULL;

size_t num_sesiones = 0;
size_t cap_sesiones = 8;
pthread_mutex_t mutex_sesiones = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int socket_fd;
    ConfigIALearner *config; // puntero, config es de solo lectura
} ArgsConexion;

typedef struct {
    char palabra_actual[64];
    size_t longitud_palabra;

    // Frencuencias BoW de la oracion actual
    int *frecuencias_oracion; // array dinamico para los tipos de documento
    
    // Frecuencias BoW de toda la ventana
    int *vec_total; // array dinamico acumulado de toda la ventana
    
    int num_tipos; // config->num_tipos
    int total_oraciones;

    char *historial; // para guardar que es todo lo que ha escrito en la ventana
    size_t longitud_historial;
    size_t capacidad_historial;
} EstadoClasificacionVentana; // El estado actual de clasificacion de la ventana, todo lo que se ha preisonado, y la palabra y oracion actual q se tiene

/* Suma los elementos de un vector */
static int sumaVector(int *vector, int tam) {
    int suma = 0;
    for (int i = 0; i < tam; i++) suma += vector[i];
    return suma;
}

/* Duplica la capacidad del arreglo de hilos*/
static void expandirArregloHilos(){
    capacidadArrHilos *= 2;
    pthread_t *tmp = realloc(arrHilos, capacidadArrHilos * sizeof(pthread_t));
    if (!tmp) {
        fprintf(stderr, "[ialearner] Error realloc arrHilos\n");
        exit(EXIT_FAILURE);
    }
    arrHilos = tmp;
}

/* Verifica a que diccionario pertenece una palabra, y segun el tipo
 aumenta la frecuencia de ese tipo en el vector global */
static void agregarPalabraAlVector(EstadoClasificacionVentana *estado, const char *palabra, ConfigIALearner *config) {
    int idx_tipo = indiceTipoPalabraEnHash(palabra, config);

    if (idx_tipo >= 0 && idx_tipo < estado->num_tipos) {
        estado->frecuencias_oracion[idx_tipo]++;
    }
    // si retorna -1, la palabra no está en ningún diccionario, se ignora
}

/* Agrega un caracter al historial completo, aumentando el buffer si hace falta
 Se agrega lo que se presionó y el \0 de fin de palabra */
static void agregarLetraAHistorial(EstadoClasificacionVentana *estado, char c) {

    if (estado->longitud_historial + 2 > estado->capacidad_historial) {
        estado->capacidad_historial *= 2;
        char *tmp = realloc(estado->historial, estado->capacidad_historial);
        if (tmp == NULL) {
            fprintf(stderr, "Error realloc en historial\n");
            return;
        }
        estado->historial = tmp;
    }
    estado->historial[estado->longitud_historial] = c;
    estado->longitud_historial++;
    estado->historial[estado->longitud_historial] = '\0';

}

/* Procesa una palabra, la agrega al vector y reinicia para construir otra */
static void procesarPalabra(EstadoClasificacionVentana *estado, ConfigIALearner *config) {
    if (estado->longitud_palabra == 0) {
        return;
    }

    estado->palabra_actual[estado->longitud_palabra] = '\0'; // Le damos un final por si acaso
    agregarPalabraAlVector(estado, estado->palabra_actual, config);
    estado->longitud_palabra = 0; // Reiniciamos tam
}

/* Clasifica la oracion actual. Acumula frecuencias_oracion en vec_total y luego resetea frecuencias_oracion.
 Retorna el indice del tipo dominante, o -1 si la oracion estaba vacia. */
static int clasificarOracion(EstadoClasificacionVentana *estado) {
    int total_oracion = sumaVector(estado->frecuencias_oracion, estado->num_tipos);
    if (total_oracion == 0) return -1; // oracion sin palabras de ningun dicc

    // Determinar el tipo dominante de esta oracion
    int tipo_dominante = -1;
    int mayor_frec = -1;
    for (int i = 0; i < estado->num_tipos; i++) {
        if (estado->frecuencias_oracion[i] > mayor_frec) {
            mayor_frec = estado->frecuencias_oracion[i];
            tipo_dominante = i;
        }
    }

    // Acumulamos en el total de la ventana
    for (int i = 0; i < estado->num_tipos; i++) {
        estado->vec_total[i] += estado->frecuencias_oracion[i];
    }

    // Reseteamos el vector de la oracion
    memset(estado->frecuencias_oracion, 0, estado->num_tipos * sizeof(int));

    estado->total_oraciones++;
    return tipo_dominante;
}

/* Clasifica la ventana completa usando vec_total
 Aplica la regla: minimo umbral_ocurrencias para ser candidato,
 si mas de uno cumple gana el de mayor suma
 Retorna indice del tipo ganador, o -1 si ninguno alcanza el umbral */
static int clasificarVentana(EstadoClasificacionVentana *estado, ConfigIALearner *config) {
    int mejor_tipo = -1;
    int mejor_suma = -1;

    for (int i = 0; i < estado->num_tipos; i++) {
        if (estado->vec_total[i] >= config->umbral_ocurrencias) {
            if (estado->vec_total[i] > mejor_suma) {
                mejor_suma = estado->vec_total[i];
                mejor_tipo = i;
            }
        }
    }
    return mejor_tipo; // -1 si es desconocido 
}

/* Determina el usuario del sistema segun las proporciones de las ventanas
 Tomando en cuenta todas las reglas de usuario que se definieron
 Llena usuarios_aplicables[] con los indices de TiposUsuario que aplican
 Puede retornar 0 (indeterminado) o mas de 1 (sistema indeciso) */
void inferirTipoUsuario(int *contadores, int num_tipos, int total_ventanas, ConfigIALearner *config, int *usuarios_aplicables, int *num_aplicables) {
    *num_aplicables = 0;
    if (total_ventanas == 0 || config->num_reglas <= 0) return;

    for (int i = 0; i < config->num_reglas; i++) {
        ReglaUsuario *regla = &config->reglas[i];
        int aplica = 1;

        for (int j = 0; j < regla->num_condiciones; j++) {
            int idx = regla->condiciones[j].indice_tipo;
            double proporcion = (double)contadores[idx] / total_ventanas;
            if (proporcion < regla->condiciones[j].proporcion_min) {
                aplica = 0;
                break;
            }
        }

        if (aplica) {
            usuarios_aplicables[(*num_aplicables)++] = i;
        }
    }
}

static void expandirArregloSesiones() {
    cap_sesiones *= 2;
    SesionLauncher **tmp = realloc(sesiones, cap_sesiones * sizeof(SesionLauncher *));
    if (!tmp) {
        fprintf(stderr, "[ialearner] Error realloc arreglo sesiones\n");
        exit(EXIT_FAILURE);
    }
    sesiones = tmp;
}

/* Crea una sesion nueva para un launcher (se llama al recibir su TMSG_HELLO_CONTROL) */
static SesionLauncher *crearSesion(int id_launcher, int socket_control, int num_tipos) {
    pthread_mutex_lock(&mutex_sesiones);

    SesionLauncher *nueva = malloc(sizeof(SesionLauncher));
    if (!nueva) {
        fprintf(stderr, "[ialearner] Error malloc SesionLauncher\n");
        pthread_mutex_unlock(&mutex_sesiones);
        return NULL;
    }
    nueva->contadores_tipos = calloc(num_tipos, sizeof(int));
    if (!nueva->contadores_tipos) {
        fprintf(stderr, "[ialearner] Error calloc contadores de sesion\n");
        free(nueva);
        pthread_mutex_unlock(&mutex_sesiones);
        return NULL;
    }
    nueva->id_launcher = id_launcher;
    nueva->socket_control = socket_control;
    nueva->activa = 1;
    pthread_mutex_init(&nueva->mutex, NULL);

    if (num_sesiones >= cap_sesiones) expandirArregloSesiones();
    sesiones[num_sesiones++] = nueva;

    pthread_mutex_unlock(&mutex_sesiones);
    return nueva;
}

/* Busca una sesion activa por id_launcher (se llama al recibir un TMSG_HELLO_VENTANA) */
static SesionLauncher *buscarSesion(int id_launcher) {
    pthread_mutex_lock(&mutex_sesiones);
    SesionLauncher *encontrada = NULL;
    for (size_t i = 0; i < num_sesiones; i++) {
        if (sesiones[i]->id_launcher == id_launcher && sesiones[i]->activa) {
            encontrada = sesiones[i];
            break;
        }
    }
    pthread_mutex_unlock(&mutex_sesiones);
    return encontrada;
}

/* Imprime el resultado de inferencia de usuario (compartido por hilo ventana e hilo control) */
static void imprimirInferenciaUsuario(SesionLauncher *sesion, ConfigIALearner *config, const char *prefijo) {
    pthread_mutex_lock(&sesion->mutex);

    int total = 0;
    for (int i = 0; i < config->num_tipos; i++) total += sesion->contadores_tipos[i];

    printf("%s[launcher PID %d] === Estado actual de clasificacion ===\n", prefijo, sesion->id_launcher);
    for (int i = 0; i < config->num_tipos; i++) {
        printf("%s  %s: %d ventanas\n", prefijo, config->tipos[i].nombre, sesion->contadores_tipos[i]);
    }

    int usuarios[32];
    int num_aplicables = 0;
    inferirTipoUsuario(sesion->contadores_tipos, config->num_tipos, total, config, usuarios, &num_aplicables);

    if (num_aplicables == 0) {
        printf("%s  Tipo de usuario: Indeterminado (datos insuficientes)\n", prefijo);
    } else if (num_aplicables == 1) {
        printf("%s  Tipo de usuario: %s\n", prefijo, config->reglas[usuarios[0]].nombre);
    } else {
        printf("%s  Sistema indeciso. Tipos posibles: ", prefijo);
        for (int i = 0; i < num_aplicables; i++) {
            printf("%s%s", config->reglas[usuarios[i]].nombre, i < num_aplicables - 1 ? " / " : "\n");
        }
    }
    printf("%s=====================================\n", prefijo);

    pthread_mutex_unlock(&sesion->mutex);
}

/* Funcionamiento del hilo asignado a cada ventana */
static void trabajoVentana(int socket_fd, Mensaje *saludo, ConfigIALearner *config) {
    int id_ventana = saludo->id_ventana;
    pid_t pid_ventana = saludo->pid_ventana;

    SesionLauncher *sesion = buscarSesion(saludo->id_launcher);
    if (!sesion) {
        fprintf(stderr, "[Ventana #%d PID %d] Launcher %d desconocido (sin sesion de control activa), se rechaza\n",
                id_ventana, (int)pid_ventana, saludo->id_launcher);
        close(socket_fd);
        return;
    }

    EstadoClasificacionVentana estado;
    memset(&estado, 0, sizeof(estado));
    estado.capacidad_historial = 256;
    estado.historial = malloc(estado.capacidad_historial);
    estado.num_tipos = config->num_tipos;
    estado.frecuencias_oracion = calloc(config->num_tipos, sizeof(int));
    estado.vec_total = calloc(config->num_tipos, sizeof(int));

    if (!estado.frecuencias_oracion || !estado.vec_total || !estado.historial) {
        fprintf(stderr, "[trabajoVentana] Error de memoria al inicializar estado\n");
        free(estado.frecuencias_oracion);
        free(estado.vec_total);
        free(estado.historial);
        close(socket_fd);
        return;
    }
    estado.historial[0] = '\0';

    while (1) {
        Mensaje mensaje_recibido;
        int bytes_leidos = recv(socket_fd, &mensaje_recibido, sizeof(Mensaje), 0);

        if (bytes_leidos <= 0) {
            if (bytes_leidos < 0) perror("[trabajoVentana] recv");
            printf("[Ventana #%d PID %d] Conexion cerrada abruptamente — obteniendo clasificacion final...\n", id_ventana, (int)pid_ventana);
            procesarPalabra(&estado, config);
            clasificarOracion(&estado);
            break;
        }

        if (bytes_leidos != sizeof(Mensaje)) {
            fprintf(stderr, "[Ventana #%d PID %d] Mensaje incompleto, se cierra la conexion\n", id_ventana, (int)pid_ventana);
            break;
        }

        if (mensaje_recibido.tipo_mensaje == TMSG_CIERRE) {
            printf("[Ventana #%d PID %d] Mensaje de cierre recibido.\n", id_ventana, (int)pid_ventana);
            procesarPalabra(&estado, config);
            if (sumaVector(estado.frecuencias_oracion, estado.num_tipos) > 0) {
                clasificarOracion(&estado);
            }
            break;
        }

        if (mensaje_recibido.tipo_mensaje == TMSG_BACKSPACE) {
            if (estado.longitud_palabra > 0) {
                estado.longitud_palabra--;
                estado.palabra_actual[estado.longitud_palabra] = '\0';
                if (estado.longitud_historial > 0) {
                    estado.longitud_historial--;
                    estado.historial[estado.longitud_historial] = '\0';
                }
            }
            continue;
        }

        if (mensaje_recibido.tipo_mensaje == TMSG_FIN_ORACION) {
            procesarPalabra(&estado, config);
            clasificarOracion(&estado);

            int tipo_ventana = clasificarVentana(&estado, config);
            if (tipo_ventana >= 0) {
                printf("[Ventana #%d] Actualmente clasificada como: %s\n", id_ventana, config->tipos[tipo_ventana].nombre);
            } else {
                printf("[Ventana #%d] Actualmente tipo Desconocido.\n", id_ventana);
            }

            imprimirInferenciaUsuario(sesion, config, "  [progreso] ");
            agregarLetraAHistorial(&estado, '\n');
            continue;
        }

        if (mensaje_recibido.tipo_mensaje == TMSG_TECLA) {
            char c = mensaje_recibido.tecla;
            agregarLetraAHistorial(&estado, c);

            if (strchr(config->delimitadores, c) != NULL) {
                procesarPalabra(&estado, config);
            } else if (estado.longitud_palabra + 1 < sizeof(estado.palabra_actual)) {
                estado.palabra_actual[estado.longitud_palabra++] = c;
                estado.palabra_actual[estado.longitud_palabra] = '\0';
            }
            printf("[Ventana #%d PID %d] Caracter recibido: '%c'\n", id_ventana, (int)pid_ventana, c);
            continue;
        }

        fprintf(stderr, "[Ventana #%d PID %d] Tipo de mensaje inesperado (%d), se ignora\n", id_ventana, (int)pid_ventana, mensaje_recibido.tipo_mensaje);
    }

    int tipoVentanaFinal = clasificarVentana(&estado, config);

    printf("[Ventana #%d PID %d] Historial: \"%s\"\n", id_ventana, (int)pid_ventana, estado.historial);
    printf("[Ventana #%d PID %d] Vectores totales: ", id_ventana, (int)pid_ventana);
    for (int i = 0; i < estado.num_tipos; i++) {
        printf("%s=%d ", config->tipos[i].nombre, estado.vec_total[i]);
    }
    printf("\n");

    if (tipoVentanaFinal >= 0) {
        printf("[Ventana #%d PID %d] Clasificacion final: %s\n", id_ventana, (int)pid_ventana, config->tipos[tipoVentanaFinal].nombre);
        pthread_mutex_lock(&sesion->mutex);
        sesion->contadores_tipos[tipoVentanaFinal]++;
        pthread_mutex_unlock(&sesion->mutex);
    } else {
        printf("[Ventana #%d PID %d] Clasificacion final: Desconocida (menos de %d ocurrencias en todos los tipos)\n", id_ventana, (int)pid_ventana, config->umbral_ocurrencias);
    }

    Mensaje respuesta;
    memset(&respuesta, 0, sizeof(respuesta));
    respuesta.tipo_mensaje = TMSG_RESULTADO_VENTANA;
    respuesta.id_ventana  = id_ventana;
    respuesta.pid_ventana = pid_ventana;
    strncpy(respuesta.nombre_tipo, tipoVentanaFinal >= 0 ? config->tipos[tipoVentanaFinal].nombre : "Desconocido", sizeof(respuesta.nombre_tipo) - 1);

    if (send(sesion->socket_control, &respuesta, sizeof(Mensaje), 0) < 0) {
        perror("[trabajoVentana] Error enviando resultado al launcher (puede que ya se haya desconectado)");
    }

    free(estado.frecuencias_oracion);
    free(estado.vec_total);
    free(estado.historial);
    close(socket_fd);
}

static void trabajoControl(int socket_fd, int id_launcher, ConfigIALearner *config) {
    SesionLauncher *sesion = crearSesion(id_launcher, socket_fd, config->num_tipos);
    if (!sesion) {
        fprintf(stderr, "[ialearner] No se pudo crear sesion para launcher PID %d\n", id_launcher);
        close(socket_fd);
        return;
    }
    printf("[ialearner] Nueva sesion de control: launcher PID %d\n", id_launcher);

    Mensaje msg;
    int bytes;
    while ((bytes = recv(socket_fd, &msg, sizeof(Mensaje), 0)) > 0) {
        if (bytes != sizeof(Mensaje)) {
            fprintf(stderr, "[trabajoControl] Mensaje incompleto de launcher PID %d, se cierra su sesion\n", id_launcher);
            break;
        }
        if (msg.tipo_mensaje == TMSG_CALC_USER) {
            printf("\n[trabajoControl] Launcher PID %d solicita resultado final del lote.\n", id_launcher);
            imprimirInferenciaUsuario(sesion, config, "[RESULTADO] ");

            pthread_mutex_lock(&sesion->mutex);
            memset(sesion->contadores_tipos, 0, config->num_tipos * sizeof(int));
            pthread_mutex_unlock(&sesion->mutex);
        }
    }

    printf("[trabajoControl] Launcher PID %d desconectado.\n", id_launcher);
    sesion->activa = 0; // ventanas tardias de este launcher seran rechazadas por buscarSesion
    close(socket_fd);
}

/* Se usa para recibir cualquier conexion entrante. Lee el primer mensaje
 para decidir si es un launcher (control) o una ventana, y llama a las funciones. */
void *hiloConexion(void *param) {
    ArgsConexion *args = (ArgsConexion *)param;
    int socket_fd = args->socket_fd;
    ConfigIALearner *config = args->config;
    free(args);

    Mensaje saludo;
    int bytes = recv(socket_fd, &saludo, sizeof(Mensaje), 0);
    if (bytes != sizeof(Mensaje)) {
        fprintf(stderr, "[ialearner] Conexion entrante invalida (saludo incompleto), se descarta\n");
        close(socket_fd);
        return NULL;
    }

    if (saludo.tipo_mensaje == TMSG_HELLO_CONTROL) {
        trabajoControl(socket_fd, saludo.id_launcher, config);
    } else if (saludo.tipo_mensaje == TMSG_HELLO_VENTANA) {
        trabajoVentana(socket_fd, &saludo, config);
    } else {
        fprintf(stderr, "[ialearner] Se esperaba un saludo (HELLO_*), se recibio tipo %d\n", saludo.tipo_mensaje);
        close(socket_fd);
    }
    return NULL;
}

int main(int argc, char *argv[]) {

    if (argc != 4) {
        fprintf(stderr, "Uso: %s <puerto> <diccionarios.conf> <reglas.conf>\n", argv[0]);
        return 1;
    }

    int puerto = atoi(argv[1]);
    if (puerto <= 0 || puerto > 65535) {
        fprintf(stderr, "Puerto invalido: %s\n", argv[1]);
        return 1;
    }

    ConfigIALearner config;
    memset(&config, 0, sizeof(config));

    // Primero parseamos los dicc luego las reglas de usuarios
    if (parsearDiccionarios(argv[2], &config) != 0) return 1;
    if (parsearReglas(argv[3], &config) != 0) {
        liberarConfig(&config);
        return 1;
    }
    imprimirConfig(&config); // muestra la config al arrancar, para veruficar

    
    arrHilos = malloc(capacidadArrHilos * sizeof(pthread_t)); // Tam inicial
    if (!arrHilos) {
        fprintf(stderr, "[ialearner] Error malloc arrHilos\n");
        liberarConfig(&config);
        return 1;
    }
    
    
    int server_sockfd = -1;
    struct sockaddr_in socket_address;
    struct sockaddr_in client_address;
    
    int opt = 1;
    int addrlen = sizeof(socket_address);

    socket_address.sin_family = AF_INET;
    socket_address.sin_addr.s_addr = INADDR_ANY; // Escucha en todas las interfaces de red
    socket_address.sin_port = htons(puerto);

    // Remove any old socket and create an unnamed socket for the server.
    if ((server_sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("[ialearner] Socket falló");
        goto limpiezaFinalError;
    }

    // Configurar la reutilización del puerto, para evitar el error "Address already in use"
    // Cuando el servidor se cierra, el puerto no se libera de inmediato, si se vuelve a abrir muy rapido
    // Saldra el error y no podra hacer bind
    if (setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("[ialearner] setsockopt falló");
        goto limpiezaFinalError;
    }

    // Ligar socket al puerto
    if (bind(server_sockfd, (struct sockaddr *)&socket_address, sizeof(socket_address)) < 0) {
        perror("[ialearner] Socket bind fallo");
        goto limpiezaFinalError;
    }

    // Escuchar conexiones entrantes
    if (listen(server_sockfd, 10) < 0) { // Cola de espera del kernel de hasta 10 conexiones
        perror("[ialearner] Socket listen fallo");
        goto limpiezaFinalError;
    }

    printf("[ialearner] Receptor listo, esperando conexiones en el puerto %d...\n", puerto);

    /* Cualquier conexion entra por este mismo loop, sea de control o de ventana,
    de cualquier launcher. hiloConexion decide que es leyendo su saludo inicial. */
    while (1) {
        int socket_entrante = accept(server_sockfd, (struct sockaddr *)&client_address, (socklen_t *)&addrlen);

        if (socket_entrante < 0) {
            perror("[ialearner] accept fallo");
            continue;
        }

        if (totalHilos >= capacidadArrHilos) {
            expandirArregloHilos();
        }

        ArgsConexion *args = malloc(sizeof(ArgsConexion));
        if (!args) {
            fprintf(stderr, "[ialearner] Error malloc ArgsConexion\n");
            close(socket_entrante);
            continue;
        }
        args->socket_fd = socket_entrante;
        args->config = &config;

        if (pthread_create(&arrHilos[totalHilos], NULL, hiloConexion, args) != 0) {
            perror("[ialearner] pthread_create (conexion)");
            free(args);
            close(socket_entrante);
            continue;
        }

        pthread_detach(arrHilos[totalHilos]);
        totalHilos++;
    }

    if(server_sockfd >= 0) close(server_sockfd);
    free(arrHilos);
    liberarConfig(&config);
    return 0;


    limpiezaFinalError:
    if(server_sockfd >= 0) close(server_sockfd);
    free(arrHilos);
    liberarConfig(&config);
    return -1;

}

