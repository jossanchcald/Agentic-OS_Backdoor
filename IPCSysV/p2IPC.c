#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <stdio.h>

// Example message structure
struct message {
	long mtype; 	// Message type pa clasificar
	char mtext[100]; // Message data, lo que envio
};

int main() {
    // Clave unica para la cola de mensajes
    key_t key = ftok("path_to_key_file", 'project_id');
    // 'path_to_key_file' can be any existing file, and 'project_id' is a unique integer

    // Crea o abre la cola de mensajes
    int msqid = msgget(key, 0666 | IPC_CREAT);
    // 0666 sets the permissions for the message queue, IPC_CREAT creates the queue if it doesn't exist

    // Creo un mensaje
    struct message msg;
    msg.mtype = 1;  // Defino el tipo de mensaje
    
    // 
    snprintf(msg.mtext, sizeof(msg.mtext), "Hello, world!");

    msgsnd(msqid, &msg, sizeof(msg.mtext), 0);
    // 'msqid' is the message queue ID, 'msg' is the message structure, sizeof(msg.mtext) is the size of the message data
    // 0 is the message flag (can be 0 or IPC_NOWAIT)

    msgctl(msqid, IPC_RMID, NULL);
    // 'msqid' is the message queue ID, IPC_RMID indicates the removal of the message queue,
    // NULL is the optional argument

    return 0;
}