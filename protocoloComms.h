
#include <sys/types.h>

#define PUERTO 8080  

// Estructura fija de los mensajes
typedef struct {
    int tipo_mensaje;
    pid_t pid_ventana;
    char tecla;
} Mensaje;

 