#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>

// Esta estructura va a almacenar directamente el tiempo inicial y el resultante
typedef struct{
    struct timeval tv0;
    double resultado;
} tHilo;

// Funcion dl hilo
void *runner(void *arg)
{
    tHilo *data = (tHilo *)arg; // Definimos el tipo del arg
    struct timeval tv1;

    if (gettimeofday(&tv1, NULL) != 0){
        perror("gettimeofday");
        pthread_exit(NULL);
    }

    // Calculamos la diferencia en milisegundos como en el de procesos
    double deltams = (tv1.tv_sec * 1000.0 + tv1.tv_usec / 1000.0) - (data->tv0.tv_sec * 1000.0 + data->tv0.tv_usec / 1000.0);
    
    data->resultado = deltams; // Guardamos el resultado en la estructura
    pthread_exit(NULL);
}

int main() {

    int n;
    printf("Numero de hilos para la medición: ");
    scanf("%d", &n);

    if (n <= 0) {
        fprintf(stderr, "n debe ser mayor que 0\n");
        return EXIT_FAILURE;
    }

    pthread_t threads[n]; // Array para almacenar los identificadores de los hilos
    tHilo datos[n]; // Array para almacenar los datos de cada hilo
    
    for (int i = 0; i < n; i++) {

        if (gettimeofday(&datos[i].tv0, NULL) != 0) {
            perror("gettimeofday");
            exit(EXIT_FAILURE);
        }

        if (pthread_create(&threads[i], NULL, runner, &datos[i]) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < n; i++) {
        pthread_join(threads[i], NULL);
    }

    double suma = 0;

    for(int i = 0; i < n; i++) {
        suma += datos[i].resultado;
    }

    double tPromedio = suma / n;
    printf("Tiempo promedio de cambio de contexto: %.6fms\n", tPromedio);

    return 0;
}