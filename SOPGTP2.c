#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SerialManager.h"

#include <unistd.h>
#include <signal.h>
#include <pthread.h>

#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/msg.h>

#define BAUDRATE 115200
#define SERIALPORT 1
#define ERRNOCNT -1  //Señal de error de funciones.
#define SLPDELAY 500000 //Delay para el poll del puerto serie, 0,5s

/*====VARIABLES GLOBALES====*/
/*THREADS*/
pthread_t thread1;

/*SOCKET*/
socklen_t addr_len;
struct sockaddr_in clientaddr;
struct sockaddr_in serveraddr;

int newfd = ERRNOCNT; //Valor inicial -1 para forzar la primer conexion.
int sckt;

/*VARIABLE DE CONTROL*/
_Bool salida = 0;

/*MUTEX*/
pthread_mutex_t mutexData = PTHREAD_MUTEX_INITIALIZER;

/*====PROTOTIPOS====*/
void bloquearSign(void);
void desbloquearSign(void);


/*====APLICACION===========*/

/* Handler de interrupcion*/
void signalHandler(int sig)
{
    /*Finalizacion del progrmama al llegar SIGINT o SIGTERM*/
    /*Setear flag de control*/
    
    salida = 1;

}

/*
Thread para enviar datos desde el cliente hacia la EDUCIAA.
*/
void *trdClientToCIAA(void *arg)
{
    char readBufClt[64];
    unsigned int nBytesReadClt = 0; //Inicializo la variable.

    while (1)
    {
        
        /* Acepta nuevas conexiones*/
        addr_len = sizeof(struct sockaddr_in);

        /*Limito el acceso a newfd en este thread*/
        pthread_mutex_lock (&mutexData);
        newfd = accept(sckt, (struct sockaddr *)&clientaddr, &addr_len);
        if (newfd == -1)
        {
            perror("error en accept");
            exit(EXIT_FAILURE);
        }
        pthread_mutex_unlock (&mutexData);

        char ipClient[32];
        inet_ntop(AF_INET, &(clientaddr.sin_addr), ipClient, sizeof(ipClient));
        printf("server:  conexion desde:  %s\n", ipClient);

        /*Mientras la conexion esta establecida recibe datos.
        Si se desconecta vuelve al accept() arriba.*/
        while (nBytesReadClt != ERRNOCNT || nBytesReadClt != 0)
        {

            /* Lee mensaje del cliente */
            if ((nBytesReadClt = read(newfd, readBufClt, sizeof(readBufClt))) == -1)
            {
                perror("Error leyendo mensaje en socket");
                exit(EXIT_FAILURE);
            }

            readBufClt[nBytesReadClt] = 0x00;

            /*Envia mensaje a EDUCIAA por UART*/
            // printf("nBytesReadClt %d readBufClt:%s\n", nBytesReadClt, readBufClt);
            serial_send(readBufClt, nBytesReadClt);
        }
        
        nBytesReadClt = 0;  // Reincio variable luego de un error de lectura.
    }

    return NULL;
}

int main()
{
    /*PUERTO SERIE*/
    unsigned char readBufCIAA[64];
    unsigned int nBytesReadCIAA = 0;
    char mode[] = {'8', 'N', '1', 0};

    /*====Configuraciones e inicializaciones===*/
    /*CONFIGURACION DEL MANEJO DE SIGNALS*/
    struct sigaction sa; // Estructura para la configuración de interrupcion.

    sa.sa_handler = signalHandler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);

    if ((sigaction(SIGINT, &sa, NULL) == -1) || (sigaction(SIGTERM, &sa, NULL) == -1))
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    /*SOCKET*/
    /*Se crea el socket*/
    sckt = socket(AF_INET, SOCK_STREAM, 0);

    if (sckt == -1)
    {
        printf("Socket error\n");
        exit(EXIT_FAILURE);
    }

    /*Cargamos datos de IP:PORT del server*/
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(10000);

    if (inet_pton(AF_INET, "127.0.0.1", &(serveraddr.sin_addr)) <= 0)
    {
        fprintf(stderr, "ERROR IP de server invalido\r\n");
        exit(EXIT_FAILURE);
    }

    /*Se asociar socket al programa*/
    if (bind(sckt, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1)
    {
        close(sckt);
        perror("Erro al abrir el puerto.");
        exit(EXIT_FAILURE);
    }

    /*Setea el socket en modo listening, atiende la conexion del socket*/
    if (listen(sckt, 10) == -1) // backlog=10
    {
        perror("Error en listen");
        exit(EXIT_FAILURE);
    }

    /*PUERTO SERIE*/
    /* Apertura del puerto serie*/
    if (serial_open(SERIALPORT, BAUDRATE))
    {
        printf("No se pudo abrir el puerto serie.");
    }

    /*Bloquear señales para trdClientToCIAA*/
    bloquearSign();

    pthread_create(&thread1, NULL, trdClientToCIAA, NULL);

    /*Desbloquear señales para trdClientToCIAA*/
    desbloquearSign();

    printf("Listo para recibir señales.");

    /*Recepcion de datos por puerto serie*/
    while (1 && !salida)    // Cuando salida = !1 sale del while y pasa al cierre ordenado del programa.
    {
        /*Polling del puerto serie*/
        nBytesReadCIAA = serial_receive(readBufCIAA, sizeof(readBufCIAA));

        /*Recibo dato y imprimo/envio*/
        if (nBytesReadCIAA != 0)
        {
           
            /*Mientras no haya conexion establecida no se ejecuta el write*/
            if(newfd != ERRNOCNT){

            /*Lo envia la trama por el socker*/
            write(newfd, readBufCIAA, sizeof(readBufCIAA));

            }

        }
  
        usleep(SLPDELAY);
    }

    /*Cerrar puerto serie*/
    serial_close();

    /*Cierro el socket TCP*/
    close(newfd);

    //Matar el thread.
    pthread_cancel(thread1);
    pthread_join(thread1,NULL);

    /*Terminar el programar*/
    exit(EXIT_SUCCESS);

    return 0;
}

/*====FUNCIONES AUXILIARES====*/
/*
    FUNCION PARA EL BLOQUEO DE SIGNALS
*/
void bloquearSign(void)
{
    sigset_t set;
    int s;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
}

/*
    FUNCION PARA EL DESBLOQUEO DE SIGNALS
*/
void desbloquearSign(void)
{
    sigset_t set;
    int s;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);
}