#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <cmath>
#include "helpers.h"
using namespace std;

void usage(char *file) {
	fprintf(stderr, "Usage: %s <ID_Client> <IP_Server> <Port_Server>\n", file);
	exit(0);
}

void print_message(msg m) {
    cout << inet_ntoa(m.ip) << ":" << m.port << " - " << m.topic << " - ";

    if (m.data_type == 0) {
        int32_t number;
        memcpy(&number, m.data + 1, sizeof(int32_t));
        number = ntohl(number);

        if (m.data[0] == 1) {
            number *= -1;
        }

        cout << "INT - " << number << "\n";
    }

    if (m.data_type == 1) {
        uint16_t number;
        memcpy(&number, m.data, sizeof(uint16_t));
        number = ntohs(number);
        cout << "SHORT_REAL - " << (double)number/100 << "\n";
    }

    if (m.data_type == 2) {
        int32_t modulo;
        int8_t power;
        memcpy(&modulo, m.data + 1, sizeof(uint32_t));
        modulo = ntohl(modulo);
        memcpy(&power, m.data + 1 + sizeof(uint32_t), sizeof(uint8_t));

        if (m.data[0] == 1) {
            modulo *= -1;
        }

        printf("FLOAT - %f\n", modulo * (pow(10, -power)));
    }

    if (m.data_type == 3) {
        cout << "STRING - " << m.data << "\n";
    }
}

int handshake(int sockfd, char* id) {
    int ret;
    char buffer[BUFLEN];
    memset(buffer, 0, strlen(buffer));

    strcpy(buffer, "new ");
    strcat(buffer, id);

    ret = send(sockfd, buffer, strlen(buffer), 0);
	DIE(ret < 0, "handshake send");

    ret = recv(sockfd, buffer, BUFLEN, 0);
    DIE(ret < 0, "handshake recv");

    if (!strncmp(buffer, "OK.", 3)) {
        if (VERBOSE) {
            cout << "Connection to server accepted.\n";
        }

        return 0;
    }

    if (VERBOSE) {
        cout << "Connection refused.\n";
    }

    return -1;
}

int main(int argc, char *argv[]) {
	int sockfd, n, ret;
	struct sockaddr_in serv_addr;
	char buffer[BUFLEN];
    int yes = 1;
    int printed_size;

    fd_set read_fds;	// multimea de citire folosita in select()
	fd_set tmp_fds;		// multime folosita temporar
	int fdmax;			// valoare maxima fd din multimea read_fds

    FD_ZERO(&read_fds);
	FD_ZERO(&tmp_fds);

	if (argc < 4) {
		usage(argv[0]);
	}

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	DIE(sockfd < 0, "socket");

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(atoi(argv[3]));
	ret = inet_aton(argv[2], &serv_addr.sin_addr);
	DIE(ret == 0, "inet_aton");
    
    ret = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char*) &yes, sizeof(int));
    DIE(ret < 0, "tcp nodelay");

	ret = connect(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr));
	DIE(ret < 0, "connect");

    ret = handshake(sockfd, argv[1]);
    DIE(ret < 0, "handshake");

    FD_SET(sockfd, &read_fds);
    FD_SET(0, &read_fds);
	fdmax = sockfd;

	while (1) {
        tmp_fds = read_fds; 
		
		ret = select(fdmax + 1, &tmp_fds, NULL, NULL, NULL);
		DIE(ret < 0, "select");

		if (FD_ISSET(0, &tmp_fds)) { // stdin
            memset(buffer, 0, BUFLEN);
		    fgets(buffer, BUFLEN - 1, stdin);

            if (strncmp(buffer, "exit", 4) == 0) {
			    break;
		    }

            if (strncmp(buffer, "subscribe", 9) && strncmp(buffer, "unsubscribe", 11)) {
                continue;
            }

            n = send(sockfd, buffer, strlen(buffer), 0);
		    DIE(n < 0, "send");

            ret = recv(sockfd, buffer, sizeof(buffer), 0);

            if (!strncmp(buffer, SUB_OK, 12) || !strncmp(buffer, UNSUB_OK, 12)) {
                cout << buffer << "\n";
                continue;
            }
        }

        if (FD_ISSET(sockfd, &tmp_fds)) { // serveru
            memset(buffer, 0, BUFLEN);

            ret = recv(sockfd, buffer, BUFLEN - 1, 0);

            if (ret <= 16) { // stiu sigur ca primesc cel putin 16 octeti,
                            // doar de la int-urile din structura msg
                continue;
            }

            printed_size = 0;

            while (printed_size < ret) {
                msg new_m;
                memset(&new_m, 0, sizeof(new_m));
                new_m = *(msg*)(buffer + printed_size);
                print_message(new_m);
                printed_size += new_m.data_size;
            }
        }
	}

	close(sockfd);

	return 0;
}
