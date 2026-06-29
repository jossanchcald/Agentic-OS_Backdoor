#include <stdio.h>   // Lib estandar para hacer input y output
#include <stdlib.h>  // Lib estandar paraaa funciones comunes creo
#include <pthread.h> // Lib para manejar threads

// Librerias para manejar ventanas y eventos de teclado en X11 que uso el profe
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "protocoloComms.h"

pthread_t *arrHilos = NULL; // Array para almacenar los identificadores de los hilos
size_t totalHilos = 0; // Tamaño real del arreglo de ids de hilos
size_t capacidadArrHilos = 100; // El espacio reservado para el arreglo de ids de hilos

Mensaje **mensajesHilos = NULL;

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

void *runner (void *param) {
    // Aquí va el código que se ejecutará por cada hilo, un hilo por ventana
    // Recibir mensajes y procesarlos

}

int classifyWindow() {
    return 0;
}

int verificarPIDExistente(int pid, pid_t *arr, int tam) {

    int n = tam / sizeof(arr[0]);
    int encontrado = 0;

    for (int i = 0; i < n; i++) {
        if (arr[i] == pid) {
            encontrado = 1;
            break; // Detiene la búsqueda al encontrar el id
        }
    }

    return encontrado;
}

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


int main(){
    
    arrHilos = malloc (100 * sizeof(pthread_t)); // Tam inicial
    
    // Bucle infinito para recibir mensajes (Cola conceptual)
    while(1) {
        Mensaje mensaje_recibido;

        int servidor_fd, nuevo_socket; //Descriptor de archivo del socket del que recibe los msj por cada ventana 1 hilo
        struct sockaddr_in socket_address;
        int opt = 1;
        int addrlen = sizeof(socket_address);

        // Creamos el socket del receptor
        if ((servidor_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
            perror("Socket falló");
            exit(1);
        }

        // Reutilizar puerto para evitar el error "Address already in use"
        setsockopt(servidor_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        socket_address.sin_family = AF_INET;
        socket_address.sin_addr.s_addr = INADDR_ANY; // Escucha en todas las interfaces de red
        socket_address.sin_port = htons(PUERTO);

        // 2. Ligar socket al puerto
        if (bind(servidor_fd, (struct sockaddr *)&socket_address, sizeof(socket_address)) < 0) {
            perror("Bind falló");
            exit(EXIT_FAILURE);
        }

        // 3. Escuchar conexiones entrantes
        if (listen(servidor_fd, 100) < 0) { // Cola de espera del kernel de hasta 100 conexiones
            perror("Listen falló");
            exit(EXIT_FAILURE);
        }

        printf("Receptor listo, esperando mensajes en el puerto %d...\n", PUERTO);

        if ((nuevo_socket = accept(servidor_fd, (struct sockaddr *)&socket_address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept falló");
            continue;
        }

        // Leer la estructura completa desde el flujo de la red
        int bytes_leidos = recv(nuevo_socket, &mensaje_recibido, sizeof(Mensaje), 0);
        if (bytes_leidos > 0) {
            printf("[Mensaje Recibido] Tipo: %d\n", mensaje_recibido.tipo_mensaje);
            
            // NOTA: Aquí se empujaria 'mensaje_recibido' a la cola std::queue (STL) o de C.

            if (!verificarPIDExistente(mensaje_recibido.pid_ventana, arrPIDVentanas, totalVentanas))
            {
                arrPIDVentanas[totalVentanas] = mensaje_recibido.pid_ventana;
                totalVentanas++;
                if (totalHilos >= capacidadArrHilos) agregarCapacidadArregloHilos(&arrHilos, &totalHilos, &capacidadArrHilos);
                
                if (pthread_create(&arrHilos[totalHilos], NULL, runner, &dataHilos[i]) != 0) {
                    perror("pthread_create");
                    exit(EXIT_FAILURE);
                }

                totalHilos++;
                
            } else
            {
                /* code */
            }
            
            if (bytes_leidos == 0 || mensaje_recibido.tipo_mensaje == 2)
            {
                close(nuevo_socket);
                break;
            }
        }
    }

    for (int i = 0; i < totalVentanas; i++) {
        pthread_join(arrHilos[i], NULL);
    }

    close(servidor_fd);
    return 0;

}

