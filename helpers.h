#ifndef _HELPERS_H
#define _HELPERS_H 1

#include <stdio.h>
#include <stdlib.h>
#include <vector>

using namespace std;

/*
 * Macro de verificare a erorilor
 * Exemplu:
 * 		int fd = open (file_name , O_RDONLY);
 * 		DIE( fd == -1, "open failed");
 */

#define DIE(assertion, call_description)                                       \
  do {                                                                         \
    if (assertion) {                                                           \
      fprintf(stderr, "(%s, %d): ", __FILE__, __LINE__);                       \
      perror(call_description);                                                \
      exit(EXIT_FAILURE);                                                      \
    }                                                                          \
  } while (0)


#define SERVER_LOCALHOST_IP "127.0.0.1"

/*
  The data structure that keeps a TCP client that was connected
  at the server and the data necesary about it
*/
struct tcp_client {
  char id[11];
  int fd;
  vector<pair<char*, int>> subscriptions;
  int connected;
};

/*
  The packet that UDP clients send to the server.
  It includes topic, data_type and the payload of data.
*/
struct udp_packet {
  char topic[51];
  unsigned int data_type;
  char payload[1501];
};

#endif
