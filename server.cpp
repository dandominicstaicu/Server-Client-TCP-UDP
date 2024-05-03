/*
 * Protocoale de comunicatii
 * Laborator 7 - TCP
 * Echo Server
 * server.c
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <queue>
#include <string>
#include <unordered_map>
#include <cmath>
#include <algorithm>

#include "common.h"
#include "helpers.h"

#define MAX_CONNECTIONS    32
#define MAX_BUF_SIZE 1600

using namespace std;

/*
    Check the status of the TCP client
    0 - first time connectiong
    1 - reconnecting
    2 - client already connected
*/
enum States {
    FIRST_TIME,
    RECONNECT,
    CONNECTED
};

char buf[MAX_BUF_SIZE];

// Mentinem un vector cu toti clientii TCP care sunt sau
// au fost conectati la server.
struct tcp_client tcp_clients[MAX_CONNECTIONS];
int num_tcp_clients = 0;

unordered_map<string, queue<string>> packets_queue;

/* Create the poll of descriptors */
struct pollfd poll_fds[MAX_CONNECTIONS];
int num_clients = 3;

bool match_topic(const char *subscription, const char *topic) {
    while (*subscription != '\0' && *topic != '\0') {
        if (*subscription == '*') {
            if (*(subscription + 1) == '\0') // '*' at the end matches all remaining characters
                return true;
            subscription++;
            if (*topic == '\0') // If topic ends but subscription still has '*', no match
                return false;
            while (*topic != '\0') { // Try all possible matches
                if (match_topic(subscription, topic))
                    return true;
                topic++;
            }
            return false; // No match found
        } else if (*subscription == '+') {
            if (*(subscription + 1) != '/' &&
                *(subscription + 1) != '\0') // Ensure '+' is followed by '/' or end of string
                return false;
            const char *next_slash = strchr(topic, '/');
            if (next_slash != nullptr) { // '+' matches until next '/'
                topic = next_slash;
            } else { // If no '/' found, match till end if '+' is at end of subscription
                if (*(subscription + 1) == '\0')
                    topic += strlen(topic); // Move to end of topic
                else
                    return false; // If '+' not at end and no '/', no match
            }
            subscription++;
        } else if (*subscription == *topic) {
            subscription++;
            topic++;
        } else {
            return false;
        }
    }
    // Check for exact end match
    return *subscription == '\0' && *topic == '\0';
}

/*
    Check if a topic is in the list of subscribers of a client:
    if it's not subscribed, return 0
    if it's subscribed without store-forward, return 1
    if it's subscribed with store-forward, return 2
*/
bool is_more_inclusive(const char *new_sub, const char *existing_sub) {
    // Simple check: if new_sub ends with '*' and existing_sub ends with '+'
    size_t new_len = strlen(new_sub);
    size_t existing_len = strlen(existing_sub);
    if (new_sub[new_len - 1] == '*' && existing_sub[existing_len - 1] == '+') {
        // Compare the base part of the subscription without the last character
        return strncmp(new_sub, existing_sub, new_len - 1) == 0;
    }
    return false;
}

bool update_subscriptions(vector<pair<char *, int>> &subscriptions, const char *topic, int sf) {
    // Check if a more inclusive subscription exists
    for (auto &sub: subscriptions) {
        if (is_more_inclusive(topic, sub.first)) {
            // Replace existing subscription with more inclusive one
            free(sub.first);  // Assume dynamic allocation
            sub.first = strdup(topic);
            sub.second = sf;
            return true;  // Return true as the subscription list was updated
        }
    }

    // Check if the exact same subscription already exists
    for (auto &sub: subscriptions) {
        if (strcmp(sub.first, topic) == 0 && sub.second == sf) {
            return false;  // Subscription already exists with the same settings
        }
    }

    // If no more inclusive or exact same subscription is found, add the new one
    subscriptions.push_back({strdup(topic), sf});
    return true;  // Return true as a new subscription was added
}


int check_subscribed(const vector<pair<char *, int>> &subscriptions, const char *topic) {
    for (auto &sub: subscriptions) {
        if (strcmp(sub.first, topic) == 0 || match_topic(sub.first, topic))
            return 1 + sub.second;
    }
    return 0;
}

void subscribe(int index, char *topic, int sf) {
    bool subscribed = false;

    // Find the client who sent the request (by file descriptor)
    for (int k = 0; k < num_tcp_clients; k++) {
        if (tcp_clients[k].fd == poll_fds[index].fd) {
            // Update subscriptions; this function now also checks for existing subscriptions
            subscribed = update_subscriptions(tcp_clients[k].subscriptions, topic, sf);

            if (subscribed) {
                // Send a confirmation message to the client
                memset(buf, 0, MAX_BUF_SIZE);
                sprintf(buf, "Subscribed to topic %s.\n", topic);
                int send_result = send_all(poll_fds[index].fd, &buf, MAX_BUF_SIZE);
                DIE(send_result < 0, "[SERV] send");
            } else {
                fprintf(stderr, "Already subscribed to topic %s\n", topic);
            }
            break;
        }
    }
}


