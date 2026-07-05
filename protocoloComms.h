/* protocoloComms.h
   Define el protocolo de mensajes entre launcher y ialearner.
   Incluido por ambos programas. El puerto NO va aqui: lo lee cada
   proceso de su propio archivo de configuracion. */

#ifndef PROTOCOLO_COMMS_H
#define PROTOCOLO_COMMS_H

#include <sys/types.h>

/* Tipos de mensaje */
#define TMSG_TECLA 1  // caracter normal 
#define TMSG_CIERRE 2  // ventana cerrada (Escape, X, Alt+F4, kill, etc.) 
#define TMSG_BACKSPACE 3  // borrar ultimo caracter
#define TMSG_FIN_ORACION 4  // Return: fin de oracion 
#define TMSG_CALC_USER 10 // mensaje de control para que se calcule el tipo de usuario

typedef struct {
    int tipo_mensaje;
    pid_t pid_ventana;
    int id_ventana; // ID local del launcher
    char tecla;
} Mensaje;

#endif /* PROTOCOLO_COMMS_H */