#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <stdio.h>
#include <string.h>

struct message {
	long mtype;
	char mtext[100];
};

int main() {
	key_t key = ftok("path_to_key_file", 'project_id');
	int msqid = msgget(key, 0666 | IPC_CREAT);

	struct message rcv_msg;

    msgrcv(msqid, &rcv_msg, sizeof(rcv_msg.mtext), 1, 0);
    // 'msqid' is the message queue ID, '&rcv_msg' is the receiving message structure,
    // sizeof(rcv_msg.mtext) is the size of the receiving message data
    // 1 is the message type to receive, 0 is the message flag (can be 0 or IPC_NOWAIT)

    // Access the received message data
    printf("Received message: %s\n", rcv_msg.mtext);

    return 0;
}