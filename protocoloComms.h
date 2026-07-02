
#include <sys/types.h>

#define PUERTO 8080
#define TMSG_TECLA 1
#define TMSG_CIERRE 2
#define TMSG_BACKSPACE 3
#define TMSG_FIN_ORACION 4

#define MSG_CALCULAR_USER 10 // Msg de control, terminaron todas las ventanas

// Estructura de los mensajes
typedef struct {
    int tipo_mensaje;
    pid_t pid_ventana;
    char tecla;
} Mensaje;

 