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

int *contadores_tipos = NULL; // contadores_tipos[i] = num. ventanas del tipo i

typedef struct {
    int socket_fd;
    ConfigIALearner *config; // puntero, config es de solo lectura
} ArgsHilo;

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

/* Clasifica la ventana completa usando vec_total.
 Aplica la regla: minimo umbral_ocurrencias para ser candidato,
 si mas de uno cumple gana el de mayor suma.
 Retorna indice del tipo ganador, o -1 si ninguno alcanza el umbral. */
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
 Tomando en cuenta todas las reglas de usuario que se definieron.
 Llena usuarios_aplicables[] con los indices de TiposUsuario que aplican.
 Puede retornar 0 (indeterminado) o mas de 1 (sistema indeciso). */
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

/* Imprime el resultado de inferencia de usuario (compartido por hilo ventana e hilo control) */
static void imprimirInferenciaUsuario(int *contadores, ConfigIALearner *config, const char *prefijo) {
    pthread_mutex_lock(&mutex_resultados);

    int total = 0;
    for (int i = 0; i < config->num_tipos; i++) total += contadores[i];

    printf("%s=== Estado actual de clasificacion ===\n", prefijo);
    for (int i = 0; i < config->num_tipos; i++) {
        printf("%s  %s: %d ventanas\n",
               prefijo, config->tipos[i].nombre, contadores[i]);
    }

    int usuarios[32];
    int num_aplicables = 0;
    inferirTipoUsuario(contadores, config->num_tipos, total, config, usuarios, &num_aplicables);

    if (num_aplicables == 0) {
        printf("%s  Tipo de usuario: Indeterminado (datos insuficientes)\n", prefijo);
    } else if (num_aplicables == 1) {
        printf("%s  Tipo de usuario: %s\n",
               prefijo, config->reglas[usuarios[0]].nombre);
    } else {
        printf("%s  Sistema indeciso. Tipos posibles: ", prefijo);
        for (int i = 0; i < num_aplicables; i++) {
            printf("%s%s", config->reglas[usuarios[i]].nombre,
                   i < num_aplicables - 1 ? " / " : "\n");
        }
    }
    printf("%s=====================================\n", prefijo);

    pthread_mutex_unlock(&mutex_resultados);
}


