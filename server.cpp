#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <queue>
#include "helpers.h"
using namespace std;

void usage(char *file) {
	fprintf(stderr, "Usage: %s server_port\n", file);
	exit(0);
}

void close_server(int tcp, int udp, vector<client>& clients) {
    close(tcp);
    close(udp);

    for (unsigned int i = 0; i < clients.size(); i++) {
        close(clients[i].socket);
    }
}

int handshake(int sockfd, vector<client>& clients, char* client_id, client** client_addr) {
    int ret;
    char buffer[BUFLEN];
    memset(buffer, 0, BUFLEN);

    ret = recv(sockfd, buffer, BUFLEN, 0);
    DIE(ret < 0, "handshake recv");
    
    if (strncmp(buffer, "new", 3)) { // nu incepe cu new cum trebuie
        memset(buffer, 0, BUFLEN);
        sprintf(buffer, "Wrong handshake format.");

        ret = send(sockfd, buffer, strlen(buffer), 0);
	    DIE(ret < 0, "handshake reply");
        return -1;
    }

    // copiem client_id-ul (adica buffer fara partea cu "new")
    memset(client_id, 0, MAX_ID_LEN);
    strcpy(client_id, buffer + 4);

    for (unsigned int i = 0; i < clients.size(); i++) {
        if (!strcmp((clients[i]).id, client_id)) {
            if (VERBOSE)
                cout << "Client already exists\n";

            if ((clients[i]).disc) { // daca avem clientul in baza de date
                *client_addr = &(clients[i]);
                (clients[i]).disc = false;
                (clients[i]).socket = sockfd;
                return 1;
            }

            // nu avem voie sa avem id duplicat
            memset(buffer, 0, BUFLEN);
            strcpy(buffer, "Duplicate ID. Please reconnect with another ID.\n");

            ret = send(sockfd, buffer, strlen(buffer), 0);
	        DIE(ret < 0, "handshake error");

            return -2;
        }
    }

    memset(buffer, 0, BUFLEN);
    sprintf(buffer, "OK.");

    ret = send(sockfd, buffer, strlen(buffer), 0);
	DIE(ret < 0, "handshake reply");

    return 0;
}
// verifica daca clientul conectat pe socket-ul client_socket e subscribed la 
// topicul t
bool is_subscribed(int client_socket, topic t) {
    for (unsigned int i = 0; i < t.clients.size(); i++) {
        if (t.clients[i]->socket == client_socket) {
            return true;
        }
    }

    return false;
}

void subscribe(int socket, char* topic_name, bool sf, vector<topic>& topics, 
               vector<client>& clients) {
    client* subscriber;
    bool found = false;
    char buffer[100];

    for (unsigned int i = 0; i < clients.size(); i++) {
        if ((clients[i]).socket == socket) {
            subscriber = &(clients[i]);
        }
    }

    for (unsigned int i = 0; i < topics.size(); i++) {
        if (!strcmp((topics[i]).name, topic_name)) {
            found = true;
            if (!is_subscribed(socket, topics[i])) {
                (topics[i]).clients.push_back(subscriber);
                if (sf) {
                    (topics[i]).clients_sf.push_back(1);
                } else {
                    (topics[i]).clients_sf.push_back(0);
                }
            }
        }
    }
    // daca nu exista, il cream
    if (!found) {
        topic t;
        memcpy(t.name, topic_name, strlen(topic_name));
        t.clients.push_back(subscriber);
        if (sf) {
            t.clients_sf.push_back(1);
        } else {
            t.clients_sf.push_back(0);
        }
        topics.push_back(t);
    }

    sprintf(buffer, SUB_OK, topic_name);
    send(socket, buffer, strlen(buffer), 0);
}

void unsubscribe(int socket, char* topic_name, vector<topic>& topics) {
    char buffer[100];
    // cautam topicul bun si clientul si il stergem
    for (unsigned int i = 0; i < topics.size(); i++) {
        if (!strcmp((topics[i]).name, topic_name)) {
            for (unsigned int j = 0; j < (topics[i]).clients.size(); j++) {
                if ((topics[i]).clients[j]->socket == socket) {
                    auto client = (topics[i]).clients.begin() + j;
                    auto sf = (topics[i]).clients_sf.begin() + j;
                    (topics[i]).clients.erase(client);
                    (topics[i]).clients_sf.erase(sf);
                }
            }
        }
    }

    sprintf(buffer, UNSUB_OK, topic_name);
    send(socket, buffer, strlen(buffer), 0);
}