/*
    Create a udp_packet from the read buffer from the UDP socket
*/
struct udp_packet create_recv_packet(char *buffer) {
    struct udp_packet received_packet{};

    /* Extract the topic */
    memcpy(received_packet.topic, buffer, 50);
    received_packet.topic[50] = '\0';

    /* Store data_type */
    received_packet.data_type = (unsigned int) (unsigned char) buffer[50];

    /* Parse payload based on the data_type */
    switch (received_packet.data_type) {
        case 0: // INT
        {
            int32_t raw_int;
            memcpy(&raw_int, buffer + 52, sizeof(int32_t));
            int message = ntohl(raw_int);

            if (buffer[51] == 1) {
                /* if sign byte is 1, then it's negative */
                message = -message;
            }

            snprintf(received_packet.payload, sizeof(received_packet.payload), "%d", message);
        }
            break;

        case 1: // SHORT_REAL
        {
            uint16_t raw_short;
            memcpy(&raw_short, buffer + 51, sizeof(uint16_t));
            double message = (double) ntohs(raw_short) / 100.0;

            snprintf(received_packet.payload, sizeof(received_packet.payload), "%.2f", message);
        }
            break;

        case 2: // FLOAT
        {
            int32_t raw_float;
            memcpy(&raw_float, buffer + 52, sizeof(int32_t));
            double value = (double) ntohl(raw_float);
            double power = pow(10, buffer[56]);

            if (buffer[51] == 1) {
                /* if sign byte is 1, then it's negative */
                value = -value;
            }

            double message = value / power;
            snprintf(received_packet.payload, sizeof(received_packet.payload), "%f", message);
        }
            break;

        case 3: // STRING
            strcpy(received_packet.payload, buffer + 51);
            break;

        default:
            fprintf(stderr, "Unrecognized data type\n");
            break;
    }

    return received_packet;
}

void tcp_first_connect(int newsockfd, char *id) {
    tcp_clients[num_tcp_clients].fd = newsockfd;
    tcp_clients[num_tcp_clients].connected = true;
    strcpy(tcp_clients[num_tcp_clients].id, id);
    num_tcp_clients++;
}

void tcp_reconnect(int newsockfd, int tcp_pos, const char *id) {
    // Updatam cu noul file descriptor si memoram ca s-a conectat
    tcp_clients[tcp_pos].connected = true;
    tcp_clients[tcp_pos].fd = newsockfd;

    // Trimitem din coada toate pachetele care s-au trimis in timp
    // ce clientul era deconectat, la care acesta era abonat si avea
    // store-forward activat.
    while (!packets_queue[id].empty()) {
        int ret = send_all(newsockfd, &packets_queue[id].front()[0], MAX_BUF_SIZE);
        DIE(ret < 0, "[SERV] send");
        packets_queue[id].pop();
    }
}

void unsubscribe(int index, char *topic) {
    bool unsubscribed = false;

    for (int k = 0; k < num_tcp_clients; k++) {
        if (tcp_clients[k].fd == poll_fds[index].fd) {
            auto& subscriptions = tcp_clients[k].subscriptions;
            auto it = std::find_if(subscriptions.begin(), subscriptions.end(),
                                   [topic](const pair<char*, int>& p) { return !strcmp(p.first, topic); });

            if (it != subscriptions.end()) {
                subscriptions.erase(it);
                unsubscribed = true;
                break;
            }
        }
    }

    if (unsubscribed) {
        memset(buf, 0, MAX_BUF_SIZE);
        sprintf(buf, "Unsubscribed from topic.\n");
        int ret = send_all(poll_fds[index].fd, &buf, MAX_BUF_SIZE);
        DIE(ret < 0, "[SERV] send");
    } else {
        fprintf(stderr, "Client is not subscribed to topic %s.\n", topic);
    }
}