/* Funcionamiento del hilo asignado a cada ventana */
void *hiloVentana(void *param) {
    ArgsHilo *args = (ArgsHilo *)param;
    int socket_fd = args->socket_fd;
    ConfigIALearner *config = args->config;
    free(args);

    // Creamos la variable de estado para la ventana
    EstadoClasificacionVentana estado;
    memset(&estado, 0, sizeof(estado)); // inicializa todo a cero de una vez
    
    estado.capacidad_historial = 256;
    estado.longitud_historial = 0;
    estado.historial = malloc(estado.capacidad_historial);
    estado.num_tipos = config->num_tipos;
    estado.frecuencias_oracion = calloc(config->num_tipos, sizeof(int));
    estado.vec_total = calloc(config->num_tipos, sizeof(int));

    if (!estado.frecuencias_oracion || !estado.vec_total || !estado.historial) {
        fprintf(stderr, "[hiloVentana] Error de memoria al inicializar estado\n");
        free(estado.frecuencias_oracion);
        free(estado.vec_total);
        free(estado.historial);
        close(socket_fd);
        return NULL;
    }

    estado.historial[0] = '\0'; // \0 porque no se ha tecleado nada aun
    estado.longitud_palabra = 0;

    pid_t pid_ventana = -1;
    int id_ventana = -1;

    // Bucle principal de recepcion de datos
    while (1) {
        Mensaje mensaje_recibido;
        int bytes_leidos = recv(socket_fd, &mensaje_recibido, sizeof(Mensaje), 0);

        if (bytes_leidos <= 0) {
            // 0 = cliente cerro conexion sin TMSG_CIERRE crash, kill -9, entre otros
            if (bytes_leidos < 0) {
                perror("[hiloVentana] recv");
            }
            printf("[Ventana #%d PID %d] Conexion cerrada abruptamente — definiendo clasificacion final...\n", id_ventana, (int)pid_ventana);

            // Clasificamos final
            procesarPalabra(&estado, config);
            clasificarOracion(&estado);
            break;
        }

        pid_ventana = mensaje_recibido.pid_ventana;
        id_ventana = mensaje_recibido.id_ventana;

        // TIPO MSG_CIERRE, se cerro la ventana
        if (mensaje_recibido.tipo_mensaje == TMSG_CIERRE) {
            printf("[Ventana #%d PID %d] Mensaje de cierre recibido.\n", id_ventana, (int)pid_ventana);

            // Procesamos la ultima palabra/oracion pendiente antes de clasificar
            procesarPalabra(&estado, config);
            if (sumaVector(estado.frecuencias_oracion, estado.num_tipos) > 0) {
                clasificarOracion(&estado);
            }
            break;
        }

        // TIPO MSG_BACKSPACE, se quiere borrar
        if (mensaje_recibido.tipo_mensaje == TMSG_BACKSPACE) {
            if (estado.longitud_palabra > 0) {
                estado.longitud_palabra--;
                estado.palabra_actual[estado.longitud_palabra] = '\0';

                estado.longitud_historial--;
                estado.historial[estado.longitud_historial] = '\0';
            }
            continue;
        }

        // TIPO MSG_FIN_ORACION: Return
        if (mensaje_recibido.tipo_mensaje == TMSG_FIN_ORACION) {
            procesarPalabra(&estado, config);
            clasificarOracion(&estado);

            // Clasificamos la ventana hasta este punto
            int tipo_ventana = clasificarVentana(&estado, config);
            if (tipo_ventana >= 0) {
                printf("[Ventana #%d] Actualmente clasificada como: %s\n", id_ventana, config->tipos[tipo_ventana].nombre);
            } else {
                printf("[Ventana #%d] Actualmente tipo Desconocido.\n", id_ventana);
            }

            // Mostramos la inferencia de usuario con los datos hasta ahora */
            imprimirInferenciaUsuario(contadores_tipos, config, "  [progreso] ");

            agregarLetraAHistorial(&estado, '\n');
            continue;
        }

        // TIPO MSG_TECLA: caracter normal
        char c = mensaje_recibido.tecla;
        agregarLetraAHistorial(&estado, c);

        if (strchr(config->delimitadores, c) != NULL) {
            procesarPalabra(&estado, config); // es delimitador
        } else {
            if (estado.longitud_palabra + 1 < sizeof(estado.palabra_actual)) {
                estado.palabra_actual[estado.longitud_palabra++] = c;
                estado.palabra_actual[estado.longitud_palabra] = '\0';
            }
            // Si la palabra excede el tamano del buffer, simplemente ignoramos
            // no es una palabra que pertenezca a algun diccionario.
        }

        printf("[hiloVentana PID #%d]: %d | Caracter recibido: '%c'\n", pid_ventana, c);
    }

    // Se cierra ventana, hacemos clasificacion final

    int tipoVentanaFinal = clasificarVentana(&estado, config);

    printf("[Ventana #%d PID %d] Historial: \"%s\"\n", id_ventana, (int)pid_ventana, estado.historial);
    printf("[Ventana #%d PID %d] Vectores totales: ", id_ventana, (int)pid_ventana);
    for (int i = 0; i < estado.num_tipos; i++) {
        printf("%s=%d ", config->tipos[i].nombre, estado.vec_total[i]);
    }
    printf("\n");

    if (tipoVentanaFinal >= 0) {
        printf("[Ventana #%d PID %d] Clasificacion final: %s\n", id_ventana, (int)pid_ventana, config->tipos[tipoVentanaFinal].nombre);
    } else {
        printf("[Ventana #%d PID %d] Clasificacion final: Desconocida (menos de %d ocurrencias en todos los tipos)\n", id_ventana, (int)pid_ventana, config->umbral_ocurrencias);
    }

    if (tipoVentanaFinal >= 0) {
        pthread_mutex_lock(&mutex_resultados);
        contadores_tipos[tipoVentanaFinal]++;
        pthread_mutex_unlock(&mutex_resultados);
    }

    free(estado.historial);
    free(estado.frecuencias_oracion);
    free(estado.vec_total);
    free(estado.historial);
    close(socket_fd);

    return NULL;
}

