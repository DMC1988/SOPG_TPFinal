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
#define SLPDELAY 0.5 //Delay para el poll del puerto serie.

/*====VARIABLES GLOBALES====*/
/*====Variables====*/
/*THREADS*/
pthread_t thread1;

/*SOCKET*/
socklen_t addr_len;
struct sockaddr_in clientaddr;
struct sockaddr_in serveraddr;

int newfd = ERRNOCNT; //Valor inicial -1 para forzar la primer conexion.
int sckt;

/*====PROTOTIPOS====*/
void bloquearSign(void);
void desbloquearSign(void);

/*====APLICACION===========*/

/* Handler de interrupcion*/
void signalHandler(int sig)
{

    /*No importa que interrupción llege se debe
   finalizar el programa correctamente*/

    /*Cerrar puerto serie*/
    serial_close();

    /*Cierro el socket TCP*/
    close(newfd);

    //Matar el thread.
    pthread_cancel(thread1);

    /*Terminar el programar*/
    exit(1);
}

/*
Thread para enviar datos desde el cliente hacia la EDUCIAA.
*/
void *trdClientToCIAA(void *arg)
{
    char readBufClt[64];
    unsigned int nBytesReadClt;

    while (1)
    {
        nBytesReadClt = 0; //Inicializo la variable.

        /* Acepta nuevas conexiones*/
        addr_len = sizeof(struct sockaddr_in);

        newfd = accept(sckt, (struct sockaddr *)&clientaddr, &addr_len);
        if (newfd == -1)
        {
            perror("error en accept");
            exit(1);
        }

        char ipClient[32];
        inet_ntop(AF_INET, &(clientaddr.sin_addr), ipClient, sizeof(ipClient));
        printf("server:  conexion desde:  %s\n", ipClient);

        /*Mientras la conexion esta establecida recibe datos.
            Si se desconecta vuelve al accept() arriba.*/
        while (nBytesReadClt != ERRNOCNT)
        {

            /* Lee mensaje del cliente */
            if ((nBytesReadClt = read(newfd, readBufClt, sizeof(readBufClt))) == -1)
            {
                perror("Error leyendo mensaje en socket");
                exit(1);
            }

            readBufClt[nBytesReadClt] = 0x00;

            /*Envia mensaje a EDUCIAA por UART*/
            // printf("nBytesReadClt %d readBufClt:%s\n", nBytesReadClt, readBufClt);
            serial_send(readBufClt, nBytesReadClt);
        }
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
        exit(1);
    }

    /*SOCKET*/
    /*TODO: Revisar si esto va en el thread main o en *trdClientToCIAA*/
    /*Se crea el socket*/
    sckt = socket(AF_INET, SOCK_STREAM, 0);

    if (sckt == -1)
    {
        printf("Socket error\n");
        exit(1);
    }

    /*Cargamos datos de IP:PORT del server*/
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(10000);

    if (inet_pton(AF_INET, "127.0.0.1", &(serveraddr.sin_addr)) <= 0)
    {
        fprintf(stderr, "ERROR IP de server invalido\r\n");
        exit(1);
    }

    /*Se asociar socket al programa*/
    if (bind(sckt, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1)
    {
        close(sckt);
        perror("Erro al abrir el puerto.");
        exit(1);
    }

    /*Setea el socket en modo listening, atiende la conexion del socket*/
    if (listen(sckt, 10) == -1) // backlog=10
    {
        perror("Error en listen");
        exit(1);
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
    while (1)
    {
        /*Polling del puerto serie*/
        nBytesReadCIAA = serial_receive(readBufCIAA, sizeof(readBufCIAA));

        /*Recibo dato y imprimo/envio*/
        if (nBytesReadCIAA != 0)
        {
            /*Imprimo el dato recibido*/
            //printf("nBytesReadCIAA: %d, recibi: %s \n", nBytesReadCIAA, readBufCIAA);

            /*Lo envio por el socket*/
            write(newfd, readBufCIAA, sizeof(readBufCIAA)); // enviar por socket
            //TODO: Manejar errores de write. Si le escribo al puerto cerrado me devuelve un signal y debo ver como lo manejo.
            printf("write: %s", readBufCIAA);
        }
        sleep(SLPDELAY);
    }

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
    //sigaddset(&set, SIGUSR1);
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
    //sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);
}