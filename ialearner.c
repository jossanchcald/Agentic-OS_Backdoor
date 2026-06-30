#include <stdio.h>   // Lib estandar para hacer input y output
#include <stdlib.h>  // Lib estandar paraaa funciones comunes creo
#include <pthread.h> // Lib para manejar threads

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "protocoloComms.h"

#define TIPO_CORREO 1
#define TIPO_ARTICULO 2
#define TIPO_REPORTE 3
#define NUM_PALABRAS_DICC 10

#define USERT_PERS_ADMIN 1
#define USERT_PERS_TECN 2
#define USERT_PROF 3
#define USERT_ESTUD 4

pthread_t *arrHilos = NULL; // Array para almacenar los identificadores de los hilos
size_t totalHilos = 0; // Tamaño real del arreglo de ids de hilos
size_t capacidadArrHilos = 100; // El espacio reservado para el arreglo de ids de hilos

typedef struct {
    char *historial; // historial completo de lo que va presionando el usuario en la ventana
    size_t longitud_historial;
    size_t capacidad_historial;
    size_t cantidad_palabras;

    char palabra_actual[32]; // palabra en construccion
    size_t longitud_palabra;

    int puntos_correo;
    int puntos_articulo;
    int puntos_reporte;
} EstadoClasificacionVentana; // El estado actual de clasificacion de la ventana, todo lo que se ha preisonado, y la palabra actual q se tiene

// Definimos que caracteres cuentan como delimitadores de palabra
const char *delimitadores = " ,./|:;\'\r\n\t";

