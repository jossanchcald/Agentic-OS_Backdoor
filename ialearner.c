// Librerias para IPC POSIX Message Queue
#include <fcntl.h>    /* For O_* constants (O_CREAT, O_RDWR, etc.) */
#include <sys/stat.h> /* For mode constants (permissions) */
#include <mqueue.h>   /* For POSIX message queue functions */

#include <stdio.h>   // A ver lib estandar para hacer input y output
#include <stdlib.h>  // Lib estandar paraaa funciones comunes creo
#include <pthread.h> // Lib para manejar threads

// Librerias para manejar ventanas y eventos de teclado en X11 que uso el profe
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <stdio.h>

typedef struct {
    KeySym keysym;
    pid_t pid;
} Mensaje;

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
    // Aquí va el código que se ejecutará en el thread
    // Por ejemplo, podrías recibir mensajes de la cola y procesarlos




}

int classifyWindow() {
    
}

int main(){
    
    // Abrir la queue
    mqd_t mq = mq_open("/backdoor_queue", O_RDWR);

    while (1) {
        // Obtenemos el mensaje al principio de la cola
        char buffer[sizeof(Mensaje)]; // Aqui llega el mensjae
        unsigned int prio; // Aqui llega la prioridad del mensaje

        mq_receive(mq, buffer, sizeof(Mensaje), &prio);

        Mensaje *msg = (Mensaje *)buffer;
        char *name = XKeysymToString(msg->keysym);
    }

}

