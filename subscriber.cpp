/*
 * Protocoale de comunicatii
 * Laborator 7 - TCP si mulplixare
 * client.c
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "helpers.h"

#define MAX_BUF_SIZE 1600

// Functionalitatea principala a unui client TCP,
// care asteapta pachete de la stdin sau de la server
void run_client(int sockfd)
{   
    int rc;
    char buf[MAX_BUF_SIZE];

    /* 
        Cream multimea de file descriptori monitorizati:
        stdin si socketul prin care clientul este
        conectat cu serverul. Tipul de evenimente
        asteptate este POLLIN deoarece clientul asteapta
        sa aiba disponibile date ce pot fi citite.
    */
    struct pollfd poll_fds[2];
    int num_clients = 2;

    poll_fds[0].fd = 0;
    poll_fds[0].events = POLLIN;

    poll_fds[1].fd = sockfd;
    poll_fds[1].events = POLLIN;

    while (1)
    {   
        // Facem poll pe file descriptori pana cand se poate
        // citi informatie de pe unul dintre ei
        rc = poll(poll_fds, num_clients, -1);
        DIE(rc < 0, "poll");

        for (int i = 0; i < num_clients; i++)
        {   
            if (poll_fds[i].revents && POLLIN)
            {   
                // Daca putem citi de la stdin
                if (poll_fds[i].fd == 0)
                {
                    memset(buf, 0, MAX_BUF_SIZE);
                    fgets(buf, sizeof(buf), stdin);

                    // Daca mesajul primit este 'exit', revenim
                    // in functia main, care inchide socketul
                    // de conexiune cu serverul 
                    if (!strncmp(buf, "exit", 4))
                        return;
                    
                    // Daca primim alt mesaj ('subscribe'/'unsubscribe'),
                    // trimitem mai departe serverului, care se ocupa de
                    // parsarea si indeplinirea cererii de (un)subscribe
                    send_all(sockfd, &buf, MAX_BUF_SIZE);
                }
                // Daca putem citi de pe socket
                else
                {
                    memset(buf, 0, MAX_BUF_SIZE);
                    int rc = recv_all(sockfd, &buf, MAX_BUF_SIZE);
                    if (rc <= 0)
                        break;

                    // Daca serverul ne-a trimis comanda de inchidere 'exit',
                    // revenim in functia main, care inchide socketul
                    // de conexiune cu serverul
                    if (!strncmp(buf, "exit", 4)) 
                       return;

                    // Printam mesajul trimis de server la stdout
                    printf("%s", buf);
                }
            }
        }
    }
}


int main(int argc, char *argv[]) {
	// Dezactivam buffering-ul la afisare
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    // Verificam argumentele primite de executabil
    if (argc != 4)
    {
        printf("\n Usage: %s <id> <ip> <port>\n", argv[0]);
        return 1;
    }

    // Parsam portul primit
    uint16_t port;
    int rc = sscanf(argv[3], "%hu", &port);
    DIE(rc != 1, "Given port is invalid");

    // Obtinem un socket TCP pentru conectarea la server
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(sockfd < 0, "socket");

    // Completăm serv_addr cu adresa serverului pe care am primit-o,
    // familia de adrese (IPv4) si portul pentru conectare
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));

    rc = inet_pton(AF_INET, argv[2], &serv_addr.sin_addr.s_addr);
    DIE(rc <= 0, "inet_pton");
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    // Ne conectăm la server
    rc = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    DIE(rc < 0, "connect");

    // Trimitem ID-ul clientului TCP catre server
    // imediat dupa conectare
    // send_all(sockfd, argv[1], sizeof(argv[1]));
    send_all(sockfd, argv[1], strlen(argv[1]) + 1);

    run_client(sockfd);

    // Inchidem conexiunea si socketul creat
    close(sockfd);

	return 0;
}



