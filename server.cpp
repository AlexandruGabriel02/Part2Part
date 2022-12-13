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
#include <strings.h>
#include <netinet/in.h>

#define PORT 2908
#define MAX_SIZE 4096
#define MAX_CONNECTION_QUEUE 5

#define CHECK_RET(value, msg) { if ((value) < 0) {perror(msg); return;} }
#define CHECK_EXIT(value, msg) { if ((value) < 0) {perror(msg); exit(-1);} }
#define CHECK_WARN(value, msg) { if ((value) < 0) {printf("Warning: "); printf(msg); fflush(stdout);} }
#define CHECK_CONTINUE(value, msg) { if ((value) < 0) {printf("Warning: "); printf(msg); fflush(stdout); continue;} }

#define INFINITE_LOOP while(1)

struct threadArgs
{
    pthread_t threadId;
    sockaddr_in client;
    int clientSocket;
};

void initServer(sockaddr_in& server, int& serverSocket)
{
    CHECK_EXIT(serverSocket = socket(AF_INET, SOCK_STREAM, 0), "server socket");
    int on = 1;
    CHECK_WARN(setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)), "setsockopt");

    bzero(&server, sizeof(server));
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(PORT);
    server.sin_family = AF_INET;
    
    CHECK_EXIT(bind(serverSocket, (sockaddr*) &server, sizeof(server)), "bind");
    CHECK_EXIT(listen(serverSocket, MAX_CONNECTION_QUEUE), "listen");
}

void killThread(const char* message)
{
    printf("%s\n", message);
    pthread_exit(NULL);
}

void readCommand(int socket, char buff[])
{
    int msgLength, bytes;
    bytes = read(socket, &msgLength, sizeof(msgLength));
    if (bytes <= 0)
        killThread("Read error. Peer probably closed unexpectedly\n");

    bytes = read(socket, buff, msgLength);
    if (bytes <= 0)
        killThread("Read error. Peer probably closed unexpectedly\n");

    buff[msgLength] = '\0';
}

void* runThread(void* arg)
{
    pthread_detach(pthread_self());
    threadArgs t = *( (threadArgs*)arg );
    bool connected = true;

    while (connected)
    {
        char buff[MAX_SIZE];
        readCommand(t.clientSocket, buff);

        printf("Am primit: %s\n", buff);
        if (strcmp(buff, "disconnect") == 0)
            connected = false;
    }

    printf("Inchid thread-ul\n");
    close(t.clientSocket);
    return NULL;
}

int main(int argc, char* argv[])
{
    sockaddr_in server;
    int serverSocket;

    initServer(server, serverSocket);    

    INFINITE_LOOP 
    {   
        int clientSocket;
        sockaddr_in client;
        socklen_t clientSize;

        bzero(&client, sizeof(client));
        CHECK_CONTINUE(clientSocket = accept(serverSocket, (sockaddr*) &client, &clientSize), "accept error");
        printf("Client conectat cu socketul %d\n", clientSocket);

        threadArgs thread;
        thread.clientSocket = clientSocket;
        thread.client = client;
        pthread_create(&thread.threadId, NULL, &runThread, &thread);
    }

    return 0;
}