/* protocoloComms.h
 Define el protocolo de mensajes entre launcher y ialearner.
 Incluido por ambos programas. */

#ifndef PROTOCOLO_COMMS_H
#define PROTOCOLO_COMMS_H

#include <sys/types.h>

/* Tipos de mensaje */
// Primeros mensajes para conexion de control y de ventana
#define TMSG_HELLO_CONTROL 1
#define TMSG_HELLO_VENTANA 2

// Mensajes del bucle de escritura
#define TMSG_TECLA 3
#define TMSG_CIERRE 4
#define TMSG_BACKSPACE 5
#define TMSG_FIN_ORACION 6

// Mensajes de control
#define TMSG_CALC_USER 10
#define TMSG_RESULTADO_VENTANA 11
#define TMSG_CONTEXTO_USUARIO 12


/* Estructura de los mensajes  */
typedef struct {
    int tipo_mensaje;
    int id_launcher; // PID del proceso launcher; identifica su sesion en ialearner.

    int id_ventana;
    pid_t pid_ventana;
    char tecla;
    char nombre_tipo[64];
} Mensaje;

#endif