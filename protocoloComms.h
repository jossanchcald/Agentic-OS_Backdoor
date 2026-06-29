
#include <sys/types.h> // para pid_t
#include <X11/keysym.h> // para KeySym

#define PUERTO 8080  

// Estructura fija de los mensajes
typedef struct {
    int tipo_mensaje;
    pid_t pid_ventana;
    KeySym tecla;
} Mensaje;

