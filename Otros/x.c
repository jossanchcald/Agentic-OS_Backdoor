#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/wait.h>

// Para el pipe
#define WRITE_END 1
#define READ_END 0

int main()
{

    int n;
    printf("Numero de procesos para la medicion: ");
    scanf("%d", &n);

    if (n <= 0)
    {
        fprintf(stderr, "n debe ser mayor que 0\n");
        return EXIT_FAILURE;
    }

    int fd[2];

    if (pipe(fd) == -1)
    {
        perror("pipe");
        return EXIT_FAILURE;
    }

    for (int i = 0; i < n; i++)
    {

        struct timeval tv0;
        if (gettimeofday(&tv0, NULL) != 0)
        {
            perror("gettimeofday");
            return EXIT_FAILURE;
        }

        pid_t pid = fork();

        if (pid < 0)
        {

            perror("fork");
            exit(EXIT_FAILURE);
        }
        else if (pid == 0)
        {

            close(fd[READ_END]);

            struct timeval tv1;
            if (gettimeofday(&tv1, NULL) != 0)
            {
                perror("gettimeofday");
                return EXIT_FAILURE;
            }

            // Convertimos a ms, gettimeofday retorna 2 valores uno en segundos y otro en microsegundos asi que en uno dividimos y en otro multiplicamos y sumamos
            // Y al final los restamos para saber el tiempo pasado en ms
            double deltams = ((tv1.tv_sec * 1000.0 + tv1.tv_usec / 1000.0) - (tv0.tv_sec * 1000.0 + tv0.tv_usec / 1000.0));

            if (write(fd[WRITE_END], &deltams, sizeof(deltams)) != sizeof(deltams))
            {
                perror("write");
                exit(EXIT_FAILURE);
            }

            close(fd[WRITE_END]);
            exit(EXIT_SUCCESS);
        }
    }

    close(fd[WRITE_END]);

    double suma = 0;
    double deltams;

    for (int i = 0; i < n; i++)
    {
        if (read(fd[READ_END], &deltams, sizeof(deltams)) != sizeof(deltams))
        {
            perror("write");
            exit(EXIT_FAILURE);
        }
        suma += deltams;
    }

    for (int i = 0; i < n; i++)
    {
        wait(NULL);
    }

    double tPromedio = suma / n;
    printf("Tiempo promedio de cambio de contexto: %.6fms\n", tPromedio);

    return 0;
}
