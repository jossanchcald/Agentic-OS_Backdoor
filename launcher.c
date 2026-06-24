#include <stdio.h>   // A ver lib estandar para hacer input y output
#include <stdlib.h>  // Lib estandar paraaa funciones comunes creo
#include <unistd.h>  // Lib para funciones de sleep, fork y otras cosas relacionadas con el sistema operativo

//#include <time.h> // Lib pa usar para generar numeros aleatorios
#include <time.h>
#include <X11/Xutil.h>

// Librerias para manejar ventanas y eventos de teclado en X11 que uso el profe
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <stdio.h>

// Librerias para IPC POSIX Message Queue
#include <fcntl.h>    /* For O_* constants (O_CREAT, O_RDWR, etc.) */
#include <sys/stat.h> /* For mode constants (permissions) */
#include <mqueue.h>   /* For POSIX message queue functions */

typedef struct {
    KeySym keysym;
    pid_t pid;
} Mensaje;

int createWindow(mqd_t mq)
{
    Display *display = XOpenDisplay(NULL);
    if (!display)
    {
        fprintf(stderr, "Cannot open display\n");
        return 1;
    }

    int screen = DefaultScreen(display);

    Window window = XCreateSimpleWindow(
        display,
        RootWindow(display, screen),
        10, 10, 400, 200,
        1,
        BlackPixel(display, screen),
        WhitePixel(display, screen));

    XSelectInput(display, window, ExposureMask | KeyPressMask);
    XMapWindow(display, window);

    XEvent event;

    while (1)
    {
        XNextEvent(display, &event);

        if (event.type == KeyPress)
        {
            KeySym keysym = XLookupKeysym(&event.xkey, 0);

            Mensaje msg = {
                keysym, 
                getpid()
            };

            // Mando la tecla que se presionó con prioridad 1
            mq_send(mq, &msg, sizeof(Mensaje), 1);

            char *name = XKeysymToString(keysym);
            if (name)
            {
                printf("Key pressed: %s\n", name);
            }
            else
            {
                printf("Unknown key\n");
            }

            if (keysym == XK_Escape)
                printf("PROCESS_FINISHED");
                break;
                
        }
    }

    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 0;
}

int createWindowRndPos() {
    Display *display = XOpenDisplay(NULL);
    if (!display)
    {
        fprintf(stderr, "Cannot open display\n");
        return 1;
    }

    int screen = DefaultScreen(display);
    int anchoDsp = DisplayWidth(display, screen);
    int altoDsp = DisplayHeight(display, screen);

    srand(time(NULL) + getpid()); // Inicializamos el rand con una semilla pa q sea random de verda
    
    int x = rand() % (anchoDsp + 20) + 10;
    int y = rand() % (altoDsp + 20) + 10;

    Window window = XCreateSimpleWindow(
        display,
        RootWindow(display, screen),
        x, y, 400, 200,
        1,
        BlackPixel(display, screen),
        WhitePixel(display, screen));

    XSizeHints hints;
    hints.flags = USPosition;
    hints.x = x;
    hints.y = y;

    XSetNormalHints(display, window, &hints);

    XSelectInput(display, window, ExposureMask | KeyPressMask);
    XMapWindow(display, window);

    XEvent event;

    while (1)
    {
        XNextEvent(display, &event);

        if (event.type == KeyPress)
        {
            KeySym keysym = XLookupKeysym(&event.xkey, 0);

            char *name = XKeysymToString(keysym);
            if (name)
            {
                printf("Key pressed: %s\n", name);
            }
            else
            {
                printf("Unknown key\n");
            }

            if (keysym == XK_Escape)
                break;
        }
    }

    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 0;
}

int main()
{
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 1000;    /* Max number of messages in queue */
    attr.mq_msgsize = 2048; /* Max message size in bytes */
    attr.mq_curmsgs = 0;

    // Open or create the queue
    mqd_t mq = mq_open("/backdoor_queue", O_CREAT | O_RDWR, 0644, &attr);

    int n;
    printf("Ingrese el numero de ventanas a crear: ");
    scanf("%d", &n);

    for (int i = 0; i < n; i++)
    {
        pid_t pid = fork();

        if (pid < 0)
        {
            perror("Error con el fork");
            exit(1);
        }
        else if (pid == 0)
        {
            createWindow(mq);
            exit(0);
        }
    }

    for (int i = 0; i < n; i++)
    {
        wait(NULL);
    }


    // Parte final, ya al cerrar, liberamos recursos de la msg queue
    // Clean up
    mq_close(mq);
    mq_unlink("/backdoor_queue");
}
