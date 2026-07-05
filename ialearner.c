#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "protocoloComms.h"
#include "config.h"

// - harcode
#define NUM_PALABRAS_DICC 10
#define NUM_DICCIONARIOS 3
#define TAM_MAX_PALABRA 32

// Tipos de ventanas - harcode
#define TIPO_CORREO 1
#define TIPO_ARTICULO 2
#define TIPO_REPORTE 3

// Tipos de usuarios - hardcode
#define USERT_DESCONOCIDO 0
#define USERT_PERS_ADMIN 1
#define USERT_PERS_TECN 2
#define USERT_PROF 3
#define USERT_ESTUD 4

#define UMBRAL_ADMIN 0.90 // hardcodeado

pthread_t *arrHilos = NULL; // Array para almacenar los identificadores de los hilos
size_t totalHilos = 0; // Tamaño real del arreglo de ids de hilos
size_t capacidadArrHilos = 100; // El espacio reservado para el arreglo de ids de hilos

pthread_mutex_t mutex_resultados = PTHREAD_MUTEX_INITIALIZER;
// - harcode
int ventanasCorreo = 0;
int ventanasArticulo = 0;
int ventanasReporte = 0;

typedef struct {
    char palabra_actual[TAM_MAX_PALABRA];
    size_t longitud_palabra;

    // Vectores de frecuencias de la oracion actual (bag of words) (se resetea en cada Return)
    int frecuencias_correo[NUM_PALABRAS_DICC];
    int frecuencias_articulo[NUM_PALABRAS_DICC];
    int frecuencias_reporte[NUM_PALABRAS_DICC];

    // Acumulado de TODA la ventana (NUNCA se resetea, suma todas las oraciones)
    int vec_total_correo[NUM_PALABRAS_DICC];
    int vec_total_articulo[NUM_PALABRAS_DICC];
    int vec_total_reporte[NUM_PALABRAS_DICC];
 
    int total_oraciones;

    char *historial; // para guardar que es todo lo que ha escrito en la ventana
    size_t longitud_historial;
    size_t capacidad_historial;
} EstadoClasificacionVentana; // El estado actual de clasificacion de la ventana, todo lo que se ha preisonado, y la palabra y oracion actual q se tiene

// Delimitadores que separan palabras - harcode
const char *delimitadores = " ,./|:;\'\t";

// Diccionarios - harcode
char* correo_electronico[] = {
    "Thank",
    "Please",
    "Regards",
    "Meeting",
    "Attached",
    "Information",
    "Update",
    "Schedule",
    "Team",
    "Project"
};
char* articulo_cientifico[] = {
    "Data",
    "Analysis",
    "Results",
    "Method",
    "Study",
    "Model",
    "Research",
    "System",
    "Significant",
    "Effect"
};
char* reporte[] = {
    "System",
    "Data",
    "Network",
    "Security",
    "Application",
    "Server",
    "User",
    "Performance",
    "Service",
    "Infrastructure"
};

/* retorna el nombre del tipo de ventana - harcode*/
const char *nombreTipoVentana(int tipo) {
    switch (tipo) {
        case TIPO_CORREO: 
            return "Correo electronico";
        case TIPO_ARTICULO: 
            return "Articulo cientifico";
        case TIPO_REPORTE: 
            return "Reporte";
        default: 
            return "Desconocido";
    }
}

/* Retorna el nombre del tipo de usuario  - harcode*/
const char *nombreUsuario(int tipo) {
    switch (tipo) {
        case USERT_PERS_ADMIN: 
            return "Personal Administrativo";
        case USERT_PERS_TECN: 
            return "Personal Tecnico";
        case USERT_PROF: 
            return "Profesor";
        case USERT_ESTUD: 
            return "Estudiante";
        default: 
            return "Indeterminado";
    }
}

/* Setea los vectores de las frecuencias BoW a 0 (para otra oracion)  - harcode*/
void resetVectoresEstadoOracion(EstadoClasificacionVentana *estado) {
    memset(estado->frecuencias_correo, 0, sizeof(estado->frecuencias_correo));
    memset(estado->frecuencias_articulo, 0, sizeof(estado->frecuencias_articulo));
    memset(estado->frecuencias_reporte, 0, sizeof(estado->frecuencias_reporte));
}

/* Verifica a que diccionario pertenece una palabra, y segun el diccionario
 aumenta la frecuencia de esa palabra en ese vector  - harcode*/
