#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SerialManager.h"

#include <unistd.h>
#include <pthread.h>

#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define BAUDRATE 115200
#define SERIALPORT 1

/*====VARIABLES GLOBALES====*/
/*SOCKET*/
socklen_t addr_len;
struct sockaddr_in clientaddr;
struct sockaddr_in serveraddr;

int newfd;
int sckt;

/*
Thread para enviar datos desde la EDUCIAA hacia el cliente.

Lee del puerto serie las tramas de datos enviados por la EDUCIAA. Luego lo pasa al socket para que
este lo envie al cliente mediante protocolo TCP.
*/
void *trdCIAAToClient(void *arg)
{
    /*PUERTO SERIE*/
    unsigned char readBufCIAA[64];
    unsigned int nBytesReadCIAA = 0;
    char mode[] = {'8', 'N', '1', 0};

    while (1)
    {
        /*Polling del puerto serie*/
        nBytesReadCIAA = serial_receive(readBufCIAA, sizeof(readBufCIAA));

        /*Recibo dato y imprimo/envio*/
        if (nBytesReadCIAA != 0)
        {
            /*Imprimo el dato recibido*/
            printf("nBytesReadCIAA: %d, recibi: %s \n", nBytesReadCIAA, readBufCIAA);

            /*Lo envio por el socket*/
            write(newfd, readBufCIAA, sizeof(readBufCIAA)); // enviar por socket
            printf("write: %s", readBufCIAA);
        }
        sleep(0.5);
    }

    return NULL;
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
        /* Acepta nuevas conexiones*/
        addr_len = sizeof(struct sockaddr_in);
        if ((newfd = accept(sckt, (struct sockaddr *)&clientaddr, &addr_len)) == -1)
        {
            perror("error en accept");
            exit(1);
        }

        /* Lee mensaje del cliente */
        if ((nBytesReadClt = read(newfd, readBufClt, sizeof(readBufClt))) == -1)
        {
            perror("Error leyendo mensaje en socket");
            exit(1);
        }

        readBufClt[nBytesReadClt] = 0x00;
        printf("nBytesReadClt %d readBufClt:%s\n", nBytesReadClt, readBufClt);

        // Cerramos conexion con cliente
        close(newfd);
    }

    return NULL;
}

int main()
{
    /*====Variables====*/
    /*THREADS*/
    pthread_t thing1, thinig2;

    /*====Configuraciones e inicializaciones===*/

    /*SOCKET*/
    /*Se crea el socket*/
    int sckt = socket(AF_INET, SOCK_STREAM, 0);

    /*Cargamos datos de IP:PORT del server*/
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(10000);

    if (inet_pton(AF_INET, "127.0.0.1", &(serveraddr.sin_addr)) <= 0)
    {
        fprintf(stderr, "ERROR IP de server invalido\r\n");
        return 1;
    }

    /*Se abre el puerto*/
    if (bind(sckt, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1)
    {
        close(sckt);
        perror("Erro al abrir el puerto.");
        return 1;
    }

    /*Seteas socket en modo Listening*/
    if (listen(sckt, 10) == -1) // backlog=10
    {
        perror("Error en listen");
        exit(1);
    }

    // Ejecutamos accept() para recibir conexiones entrantes
    addr_len = sizeof(struct sockaddr_in);
    if ((newfd = accept(sckt, (struct sockaddr *)&clientaddr, &addr_len)) == -1)
    {
        perror("error en accept");
        exit(1);
    }

    char ipClient[32];
    inet_ntop(AF_INET, &(clientaddr.sin_addr), ipClient, sizeof(ipClient));
    printf("server:  conexion desde:  %s\n", ipClient);

    /*PUERTO SERIE*/
    /* Apertura del puerto serie*/
    if (serial_open(SERIALPORT, BAUDRATE))
    {
        printf("No se pudo abrir el puerto serie.");
    }


    /*Por aca debo elegir un thread que pueda manejar las interrupciones
    bloquear
    crear/lanzar thread
    desbloquear*/
    
    pthread_create(&thing1, NULL, trdCIAAToClient, NULL);
    //pthread_create (&thing1, NULL, trdClientToCIAA, NULL);

    pthread_join(thing1, NULL);
    //pthread_join (thing2, NULL);

    return 0;

    //pthread_create (&thread2, NULL, threadSocket, NULL);
}