msg extract_msg(char* buffer, struct sockaddr_in info, int len) {
    msg message;

    memcpy(&(message.ip), &(info.sin_addr), sizeof(struct in_addr));
    message.port = ntohs(info.sin_port);
    memcpy(message.topic, buffer, 50);
    memcpy(message.data, buffer + 51, len - 51);
    message.data[len - 51] = '\0';
    message.data[1500] = '\0';
    message.data_size = len - 51 + MSG_HDR;
    message.data_type = buffer[50];

    return message;
}

void forward_stored(client& c, vector<client>& clients) {
    for (unsigned int i = 0; i < clients.size(); i++) {
        if (!strcmp((clients[i]).id, c.id)) {
            for (unsigned int j = 0; j < (clients[i]).msg_for_sf.size(); j++) {
                int ret = send((clients[i]).socket, 
                               &((clients[i]).msg_for_sf[j]), 
                               (clients[i]).msg_for_sf[j].data_size, 0);
                DIE(ret < 0, "S&F send");
            }
            (clients[i]).msg_for_sf.clear();
        }
    }
}

int main(int argc, char *argv[]){
	int listen_tcp, portnr, listen_udp;
	char buffer[BUFLEN];
	struct sockaddr_in serv_addr;
    struct sockaddr_in info;
	int ret;
    int yes = 1;
    unsigned int addrlen = sizeof(struct sockaddr_in);

    // vector de clienti
    vector<client> clients;
    vector<topic> topics;

	fd_set read_fds;	// multimea de citire folosita in select()
	fd_set tmp_fds;		// multime folosita temporar
	int fdmax;			// valoare maxima fd din multimea read_fds

	if (argc < 2) {
		usage(argv[0]);
	}

	// se goleste multimea de descriptori de citire (read_fds) 
    // si multimea temporara (tmp_fds)
	FD_ZERO(&read_fds);
	FD_ZERO(&tmp_fds);

	listen_tcp = socket(PF_INET, SOCK_STREAM, 0);
	DIE(listen_tcp < 0, "socket tcp");

    listen_udp = socket(PF_INET, SOCK_DGRAM, 0);
	DIE(listen_udp < 0, "socket udp");

	portnr = atoi(argv[1]);
	DIE(portnr == 0, "atoi portnr");

	memset((char *) &serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(portnr);
	serv_addr.sin_addr.s_addr = INADDR_ANY;

	ret = bind(listen_tcp, (struct sockaddr *) &serv_addr, 
               sizeof(struct sockaddr));
	DIE(ret < 0, "bind tcp");

    memset((char *) &serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(portnr);
	serv_addr.sin_addr.s_addr = INADDR_ANY;

	ret = bind(listen_udp, (struct sockaddr *) &serv_addr, 
               sizeof(struct sockaddr));
	DIE(ret < 0, "bind udp");

	ret = listen(listen_tcp, MAX_CLIENTS);
	DIE(ret < 0, "listen");

	// se adauga noul file descriptor (socketul pe care se asculta conexiuni) 
    // in multimea read_fds
	FD_SET(listen_tcp, &read_fds);
    FD_SET(listen_udp, &read_fds);
    FD_SET(0, &read_fds);

    // dezactivam neagle
    ret = setsockopt(listen_tcp, IPPROTO_TCP, TCP_NODELAY, 
            (char*) &yes, sizeof(int));
    DIE(ret < 0, "tcp nodelay");

	fdmax = listen_tcp;
    if (listen_udp > fdmax) {
        fdmax = listen_udp;
    }

	while (1) {
		tmp_fds = read_fds; 
		
		ret = select(fdmax + 1, &tmp_fds, NULL, NULL, NULL);
		DIE(ret < 0, "select");

		for (int sockid = 0; sockid <= fdmax; sockid++) {
			if (FD_ISSET(sockid, &tmp_fds)) {
				if (sockid == listen_tcp) {
                    char client_id[11];
                    if (VERBOSE)
					    cout << "Client nou tcp.\n";
                    
                    // acceptam o conexiune noua
                    int client_fd = accept(listen_tcp,
                                    (struct sockaddr *) &info, &addrlen);
                    
                    ret = setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY,
                                (char*) &yes, sizeof(int));
                    DIE(ret < 0, "tcp nodelay");

                    FD_SET(client_fd, &read_fds);
                    if (client_fd > fdmax) {
                        fdmax = client_fd;
                    }

                    client c;
                    client* addr;
                    ret = handshake(client_fd, clients, client_id, &addr);

                    if (ret < 0) {
                        close(client_fd);
                        FD_CLR(client_fd, &read_fds);
                        continue;
                    }

                    if (ret == 0) {
                        cout << "New client " << client_id << " connected from " << 
                                inet_ntoa(info.sin_addr) << ":" << 
                                ntohs(info.sin_port) << "\n";
                        c.socket = client_fd;
                        strcpy(c.id, client_id);
                        c.disc = false;
                        c.sf = false;
                        clients.push_back(c);
                    }

                    if (ret == 1) {
                        cout << "Old client " << client_id << " connected from " << 
                                inet_ntoa(info.sin_addr) << ":" << 
                                ntohs(info.sin_port) << "\n";
                        forward_stored(*addr, clients);
                    }

                    continue;
                }

                if (sockid == listen_udp) {
                    if (VERBOSE)
                        cout << "Am primit de la udp:\n";

                    memset(buffer, 0, BUFLEN);
                    ret = recvfrom(listen_udp, buffer, BUFLEN, 0, 
                                 ((struct sockaddr*)&info), &addrlen);

                    char topic_name[51];
                    memcpy(topic_name, buffer, 50);
                    topic_name[50] = '\0';

                    
                    for (unsigned int i = 0; i < topics.size(); i++) {
                        if (!strncmp((topics[i]).name, 
                                      topic_name, 
                                      strlen(topic_name))) {

                            for (unsigned int j = 0;
                                 j < (topics[i]).clients.size(); j++) {

                                msg message = extract_msg(buffer, info, ret);
                                msg* backup = (msg*) calloc(1, sizeof(msg));
                                memcpy(backup, &message, sizeof(message));

                                if ((topics[i]).clients[j]->disc && 
                                   ((topics[i]).clients_sf[j] == 1)) {

                                    (topics[i]).clients[j]->
                                        msg_for_sf.push_back(message);
                                } else if (!(topics[i]).clients[j]->disc) {
                                    ret = send((topics[i]).clients[j]->socket,
                                                backup, ret + 16, 0);
                                    //free(backup);
                                    DIE(ret < 0, "send client online");
                                }

                                free(backup);
                            }
                        }
                    }

                    continue;
                }

                if (sockid == 0) {
                    if (VERBOSE)
                        cout << "stdin:\n";
                    memset(buffer, 0, BUFLEN);
		            fgets(buffer, BUFLEN - 1, stdin);

                    if (!strncmp(buffer, "exit", 4)) {
                        close_server(listen_tcp, listen_udp, clients);
                        return 0;
                    } else {
                        if (VERBOSE) {
                            cout << "Unknown command: %s" << buffer;
                        }
                    }
                } else {
                    if (VERBOSE)
					    cout << "Am primit de la client tcp:\n";

                    memset(buffer, 0, BUFLEN);
                    ret = recv(sockid, buffer, BUFLEN, 0);
                    DIE(ret < 0, "tcp recv");

                    if (ret == 0) { // connection closed
                        if (VERBOSE)
                            cout << "Connection closed.\n";

                        for (unsigned int i = 0; i < clients.size(); i++) {
                            if ((clients[i]).socket == sockid) {
                                cout << "Client " << (clients[i]).id << " disconnected.\n";
                                (clients[i]).disc = true;
                            }
                        }

                        FD_CLR(sockid, &read_fds);
                    } else {
                        char* command = strtok(buffer, " ");
                        char* topic_name = strtok(NULL, " ");

                        if (VERBOSE)
                            cout << "command: " << command << "\n";

                        char* sf_string = strtok(NULL, " ");
                        bool sf = 0;
                        if (sf_string != NULL) {
                            sf = atoi(sf_string);
                        } else {
                            topic_name[strlen(topic_name) - 1] = '\0';
                        }

                        if (!strncmp(buffer, SUB, strlen(SUB))) {
                            subscribe(sockid, topic_name, sf, topics, clients);
                        }

                        if (!strncmp(buffer, UNSUB, strlen(UNSUB))) {
                            unsubscribe(sockid, topic_name, topics);
                        }
                    }
				}
			}
		}
	}
    
	return 0;
}