void run_server(int udp_sockfd, int tcp_sockfd) {
    /* Listen for clients */
    int ret = listen(tcp_sockfd, MAX_CONNECTIONS);
    DIE(ret < 0, "[SERV] Error while listening");


    poll_fds[0].fd = tcp_sockfd;
    poll_fds[0].events = POLLIN;

    poll_fds[1].fd = udp_sockfd;
    poll_fds[1].events = POLLIN;

    poll_fds[2].fd = 0;
    poll_fds[2].events = POLLIN;


    int running = 1;

    while (running) {
        /* Poll the fds until can read from one of them */
        ret = poll(poll_fds, num_clients, -1);
        DIE(ret < 0, "poll");

        for (int i = 0; i < num_clients; i++) {
            if (poll_fds[i].revents & POLLIN) {
                if (poll_fds[i].fd == tcp_sockfd) {
                    /* New TCP connection */
                    struct sockaddr_in client_addr;
                    socklen_t client_addr_len = sizeof(client_addr);
                    int newsockfd = accept(tcp_sockfd, (struct sockaddr *) &client_addr, &client_addr_len);
                    DIE(newsockfd < 0, "[SERV] Error while accepting new TCP connection");

                    char id[11];
                    ret = recv(newsockfd, &id, sizeof(id), 0);
                    DIE(ret < 0, "[SERV] Error while receiving id");

                    /*
                        Check the status of the TCP client
                        0 - first time connectiong
                        1 - reconnecting
                        2 - client already connected
                    */
                    States status = FIRST_TIME;
                    int tcp_pos = -1;

                    for (int j = 0; j < num_tcp_clients; ++j) {
                        if (strcmp(tcp_clients[j].id, id) == 0) {
                            tcp_pos = j;
                            status = RECONNECT;
                            if (tcp_clients[j].connected)
                                status = CONNECTED;
                            break;
                        }
                    }

                    /* if is already connected */
                    if (status == CONNECTED) {
                        printf("Client %s already connected.\n", id);

                        // Trimitem cerere de inchidere catre client
                        memset(buf, 0, MAX_BUF_SIZE);
                        sprintf(buf, "exit");
                        ret = send_all(newsockfd, &buf, sizeof(buf));
                        DIE(ret < 0, "send");

                        // Inchidem socketul
                        close(newsockfd);
                        break;
                    }

                    /* Not already connected ->
                     * Add the new socket to the poll */
                    poll_fds[num_clients].fd = newsockfd;
                    poll_fds[num_clients].events = POLLIN;
                    num_clients++;

                    printf("New client %s connected from %s:%d.\n",
                           id, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

                    /* this is the first time connecting */
                    if (status == FIRST_TIME) {
                        tcp_first_connect(newsockfd, id);
                    } else { /* reconnecting */
                        tcp_reconnect(newsockfd, tcp_pos, id);
                    }

                    break;
                } else if (poll_fds[i].fd == udp_sockfd) {
                    struct sockaddr_in client_addr{};
                    socklen_t clen = sizeof(client_addr);
                    memset(buf, 0, MAX_BUF_SIZE);

                    // Receptionam pachetul trimis de clientul UDP
                    ret = recvfrom(udp_sockfd, &buf, MAX_BUF_SIZE, 0,
                                   (struct sockaddr *) &client_addr, &clen);
                    DIE(ret < 0, "[SERV] send");

                    // Convertim pachetul primit sub forma de char* intr-o structura de tipul udp_packet
                    struct udp_packet received_packet = create_recv_packet(buf);

                    // Trimitem pachetul clientilor TCP abonati la topicul primit
                    for (int k = 0; k < num_tcp_clients; k++) { // Verificam daca clientul TCP k este abonat la topic

                        int sub = check_subscribed(tcp_clients[k].subscriptions, received_packet.topic);

                        // TODO enum subscribed
                        if (sub > 0) {
                            // datatypes as strings
                            char const *data_type_string[4] = {"INT", "SHORT_REAL", "FLOAT", "STRING"};

                            // transformam informatiile primite stringul ce va fi trimis clientului TCP
                            memset(buf, 0, MAX_BUF_SIZE);
                            sprintf(buf, "%s:%d - %s - %s - %s\n", inet_ntoa(client_addr.sin_addr),
                                    (ntohs(client_addr.sin_port)), received_packet.topic,
                                    data_type_string[received_packet.data_type], received_packet.payload);

                            // Daca clientul e conectat, ii trimitem direct stringul
                            if (tcp_clients[k].connected) {
                                ret = send_all(tcp_clients[k].fd, &buf, sizeof(buf));
                                DIE(ret < 0, "send");
                            }
                                // Daca nu e conectat dar are sf = 1 pe topicul curent, stocam stringul
                                //  in coada de mesaje a clientului in caz ca se va reconecta mai tarziu
                            else if (sub == 2)
                                packets_queue[tcp_clients[k].id].push(buf);
                        }
                    }

                    break;
                } else if (poll_fds[i].fd == 0) { // Daca primim un mesaj de la stdin
                    // Citim mesajul
                    memset(buf, 0, MAX_BUF_SIZE);
                    fgets(buf, sizeof(buf), stdin);

                    // Daca mesajul curent este 'exit'
                    if (!strncmp(buf, "exit", 4)) {
                        // Parcurgem toti clientii TCP
                        for (int idx = 0; idx < num_tcp_clients; idx++) {
                            // Daca clientul e conectat, ii trimitem mesajul 'exit', ca sa se inchida
                            // si inchidem si noi socketul de conexiune intre el si server
                            if (tcp_clients[idx].connected) {
                                ret = send_all(tcp_clients[idx].fd, &buf, sizeof(buf));
                                DIE(ret < 0, "send");
                                close(tcp_clients[idx].fd);
                            }
                        }

                        // Revenim in main, care inchide socketii de listen si opreste serverul
                        return;
                    } else
                        fprintf(stderr, "Unrecognized command.\n");

                    break;
                } else { // Daca se primesc date de pe socketul unuia dintre clientii TCP conectati
                    // Receptionam mesajul ca string
                    memset(buf, 0, MAX_BUF_SIZE);
                    int rc = recv_all(poll_fds[i].fd, &buf, MAX_BUF_SIZE);
                    DIE(rc < 0, "recv");

                    // Clientul TCP a inchis conexiunea
                    if (rc == 0) {
                        // Determinam ID ul clientului TCP, caruia ii stim doar fd-ul socketului
                        char *id;
                        int client_num;
                        for (int k = 0; k < num_tcp_clients; k++) {
                            if (tcp_clients[k].fd == poll_fds[i].fd) {
                                id = tcp_clients[k].id;
                                client_num = k;
                                break;
                            }
                        }

                        printf("Client %s disconnected.\n", id);

                        // Inchidem socketul corespunzator
                        close(poll_fds[i].fd);

                        // Scoatem socketul din vectorul de poll
                        for (int j = i; j < num_clients - 1; j++)
                            poll_fds[j] = poll_fds[j + 1];
                        num_clients--;

                        // Marcam clientul TCP in vectorul de clienti ca fiind deconectat
                        tcp_clients[client_num].connected = 0;
                        tcp_clients[client_num].fd = -1;
                    } else {// Clientul este inca conectat
                        // Parsam stringul primit si extragem cele 3 componente:
                        // - subscribe/unsubscribe
                        // - topicul
                        // - store-forward = 0/1
                        char sub[15], topic[50];
                        memset(sub, 0, 15);
                        memset(topic, 0, 50);
                        int sf;
                        sscanf(buf, "%s %s %d", sub, topic, &sf);

                        // If a subscribe request is received
                        if (!strcmp(sub, "subscribe")) {
                            subscribe(i, topic, sf);
                        }
                            // Daca am primit o cerere de unsubscribe
                        else if (!strcmp(sub, "unsubscribe")) {
                            unsubscribe(i, topic);
                        } else {
                            fprintf(stderr, "Unrecognized command.\n");
                        }
                    }

                    break;
                }
            }
        }
    }
}


int main(int argc, char *argv[]) {
    uint16_t port;
    int udp_sockfd, tcp_sockfd;
    int flag = 1;
    struct sockaddr_in server_addr;


    /* Deactivate buffering for stdout */
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    /* Check the number of arguments */
    DIE(argc != 2, "[SERV] Usage: ./server <PORT_SERVER>");

    /* Get the server port */
    int ret = sscanf(argv[1], "%hu", &port);
    DIE(ret != 1, "[SERV] Given port is invalid");

    /* Create UDP socket */
    udp_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    DIE(udp_sockfd < 0, "[SERV] Error while creating UDP socket");

    /* Create TCP socket */
    tcp_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(tcp_sockfd < 0, "[SERV] Error while creating TCP socket");

    /* Disable the Nagle algorithm */
    ret = setsockopt(tcp_sockfd, IPPROTO_TCP, SO_REUSEADDR | TCP_NODELAY, &flag, sizeof(int));
    DIE (ret < 0, "[SERV] Error while disabling the Nagle algorithm");

    ret = setsockopt(udp_sockfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int));
    DIE(ret < 0, "[SERV ]setsockopt(SO_REUSEADDR) udp failed");

    /* Set port and IP that we'll be listening for */
    memset(&server_addr, 0, sizeof(server_addr));
    ret = inet_pton(AF_INET, SERVER_LOCALHOST_IP, &server_addr.sin_addr.s_addr);
    DIE(ret <= 0, "inet_pton");
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    /* Bind to the set port and IP */
    ret = bind(udp_sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr));
    DIE(ret < 0, "[SERV] Couldn't bind to the port");

    ret = bind(tcp_sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr));
    DIE(ret < 0, "[SERV] Couldn't bind to the port");

    run_server(udp_sockfd, tcp_sockfd);

    return 0;
}
