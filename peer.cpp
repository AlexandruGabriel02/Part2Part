#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <vector>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/un.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_SIZE 4096
#define MAX_CONNECTION_QUEUE 5

#define CHECK_EXIT(value, msg) { if ((value) < 0) {perror(msg); exit(-1);} }
#define CHECK_WARN(value, msg) { if ((value) < 0) {printf("Warning: "); printf(msg); fflush(stdout);} }
#define CHECK_CONTINUE(value, msg) { if ((value) < 0) {printf("Warning: "); printf(msg); fflush(stdout); continue;} }

#define INFINITE_LOOP while(1)

sockaddr_in indexServer;
int port;
std::string hostname;

namespace Utils
{
    void writeCommand(int socket, char buff[])
    {
        int msgLength = strlen(buff);
        write(socket, &msgLength, sizeof(msgLength));
        write(socket, buff, msgLength);
    }
};

namespace Client
{
    int indexSocket;

    void initConnection()
    {
        CHECK_EXIT(indexSocket = socket(AF_INET, SOCK_STREAM, 0), "socket");

        CHECK_EXIT(connect(indexSocket, (sockaddr*) &indexServer, sizeof(indexServer)), "connection error");

        printf("conectat cu socketul %d\n", indexSocket);
    }

    void* run(void* arg)
    {
        pthread_detach(pthread_self());

        INFINITE_LOOP
        {
            char buff[MAX_SIZE];
            printf(">: ");
            fflush(stdout);
            std::cin >> buff;

            Utils::writeCommand(indexSocket, buff);
            if (strcmp(buff, "disconnect") == 0)
            {
                close(indexSocket);
                exit(0);
            }
        }

        return NULL;
    }
};

namespace Server
{
    void initServer(int& serverSocket, sockaddr_in& server)
    {
        CHECK_EXIT(serverSocket = socket(AF_INET, SOCK_STREAM, 0), "server socket");
        int on = 1;
        CHECK_WARN(setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)), "setsockopt");

        bzero(&server, sizeof(server));
        server.sin_addr.s_addr = htonl(INADDR_ANY);
        server.sin_port = htons(port);
        server.sin_family = AF_INET;
        
        CHECK_EXIT(bind(serverSocket, (sockaddr*) &server, sizeof(server)), "bind");
        CHECK_EXIT(listen(serverSocket, MAX_CONNECTION_QUEUE), "listen");
    }

    void* run(void* arg)
    {
        pthread_detach(pthread_self());

        return NULL;
    }
};

void initPeer(char* argv[])
{
    printf("Choose a port to use: ");
    fflush(stdout);
    std::cin >> port;

    printf("Choose a nickname: ");
    std::cin >> hostname; 

    indexServer.sin_addr.s_addr = inet_addr(argv[1]);
    indexServer.sin_family = AF_INET;
    indexServer.sin_port = htons(atoi(argv[2]));
}

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        printf("Syntax: %s <ip_server_central> <port>\n", argv[0]);
        exit(-1);
    }

    /* initializare */
    initPeer(argv);

    /* execut partea de "client" din peer */
    Client::initConnection(); //conectarea la serverul central
    pthread_t client_thread;
    pthread_create(&client_thread, NULL, &Client::run, NULL);

    /* execut in paralel "server-ul" peer-ului */
    int serverSocket;
    sockaddr_in server;
    Server::initServer(serverSocket, server);

    INFINITE_LOOP
    {
        int clientSocket;
        sockaddr_in client;
        socklen_t clientSize;

        bzero(&client, sizeof(client));
        CHECK_CONTINUE(clientSocket = accept(serverSocket, (sockaddr*) &client, &clientSize), "accept error");

        pthread_t thread;
        pthread_create(&thread, NULL, &Server::run, NULL);
    }

    return 0;
}