#ifndef _HELPERS_H
#define _HELPERS_H 1

#include <stdio.h>
#include <stdlib.h>
#include <queue>
#include <string>
#include <netinet/in.h>

/*
 * Macro de verificare a erorilor
 * Exemplu:
 *     int fd = open(file_name, O_RDONLY);
 *     DIE(fd == -1, "open failed");
 */

#define DIE(assertion, call_description)	\
	do {									\
		if (assertion) {					\
			fprintf(stderr, "(%s, %d): ",	\
					__FILE__, __LINE__);	\
			perror(call_description);		\
			exit(EXIT_FAILURE);				\
		}									\
	} while(0)

#define BUFLEN		sizeof(msg) // dimensiunea maxima a calupului de date
#define MAX_CLIENTS	10	        // numarul maxim de clienti in asteptare
#define VERBOSE     0
#define SUB         "subscribe"
#define UNSUB       "unsubscribe"
#define MSG_HDR     67
#define SUB_OK      "subscribed topic: %s"
#define UNSUB_OK    "unsubscribed topic: %s"
#define MAX_ID_LEN  11


typedef struct {
    struct in_addr ip; // ip-ul si port-ul udp-ului
    uint32_t port;
    uint32_t data_size;
    uint32_t data_type;
    char topic[51];
    char data[1501];
} msg;

typedef struct {
    char id[11];
    int socket;
    bool sf;
    bool disc; // daca e offline sau nu
    std::vector<msg> msg_for_sf; // il folosesc ca pe un queue, adaug cu
                                 // push_back si ca sa le trimit, iau de la 0
                                 // pana la ultimu
} client;

typedef struct {
    char name[50];
    std::vector<client*> clients;
    std::vector<int> clients_sf;
} topic;

#endif