void agregarPalabraAlVector(EstadoClasificacionVentana *estado, const char *palabra) {
    for (int i = 0; i < NUM_PALABRAS_DICC; i++) {
        if (strcasecmp(palabra, correo_electronico[i]) == 0) estado->frecuencias_correo[i]++;
        if (strcasecmp(palabra, articulo_cientifico[i]) == 0) estado->frecuencias_articulo[i]++;
        if (strcasecmp(palabra, reporte[i]) == 0) estado->frecuencias_reporte[i]++;
    }
}

/* Agrega un caracter al historial completo, aumentando el buffer si hace falta
 Se agrega lo que se presionó y el \0 de fin de palabra */
void agregarLetraAHistorial(EstadoClasificacionVentana *estado, char c) {

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

/* Procesa una palabra, usa agregarPalabraAlVector, y reinicia el contador de la longitud, */
void procesarPalabra(EstadoClasificacionVentana *estado) {
    if (estado->longitud_palabra == 0) {
        return;
    }

    estado->palabra_actual[estado->longitud_palabra] = '\0'; // Por si acaso vuelvo a ponerle un final por si no tenia
    agregarPalabraAlVector(estado, estado->palabra_actual);
    estado->longitud_palabra = 0; // Reiniciamos tam
}

/* Suma los contenidos de un vector */
int sumaVector(int *vector, int tam) {
    int suma = 0;
    for (int i = 0; i < tam; i++) suma += vector[i];
    return suma;
}

/* Clasifica la oracion actual segun los valores de los vectores y 
 actualiza el conteo de tipo de usuario */
int clasificarOracion(EstadoClasificacionVentana *estado) {
    int total_correo = sumaVector(estado->frecuencias_correo, NUM_PALABRAS_DICC);
    int total_articulo = sumaVector(estado->frecuencias_articulo, NUM_PALABRAS_DICC);
    int total_reporte = sumaVector(estado->frecuencias_reporte, NUM_PALABRAS_DICC);

    int categoria;
    if (total_correo >= total_articulo && total_correo >= total_reporte) {
        categoria = TIPO_CORREO;
    } else if (total_articulo >= total_reporte) {
        categoria = TIPO_ARTICULO;
    } else {
        categoria = TIPO_REPORTE;
    }
    estado->total_oraciones++;

    // Acumulamos en el total de la ventana, DESPUES reseteamos la oracion
    for (int i = 0; i < NUM_PALABRAS_DICC; i++) {
        estado->vec_total_correo[i]   += estado->frecuencias_correo[i];
        estado->vec_total_articulo[i] += estado->frecuencias_articulo[i];
        estado->vec_total_reporte[i]  += estado->frecuencias_reporte[i];
    }
    resetVectoresEstadoOracion(estado); // limpiamos los vectores
    return categoria;
}

/* Clasifica la ventana segun los vectores de frecuencia de cada diccionario, 
 minimo 3 ocurrencias para clasificar, si mas de uno cumple gana el de mayor suma */
int clasificarVentana(EstadoClasificacionVentana *estado) {
    int fCorreo = sumaVector(estado->vec_total_correo,   NUM_PALABRAS_DICC);
    int fArticulo = sumaVector(estado->vec_total_articulo, NUM_PALABRAS_DICC);
    int fReporte = sumaVector(estado->vec_total_reporte,  NUM_PALABRAS_DICC);

    int candCorreo = (fCorreo >= 3);
    int candArticulo = (fArticulo >= 3);
    int candReporte = (fReporte >= 3);

    if (!(candCorreo) && !(candArticulo) && !(candReporte)) return USERT_DESCONOCIDO; // Es de tipo desconocido

    int tipoVentana = 0;
    int mayorFrecuencia = -1;
    if (candCorreo && fCorreo > mayorFrecuencia) {
        mayorFrecuencia = fCorreo;
        tipoVentana = TIPO_CORREO;
    }
    if (candArticulo && fArticulo > mayorFrecuencia) {
        mayorFrecuencia = fArticulo;
        tipoVentana = TIPO_ARTICULO;
    }
    if (candReporte && fReporte > mayorFrecuencia) {
        mayorFrecuencia = fReporte;
        tipoVentana = TIPO_REPORTE;
    }
    return tipoVentana;
}

/* Duplica la capacidad del arreglo de hilos*/
void agregarCapacidadArregloHilos(pthread_t **arr, size_t *size, size_t *cap){
    // Si esta lleno duplicamos la capacidad
    if (*size >= *cap) { 
        *cap = *cap * 2;

        pthread_t *tmp = realloc(*arr, (*cap) * sizeof(pthread_t));
        if (tmp == NULL) {
            fprintf(stderr, "Error realloc\n");
            exit(EXIT_FAILURE);
        }
        *arr = tmp;
    }
}

/* Determina el usuario del sistema segun las proporciones de las ventanas */
int inferirUsuarioPorProporcion(double pCorreo, double pArticulo, double pReporte) {
    if (pCorreo >= UMBRAL_ADMIN) return USERT_PERS_ADMIN;
    if (pCorreo <= pArticulo && pCorreo <= pReporte) return USERT_ESTUD;
    if (pArticulo <= pCorreo && pArticulo <= pReporte) return USERT_PERS_TECN;
    return USERT_PROF;
}

/* Funcionamiento del hilo asignado a cada ventana */
void *hiloVentana(void *param) {
    int socket_fd = *(int *)param;
    free(param);

    // Creamos la variable de estado para la ventana
    EstadoClasificacionVentana estado;
    memset(&estado, 0, sizeof(estado)); // inicializa todo a cero de una vez
    estado.capacidad_historial = 256;
    estado.longitud_historial = 0;
    estado.historial = malloc(estado.capacidad_historial);

    if (estado.historial == NULL) {
        fprintf(stderr, "Error malloc en hiloVentana\n");
        close(socket_fd);
        return NULL;
    }

    estado.historial[0] = '\0'; // \0 porque no se ha tecleado nada aun
    estado.longitud_palabra = 0;

    pid_t pid_ventana_actual = -1;

    while (1) {
        Mensaje mensaje_recibido;
        int bytes_leidos = recv(socket_fd, &mensaje_recibido, sizeof(Mensaje), 0);

        if (bytes_leidos <= 0) {
            if (bytes_leidos < 0) {
                perror("Error en recv dentro de runner");
            }
            printf("Conexion cerrada.\n");
            break;
        }

        pid_ventana_actual = mensaje_recibido.pid_ventana;

        // TIPO MSG_CIERRE, se cerro la ventana
        if (mensaje_recibido.tipo_mensaje == TMSG_CIERRE) {
            printf("Recibido mensaje de cierre.\n");

            // Procesamos la ultima palabra/oracion pendiente antes de clasificar
            procesarPalabra(&estado);
            int hayPendiente = sumaVector(estado.frecuencias_correo, NUM_PALABRAS_DICC) + sumaVector(estado.frecuencias_articulo, NUM_PALABRAS_DICC) + sumaVector(estado.frecuencias_reporte, NUM_PALABRAS_DICC);
            if (hayPendiente > 0) {
                clasificarOracion(&estado);
            }

            int tipoDoc = clasificarVentana(&estado);
            printf("Clasificacion final de la ventana: %s\n", nombreTipoVentana(tipoDoc));

            // Actualiza el contador global del lote
            pthread_mutex_lock(&mutex_resultados);
            if (tipoDoc == TIPO_CORREO) {
                ventanasCorreo++;
            } else if (tipoDoc == TIPO_ARTICULO) {
                ventanasArticulo++;
            } else if (tipoDoc == TIPO_REPORTE) {
                ventanasReporte++;
            }
            // si es desconocido (0) simplemente no se cuenta para las proporciones
            pthread_mutex_unlock(&mutex_resultados);

            int usuario = 0;
            pthread_mutex_lock(&mutex_resultados);
            int total = ventanasCorreo + ventanasArticulo + ventanasReporte;

            if (total > 0) {
                double pCorreo = (double)ventanasCorreo / total;
                double pArticulo = (double)ventanasArticulo / total;
                double pReporte = (double)ventanasReporte / total;
                usuario = inferirUsuarioPorProporcion(pCorreo, pArticulo, pReporte);
            }
            pthread_mutex_unlock(&mutex_resultados);

            printf("Tipo de usuario actual: %s\n\n", nombreUsuario(usuario));

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
            // proc la ultima palabra/oracion que se tenia
            procesarPalabra(&estado); 
            int categoria = clasificarOracion(&estado);

            int usuario = 0;

            pthread_mutex_lock(&mutex_resultados);
            int total = ventanasCorreo + ventanasArticulo + ventanasReporte;

            if (total > 0) {
                double pCorreo = (double)ventanasCorreo / total;
                double pArticulo = (double)ventanasArticulo / total;
                double pReporte = (double)ventanasReporte / total;
                usuario = inferirUsuarioPorProporcion(pCorreo, pArticulo, pReporte);
            }
            pthread_mutex_unlock(&mutex_resultados);

            printf("Tipo de usuario actual: %s\n\n", nombreUsuario(usuario));

            if (estado.longitud_historial > 0) {
                agregarLetraAHistorial(&estado, '\n');
            }
            continue;
        }

        // TIPO MSG_TECLA: caracter normal
        char c = mensaje_recibido.tecla;
        agregarLetraAHistorial(&estado, c);

        if (strchr(delimitadores, c) != NULL) {
            procesarPalabra(&estado); // es delimitador
        } else {
            if (estado.longitud_palabra + 1 < TAM_MAX_PALABRA) {
                estado.palabra_actual[estado.longitud_palabra++] = c;
                estado.palabra_actual[estado.longitud_palabra] = '\0';
            }
        
            // Si la "palabra" excede el tamano del buffer, simplemente ignoramos
            // no es una palabra que pertenezca a algun diccionario.
        }

        printf("Caracter recibido: '%c'\n", pid_ventana_actual, c);
    }

    // Se cierra ventana, hacemos clasificacion final

    int tipoVentana = clasificarVentana(&estado);
    printf("Texto completo: \"%s\"\n", estado.historial);
    printf("Clasificacion final: %d\n", tipoVentana);

    free(estado.historial);
    close(socket_fd);

    return NULL;
}

void *hiloControl(void *param) {

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

    
    arrHilos = malloc (100 * sizeof(pthread_t)); // Tam inicial
    if (!arrHilos) {
        fprintf(stderr, "Error malloc launcher arrHilos");
        return -1;
    }
    
    
    int server_sockfd, socket_hilo; //Descriptor de archivo del socket del que recibe los msj por cada ventana 1 hilo

    struct sockaddr_in socket_address;
    struct sockaddr_in client_address;
    
    int opt = 1;
    int addrlen = sizeof(socket_address);

    socket_address.sin_family = AF_INET;
    socket_address.sin_addr.s_addr = INADDR_ANY; // Escucha en todas las interfaces de red
    socket_address.sin_port = htons(puerto);

    // Remove any old socket and create an unnamed socket for the server.
    if ((server_sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket falló");
        exit(1);
    }

    // Configurar la reutilización del puerto, para evitar el error "Address already in use"
    // Cuando el servidor se cierra, el puerto no se libera de inmediato, si se vuelve a abrir muy rapido
    // Saldra el error y no podra hacer bind
    if (setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt falló");
        exit(EXIT_FAILURE);
    }

    // Ligar socket al puerto
    if (bind(server_sockfd, (struct sockaddr *)&socket_address, sizeof(socket_address)) < 0) {
        perror("Socket bind fallo");
        exit(EXIT_FAILURE);
    }

    // Escuchar conexiones entrantes
    if (listen(server_sockfd, 10) < 0) { // Cola de espera del kernel de hasta 10 conexiones
        perror("Socket listen fallo");
        exit(EXIT_FAILURE);
    }

    printf("Receptor listo, esperando mensajes en el puerto %d...\n", puerto);

    /*
    int socket_control_fd = accept(server_sockfd, (struct sockaddr *)&client_address, (socklen_t*)&addrlen);
    if (socket_control_fd < 0) { 
        perror("Accept control fallo");
        exit(EXIT_FAILURE);
    }

    int *ctrl_ptr = malloc(sizeof(int));
    *ctrl_ptr = socket_control_fd;
    pthread_t hiloDeControl;
    pthread_create(&hiloDeControl, NULL, hiloControl, ctrl_ptr); */

    // Bucle infinito para recibir las conexiones de las ventanas
    while(1) {

        if ((socket_hilo = accept(server_sockfd, (struct sockaddr *)&client_address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept falló");
            continue;
        }

        if (totalHilos >= capacidadArrHilos){
            agregarCapacidadArregloHilos(&arrHilos, &totalHilos, &capacidadArrHilos);
        }

        int *socket_ptr = malloc(sizeof(int));
        *socket_ptr = socket_hilo;
        
        if (pthread_create(&arrHilos[totalHilos], NULL, hiloVentana, socket_ptr) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }

        totalHilos++;
    }

    for (int i = 0; i < totalHilos; i++) {
        pthread_join(arrHilos[i], NULL);
    }

    close(server_sockfd);
    liberarConfig(&config);
    return 0;

}