void *hiloControl(void *param) {
    ArgsHilo *args = (ArgsHilo *)param;
    int socket_fd = args->socket_fd;
    ConfigIALearner *config = args->config;
    free(args);

    Mensaje msg;
    while (recv(socket_fd, &msg, sizeof(Mensaje), 0) > 0) {
        if (msg.tipo_mensaje == TMSG_CALC_USER) {
            printf("\n[hiloControl] Launcher solicita resultado final del lote.\n");
            imprimirInferenciaUsuario(contadores_tipos, config, "[RESULTADO] ");

            /* Reiniciamos para el siguiente lote */
            pthread_mutex_lock(&mutex_resultados);
            memset(contadores_tipos, 0, config->num_tipos * sizeof(int));
            pthread_mutex_unlock(&mutex_resultados);
        }
    }

    printf("[hiloControl] Conexion de control cerrada. Launcher desconectado.\n");
    close(socket_fd);
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
    imprimirConfig(&config); // muestra la config al arrancar, para veruficar nomas

    contadores_tipos = calloc(config.num_tipos, sizeof(int));
    if (!contadores_tipos) {
        fprintf(stderr, "[ialearner] Error calloc contadores_tipos\n");
        liberarConfig(&config);
        return 1;
    }
    
    arrHilos = malloc (100 * sizeof(pthread_t)); // Tam inicial
    if (!arrHilos) {
        fprintf(stderr, "[ialearner] Error malloc arrHilos\n");
        free(contadores_tipos);
        liberarConfig(&config);
        return 1;
    }
    
    
    int server_sockfd;
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

    printf("[ialearner] Receptor listo, esperando mensajes en el puerto %d...\n", puerto);

    // La primera conexion sera la del hilo de control
    int socket_control = accept(server_sockfd, (struct sockaddr *)&client_address, (socklen_t *)&addrlen);
    if (socket_control < 0) {
        perror("[ialearner] accept para hiloControl falló");
        goto limpiezaFinalError;
    }
    printf("[ialearner] Conexion de control establecida.\n");

    ArgsHilo *ctrl_args = malloc(sizeof(ArgsHilo));
    if (!ctrl_args) {
        fprintf(stderr, "[ialearner] Error malloc ArgsHiloControl\n");
        close(socket_control);
        goto limpiezaFinalError;
    }
    ctrl_args->socket_fd = socket_control;
    ctrl_args->config = &config;

    pthread_t hilo_control;
    if (pthread_create(&hilo_control, NULL, hiloControl, ctrl_args) != 0) {
        perror("[ialearner] pthread_create (control)");
        free(ctrl_args);
        close(socket_control);
        goto limpiezaFinalError;
    }

    // Bucle infinito para recibir las conexiones de las ventanas
    while(1) {

        int socket_hilo = accept(server_sockfd, (struct sockaddr *)&client_address, (socklen_t*)&addrlen);

        if (socket_hilo < 0) {
            perror("[ialearner] Accept para hiloVentana falló");
            continue;
        }

        if (totalHilos >= capacidadArrHilos){
            expandirArregloHilos();
        }

        ArgsHilo *ventana_args = malloc(sizeof(ArgsHilo));
        if (!ventana_args) {
            fprintf(stderr, "[ialearner] Error malloc ArgsHiloVentana\n");
            close(socket_hilo);
            continue;
        }
        ventana_args->socket_fd = socket_hilo;
        ventana_args->config = &config; // puntero a la config global
        
        if (pthread_create(&arrHilos[totalHilos], NULL, hiloVentana, ventana_args) != 0) {
            perror("[ialearner] pthread_create (ventana)");
            free(ventana_args);
            close(socket_hilo);
            continue;
        }

        // Hacemos detach y no join para no bloquear el bucle de ialearner y permitir que
        // otros launchers se conecten a la vez
        pthread_detach(arrHilos[totalHilos]);
        totalHilos++;
        totalHilos++;
    }

    close(server_sockfd);
    free(contadores_tipos);
    free(arrHilos);
    liberarConfig(&config);
    return 0;

    limpiezaFinalError:
    close(server_sockfd);
    free(contadores_tipos);
    free(arrHilos);
    liberarConfig(&config);
    return -1;

}

