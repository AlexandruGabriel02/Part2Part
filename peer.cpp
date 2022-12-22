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
#include <sstream>
#include <filesystem>
#include <dirent.h>

#define MAX_SIZE 4096
#define MAX_CONNECTION_QUEUE 5

#define CHECK_EXIT(value, msg) { if ((value) < 0) {perror(msg); exit(-1);} }
#define CHECK_WARN(value, msg) { if ((value) < 0) {printf("Warning: "); printf(msg); printf("\n"); fflush(stdout);} }
#define CHECK_CONTINUE(value, msg) { if ((value) < 0) {printf("Warning: "); printf(msg); printf("\n"); fflush(stdout); continue;} }

#define INFINITE_LOOP while(1)

sockaddr_in indexServer;
int port;
std::string hostname;
std::string downLocation = ".";

namespace Utils
{
    enum cmdType
    {
        CMD_SEARCH,
        CMD_DOWNLOAD,
        CMD_PUBLISH,
        CMD_UNPUBLISH,
        CMD_DISCONNECT,
        CMD_DOWNLOCATION,
        CMD_UNKNOWN
    };
    const char* validCmd[] = {"search", "download", "publish", "unpublish", "disconnect", "downlocation"};
    const int cmdCount = 6;

    void writeToServer(int socket, char buff[])
    {
        int msgLength = strlen(buff);
        write(socket, &msgLength, sizeof(msgLength));
        write(socket, buff, msgLength);
    }

    cmdType validateCommand(char command[])
    {
        int index;
        //extrag primul cuvant (numele de comanda)
        std::stringstream ss(command);
        std::string cmdName;
        ss >> cmdName;

        for (index = 0; index < cmdCount; index++)
            if (cmdName == validCmd[index])
                return (cmdType) index;
        
        return (cmdType) index;
    }

    void executeDisconnect(char command[], int socket)
    {
        writeToServer(socket, command);
        close(socket);
        exit(0);
    }

    bool isDirectory(const std::string& location)
    {
        if (opendir(location.c_str()))
            return true;
        return false;
    }

    void executeDownLocation(char command[])
    {
        std::string location, dummy;
        std::stringstream ss(command);
        ss >> dummy >> location;
        dummy.clear();
        ss >> dummy;

        if (dummy.size() > 0)
        {
            printf("Too many arguments for this command\n");
            fflush(stdout);
            return;
        }

        if (!isDirectory(location))
        {
            printf("Invalid / non existent directory!\n");
            fflush(stdout);
            return;
        }

        downLocation = location;
        printf("New download folder set successfully!\n");
        fflush(stdout);
    }

    void executeCommand(char command[], int socket, cmdType type)
    {
        switch(type)
        {
            case CMD_DISCONNECT:
                executeDisconnect(command, socket);
                break;
            case CMD_DOWNLOCATION:
                executeDownLocation(command);
                break;
            default:
                break;
        }
    }

    int readInput(char command[])
    {
        char ch;
        int size = 0;
        bool size_exceeded = false;
        while (read(STDIN_FILENO, &ch, 1))
        {
            if (ch == '\n')
                break;

            if (size >= MAX_SIZE)
                size_exceeded = true;
            else
                command[size++] = ch;
        }
        if (size_exceeded)
            return -1;

        command[size] = '\0';

        return size;
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

            CHECK_CONTINUE(Utils::readInput(buff), "Max input size exceeded");

            Utils::cmdType type;
            type = Utils::validateCommand(buff);
            if (type == Utils::CMD_UNKNOWN)
            {
                printf("Unknown command!\n");
                fflush(stdout);
                continue;
            }

            Utils::executeCommand(buff, indexSocket, type);
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