// Diccionarios
const char* correo_electronico[] = {
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

const char* articulo_cientifico[] = {
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

const char* reporte[] = {
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

// Se clasifica la ventana segun los puntos que tenga de cada diccionario, (num de palabras asoc. a los dicc)
int clasificarVentana(EstadoClasificacionVentana *estado) {

    if (estado->puntos_correo > estado->puntos_articulo && estado->puntos_correo > estado->puntos_reporte) {
        return TIPO_CORREO;
    } else if (estado->puntos_articulo > estado->puntos_correo && estado->puntos_articulo > estado->puntos_reporte) {
        return TIPO_ARTICULO;
    } else {
        return TIPO_REPORTE;
    }
    
}

// Se determina el usuario del sistema segun las proporciones de las ventanas,
// Implementacion base y no correcta, pero de forma arquitectonica
int determinarUsuario(EstadoClasificacionVentana *estado) {
    // Como es proporcional, transformamos las frec. de los valores a porcentajes
    double propCorreo = ((estado->puntos_correo)/ estado->cantidad_palabras)* 100;
    double propArticuloCientf = ((estado->puntos_articulo)/ estado->cantidad_palabras)* 100;
    double propReporte = ((estado->puntos_reporte)/ estado->cantidad_palabras)* 100;

    /* Definir una comparacion de proporciones apra determinar el usuario de la ventana
    if () {
        return 1;
    } else if () {
        return 2;
    } else {
        return 3;
    }
    */
}

// Funcion para duplicar la capacidad del arreglo de hilos, por si se llena
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

// Agrega un caracter al historial completo, creciendo el buffer si hace falta
// Se agrega lo que se presionó y el \0 de fin de palabra
void agregarAlHistorial(EstadoClasificacionVentana *estado, char c) {

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

// Verifica si una palabra pertenece a un diccionario, comparando las palabras del dicc
// y la que queremos verif sin importar mayusculas
int perteneceADiccionario(const char *palabra, const char **diccionario, int tam) {

    for (int i = 0; i < tam; i++) {
        if (strcasecmp(palabra, diccionario[i]) == 0) {
            return 1;
        }
    }
    return 0;

}

// Cuando ya se ingresó una palabra (se detecta espacio, coma, punto, etc), se procesa para
// determinar de que tipo es
void procesarPalabraCompleta(EstadoClasificacionVentana *estado) {

    if (estado->longitud_palabra == 0) {
        return; // no habia palabra pendiente (dos espacios seguidos, etc)
    }

    estado->cantidad_palabras++;
    estado->palabra_actual[estado->longitud_palabra] = '\0'; // Al final de la palabra ponemos fin

    // Verificamos en que diccionario está la palabra, si en ninguno, entonces la palabra no cuenta 
    if (perteneceADiccionario(estado->palabra_actual, correo_electronico, NUM_PALABRAS_DICC)) {
        estado->puntos_correo++;
    }
    if (perteneceADiccionario(estado->palabra_actual, articulo_cientifico, NUM_PALABRAS_DICC)) {
        estado->puntos_articulo++;
    }
    if (perteneceADiccionario(estado->palabra_actual, reporte, NUM_PALABRAS_DICC)) {
        estado->puntos_reporte++;
    }

    estado->longitud_palabra = 0; // se reinicia para la siguiente palabra
}

void *hiloVentana(void *param) {
    int socket_fd = *(int *)param;
    free(param);

    // Inicializamos el estado de clasificacion para esta ventana
    EstadoClasificacionVentana estado;
    estado.capacidad_historial = 256;
    estado.longitud_historial = 0;
    estado.historial = malloc(estado.capacidad_historial);

    if (estado.historial == NULL) {
        fprintf(stderr, "Error malloc en funcion de hilo\n");
        close(socket_fd);
        return NULL;
    }

    estado.historial[0] = '\0'; // Mantenemos siempre el caracter \0 para saber donde termina
    estado.longitud_palabra = 0;
    estado.puntos_correo = 0;
    estado.puntos_articulo = 0;
    estado.puntos_reporte = 0;

    pid_t pid_ventana_actual = -1;

    while (1) {
        Mensaje mensaje_recibido;
        int bytes_leidos = recv(socket_fd, &mensaje_recibido, sizeof(Mensaje), 0);

        if (bytes_leidos <= 0) {
            if (bytes_leidos < 0) {
                perror("Error en recv dentro de runner");
            }
            printf("[Hilo PID %d] Conexion cerrada (sin mensaje de cierre explicito).\n", pid_ventana_actual);
            break;
        }

        pid_ventana_actual = mensaje_recibido.pid_ventana;

        // TIPO 2: cierre de ventana (Escape, X, Alt+F4, etc.)
        if (mensaje_recibido.tipo_mensaje == 2) {
            printf("[Hilo PID %d] Recibido mensaje de cierre.\n", pid_ventana_actual);
            procesarPalabraCompleta(&estado); // por si quedo una palabra sin cerrar con delimitador
            break;
        }

        // TIPO 3: BackSpace
        if (mensaje_recibido.tipo_mensaje == 3) {
            // Solo afecta la palabra en construccion (no deshace palabras ya formadas, asi que solo hasta el delimitador)
            if (estado.longitud_palabra > 0) {
                estado.longitud_palabra--;
                estado.palabra_actual[estado.longitud_palabra] = '\0';

                estado.longitud_historial--;
                estado.historial[estado.longitud_historial] = '\0';
            }
            printf("[Hilo PID %d] BackSpace procesado.\n", pid_ventana_actual);
            continue;
        }

        // TIPO 1: caracter normal
        char c = mensaje_recibido.tecla;
        agregarAlHistorial(&estado, c);

        if (strchr(delimitadores, c) != NULL) {

            procesarPalabraCompleta(&estado);

            // Clasificacion dinámica, por cada palabra, vamos determinando el tipo de ventana que es
            int categoria_actual = clasificarVentana(&estado);
            printf("[Hilo PID %d] Progreso -> correo:%d articulo:%d reporte:%d | categoria actual: %d\n", pid_ventana_actual, estado.puntos_correo, estado.puntos_articulo, estado.puntos_reporte, categoria_actual);

        } else {

            // Caracter normal
            if (estado.longitud_palabra + 1 < sizeof(estado.palabra_actual)) {
                estado.palabra_actual[estado.longitud_palabra++] = c;
                estado.palabra_actual[estado.longitud_palabra] = '\0';
            }
            // Si la "palabra" excede el tamano del buffer, simplemente truncamos, no es una palabra que pertenezca a algun tipo.
        }

        printf("[Hilo PID %d] Caracter recibido: '%c'\n", pid_ventana_actual, c);
    }

    // Se cierra ventana hacemos clasificacion final

    int categoria_final = clasificarVentana(&estado);
    printf("[Hilo PID %d] Texto completo: \"%s\"\n", pid_ventana_actual, estado.historial);
    printf("[Hilo PID %d] Puntos -> correo:%d articulo:%d reporte:%d\n", pid_ventana_actual, estado.puntos_correo, estado.puntos_articulo, estado.puntos_reporte);
    printf("[Hilo PID %d] Clasificacion final: %d\n", pid_ventana_actual, categoria_final);

    free(estado.historial);
    close(socket_fd);

    return NULL;
}

int main(){
    
    arrHilos = malloc (100 * sizeof(pthread_t)); // Tam inicial
    
    int server_sockfd, socket_hilo; //Descriptor de archivo del socket del que recibe los msj por cada ventana 1 hilo

    struct sockaddr_in socket_address;
    struct sockaddr_in client_address;
    
    int opt = 1;
    int addrlen = sizeof(socket_address);

    socket_address.sin_family = AF_INET;
    socket_address.sin_addr.s_addr = INADDR_ANY; // Escucha en todas las interfaces de red
    socket_address.sin_port = htons(PUERTO);

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

    printf("Receptor listo, esperando mensajes en el puerto %d...\n", PUERTO);

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
    return 0;

}

