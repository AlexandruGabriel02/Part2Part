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
#include <algorithm>
#include <sys/stat.h>
#include <openssl/md5.h>
#include <fcntl.h>

#define MAX_SIZE 4096
#define MAX_CONNECTION_QUEUE 5

#define CHECK_EXIT(value, msg) { if ((value) < 0) {perror(msg); exit(-1);} }
#define CHECK_WARN(value, msg) { if ((value) < 0) {printf("Warning: "); printf(msg); printf("\n"); fflush(stdout);} }
#define CHECK_CONTINUE(value, msg) { if ((value) < 0) {printf("Warning: "); printf(msg); printf("\n"); fflush(stdout); continue;} }

#define INFINITE_LOOP while(1)

sockaddr_in indexServer;
int port;
char hostname[4096];
std::string downLocation = ".";
std::vector<std::string> publishedFiles; 
pthread_mutex_t mutex;

enum fileType
{
    FILE_TXT,
    FILE_AUDIO,
    FILE_VIDEO,
    FILE_GAME,
    FILE_SOFTWARE,
    FILE_OTHER,
    FILE_UNKNOWN
};

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

struct publishedFile
{
    char name[MAX_SIZE];
    double size;
    //fileType type;
    char hash[2 * MD5_DIGEST_LENGTH];
};


namespace Utils
{
    const char* validCmd[] = {"search", "download", "publish", "unpublish", "disconnect", "downlocation"};
    const int cmdCount = 6;

    void writeToServer(int socket, char buff[])
    {
        int msgLength = strlen(buff);
        write(socket, &msgLength, sizeof(msgLength));
        write(socket, buff, msgLength);
    }

    template <class T>
    void writeToServer(int socket, const T& data)
    {
        int dataSize = sizeof(data);
        write(socket, &dataSize, sizeof(dataSize));
        write(socket, &data, dataSize);
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

    bool isDirectory(const std::string& location)
    {
        if (opendir(location.c_str()))
            return true;
        return false;
    }

    bool fileExists(const std::string& filePath)
    {
        if (access(filePath.c_str(), F_OK) == 0 && !isDirectory(filePath))
            return true;
        return false;
    }

    void setFileHash(char hash[], const std::string& filePath)
    {
        char command[MAX_SIZE];
        sprintf(command, "md5sum %s | cut -d\" \" -f1 > temp.txt", filePath.c_str());

        pthread_mutex_lock(&mutex);

        system(command);

        int fd = open("temp.txt", O_RDONLY);
        int size = 0;
        char ch;

        while (read(fd, &ch, 1))
        {
            if (ch == '\n')
                break;
            hash[size++] = ch;
        }
        hash[size] = '\0';

        close(fd);

        pthread_mutex_unlock(&mutex);
    }

    void setNameFromPath(char name[], const std::string& filePath)
    {
        int size = 0;
        for (int i = filePath.size() - 1; i >= 0 && filePath[i] != '/'; i--)
            name[size++] = filePath[i];
        name[size] = '\0';

        for (int i = 0, j = size - 1; i < j; i++, j--)
            std::swap(name[i], name[j]);
    }

    double getFileSize(const std::string& filePath)
    {
        //pentru fisiere > 4GB -> stat64
        struct stat st;
        stat(filePath.c_str(), &st);
        return st.st_size / 1024. / 1024. ;
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

    void sendInitialData()
    {
        Utils::writeToServer(indexSocket, hostname);
        Utils::writeToServer(indexSocket, port);
    }

    void executeDisconnect(char command[], int socket)
    {
        Utils::writeToServer(socket, CMD_DISCONNECT);
        close(socket);
        exit(0);
    }

    void executeUnpublish(char command[], int socket)
    {
        std::stringstream ss(command);
        std::string dummy, filePath;

        ss >> dummy >> filePath;

        auto it = find(publishedFiles.begin(), publishedFiles.end(), filePath);
        if (it == publishedFiles.end())
        {
            printf("File is not published in order to unpublish it\n");
            fflush(stdout);
            return;
        }

        publishedFiles.erase(it);
        
        publishedFile file;
        Utils::setNameFromPath(file.name, filePath);
        file.size = Utils::getFileSize(filePath);

        Utils::writeToServer(socket, CMD_UNPUBLISH);
        Utils::writeToServer(socket, file);
        //Utils::raspuns de la server

        printf("File unpublished\n");
        fflush(stdout);
    }

    void executePublish(char command[], int socket)
    {
        std::stringstream ss(command);
        std::string dummy, filePath;

        ss >> dummy >> filePath;

        if (!Utils::fileExists(filePath))
        {
            printf("Invalid file path\n");
            fflush(stdout);
            return;
        }

        auto it = find(publishedFiles.begin(), publishedFiles.end(), filePath);
        if (it != publishedFiles.end())
        {
            printf("File already published\n");
            fflush(stdout);
            return;
        }

        publishedFiles.push_back(filePath);

        publishedFile file;
        Utils::setNameFromPath(file.name, filePath);
        file.size = Utils::getFileSize(filePath);
        Utils::setFileHash(file.hash, filePath);

        Utils::writeToServer(socket, CMD_PUBLISH);
        Utils::writeToServer(socket, file);
        //Utils::raspuns de la server

        printf("File published\n");
        fflush(stdout);
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

        if (!Utils::isDirectory(location))
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
            case CMD_PUBLISH:
                executePublish(command, socket);
                break;
            case CMD_UNPUBLISH:
                executeUnpublish(command, socket);
                break;
            default:
                break;
        }
    }

    void* run(void* arg)
    {
        pthread_detach(pthread_self());

        sendInitialData();

        INFINITE_LOOP
        {
            char buff[MAX_SIZE];
            printf(">: ");
            fflush(stdout);

            CHECK_CONTINUE(Utils::readInput(buff), "Max input size exceeded");

            cmdType type;
            type = Utils::validateCommand(buff);
            if (type == CMD_UNKNOWN)
            {
                printf("Unknown command!\n");
                fflush(stdout);
                continue;
            }

            executeCommand(buff, indexSocket, type);
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