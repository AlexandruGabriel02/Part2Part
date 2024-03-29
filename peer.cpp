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
#define MD5_HASH_SIZE 33

#define CHECK_RET(value, msg) { if ((value) < 0) {perror(msg); return;} }
#define CHECK_EXIT(value, msg) { if ((value) < 0) {perror(msg); exit(-1);} }
#define CHECK_WARN(value, msg) { if ((value) < 0) {printf("Warning: "); printf(msg); printf("\n"); fflush(stdout);} }
#define CHECK_CONTINUE(value, msg) { if ((value) < 0) {printf("Warning: "); printf(msg); printf("\n"); fflush(stdout); continue;} }
#define print_err(msg) {printf("[ERROR]: %s\n", msg); fflush(stdout);}

#define INFINITE_LOOP while(1)

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

enum responseType
{
    RESPONSE_ERR = -1,
    RESPONSE_OK
};

struct publishedFile
{
    char name[MAX_SIZE];
    double size;
    fileType type;
    char hash[MD5_HASH_SIZE];
};

struct pathInfo
{
    std::string path;
    publishedFile file;
};

struct argsInfo
{
    char name[MAX_SIZE];
    double maxSize;
    double minSize;
    fileType type;
    char hash[MD5_HASH_SIZE];
};

struct peerInfo
{
    unsigned int ip;
    int port;
};

sockaddr_in indexServer;
int port;
char hostname[4096];
std::string downLocation = ".";
std::vector<pathInfo> publishedFiles; 
pthread_mutex_t mutex;

class argParser
{
private:
    //toate argumentele posibile
    double maxSize;
    double minSize;
    fileType type;
    char hash[MD5_HASH_SIZE];
    //int depth;

    //tipul de comanda
    cmdType cmd;

    fileType strToType(const std::string& str)
    {
        const char* types[] = {"text", "audio", "video", "game", "software", "other"};
        int retType;
        for (retType = 0; retType < 6 && str != types[retType]; retType++);

        return (fileType) retType;
    }

    int parseTypeArg(std::stringstream& ss)
    {
        if (type != FILE_UNKNOWN)
        {
            print_err("Same argument used multiple times (-type)");
            return -1;
        }

        std::string strType;
        ss >> strType;
        type = strToType(strType);
        if (type == FILE_UNKNOWN)
        {
            print_err("Invalid -type argument");
            return -1;
        }

        return 0;
    }

    int parseMinsizeArg(std::stringstream& ss)
    {
        if (cmd != CMD_SEARCH && cmd != CMD_DOWNLOAD)
        {
            print_err("-minsize argument not suitable for this command");
            return -1;
        }
        if (minSize != -1)
        {
            print_err("Same argument used multiple times (-minsize)");
            return -1;
        }

        ss >> minSize;
        if (minSize < 0)
        {
            print_err("Invalid double value for -minsize argument");
            return -1;
        }

        return 0;
    }

    int parseMaxsizeArg(std::stringstream& ss)
    {
        if (cmd != CMD_SEARCH && cmd != CMD_DOWNLOAD)
        {
            print_err("-maxsize argument not suitable for this command");
            return -1;
        }
        if (maxSize != -1)
        {
            print_err("Same argument used multiple times (-maxsize)");
            return -1;
        }

        ss >> maxSize;
        if (maxSize < 0)
        {
            print_err("Invalid double value for maxsize argument");
            return -1;
        }

        return 0;
    }

    int parseHashArg(std::stringstream& ss)
    {
        if (cmd != CMD_DOWNLOAD)
        {
            print_err("-hash argument not suitable for this command");
            return -1;
        }
        if (strlen(hash) > 0)
        {
            print_err("Same argument used multiple times (-hash)");
            return -1;
        }

        ss >> hash;

        return 0;
    }
public:
    argParser(cmdType p_cmd)
    {
        cmd = p_cmd;
        maxSize = minSize = -1;
        //depth = 0;
        type = FILE_UNKNOWN;
        strcpy(hash, "");
    }

    int parse(std::string arg)
    {        
        for (int i = 0; i < (int)arg.size(); i++)
        {
            if (arg[i] == ':')
            {
                arg[i] = ' ';
                break;
            }
        }
        
        std::string argName;
        std::stringstream ss(arg);
        ss >> argName;

        if (argName[0] != '-')
        {
            print_err("Argument error. Use \'-\' to begin an argument");
            return -1;
        }
        
        if (argName == "-type")
            return parseTypeArg(ss);
        else if (argName == "-minsize")
            return parseMinsizeArg(ss);
        else if (argName == "-maxsize")
            return parseMaxsizeArg(ss);
        else if (argName == "-hash")
            return parseHashArg(ss);
        else 
        {
            //to do (maybe): add recursive arg 
            print_err("Unrecognized argument");
            return -1;
        }

        return 0;
    }

    double getMinSize()
    {
        return minSize;
    }

    double getMaxSize()
    {
        return maxSize;
    }

    fileType getFileType()
    {
        return type;
    }

    void getHash(char p_hash[])
    {
        strcpy(p_hash, hash);
    }
    
};

namespace Utils
{
    const char* validCmd[] = {"search", "download", "publish", "unpublish", "disconnect", "downlocation"};
    const int cmdCount = 6;

    void writeToServer(int socket, char buff[])
    {
        int msgLength = strlen(buff);
        CHECK_EXIT(write(socket, &msgLength, sizeof(msgLength)), "Can't write to server\n");
        CHECK_EXIT(write(socket, buff, msgLength), "Can't write to server\n");
    }

    template <class T>
    void writeToServer(int socket, const T& data)
    {
        int dataSize = sizeof(data);
        CHECK_EXIT(write(socket, &dataSize, sizeof(dataSize)), "Can't write to server\n");
        CHECK_EXIT(write(socket, &data, dataSize), "Can't write to server\n");
    }

    void readFromServer(int socket, char buff[])
    {
        int msgLength;
        CHECK_EXIT(read(socket, &msgLength, sizeof(msgLength)), "Can't read from server!\n");
        CHECK_EXIT(read(socket, buff, msgLength), "Can't read from server!\n");
        buff[msgLength] = '\0';
    }

    template<class T>
    void readFromServer(int socket, T& data)
    {
        int dataSize;
        CHECK_EXIT(read(socket, &dataSize, sizeof(dataSize)), "Can't read from server!\n");
        CHECK_EXIT(read(socket, &data, dataSize), "Can't read from server!\n");
    }

    void readResponse(int socket, responseType& response)
    {
        read(socket, &response, sizeof(response));
    }

    void receiveFile(int socket, char fileName[])
    {
        std::string path = downLocation + "/" + fileName;
        int file_fd;
        char buff[MAX_SIZE];
        int chunkSize = MAX_SIZE;

        CHECK_RET(file_fd = open(path.c_str(), O_WRONLY | O_CREAT, S_IRWXU), "Can't open for file transfer");

        while (chunkSize == MAX_SIZE)
        {
            chunkSize = read(socket, buff, MAX_SIZE);
            write(file_fd, buff, chunkSize);
        }

        close(file_fd);
    }

    void sendFile(int socket, const std::string& path)
    {
        int file_fd;
        char buff[MAX_SIZE];
        int chunkSize = MAX_SIZE;

        CHECK_RET(file_fd = open(path.c_str(), O_RDONLY), "Can't open for file transfer");

        while (chunkSize == MAX_SIZE)
        {
            chunkSize = read(file_fd, buff, MAX_SIZE);
            write(socket, buff, chunkSize);
        }

        close(file_fd);
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

        std::vector<pathInfo>::iterator it;
        for (it = publishedFiles.begin(); it != publishedFiles.end() && it->path != filePath; it++)
        {
            //empty
        }

        if (it == publishedFiles.end())
        {
            print_err("File is not published in order to unpublish it");
            return;
        }
        
        Utils::writeToServer(socket, CMD_UNPUBLISH);
        Utils::writeToServer(socket, it->file);

        responseType response;
        Utils::readResponse(socket, response);

        if (response == RESPONSE_ERR)
        {
            print_err("Server error for the unpublish request");
            return;
        }

        pthread_mutex_lock(&mutex);
        publishedFiles.erase(it);
        pthread_mutex_unlock(&mutex);

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
            print_err("Invalid file path");
            return;
        }

        std::vector<pathInfo>::iterator it;
        for (it = publishedFiles.begin(); it != publishedFiles.end() && it->path != filePath; it++)
        {
            //empty
        }
        if (it != publishedFiles.end())
        {
            print_err("File already published");
            return;
        }

        std::string arg;
        argParser p(CMD_PUBLISH);
        while (ss >> arg)
        {
            if(p.parse(arg) == -1)
                return;
        }

        publishedFile file;
        Utils::setNameFromPath(file.name, filePath);
        file.size = Utils::getFileSize(filePath);
        file.type = p.getFileType();
        Utils::setFileHash(file.hash, filePath);

        Utils::writeToServer(socket, CMD_PUBLISH);
        Utils::writeToServer(socket, file);
        
        responseType response;
        Utils::readResponse(socket, response);
        if (response == RESPONSE_ERR)
        {
            print_err("Server error for the publish request");
            return;
        }

        pthread_mutex_lock(&mutex);
        publishedFiles.push_back({filePath, file});
        pthread_mutex_unlock(&mutex);

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
            print_err("Too many arguments for this command");
            return;
        }

        if (!Utils::isDirectory(location))
        {
            print_err("Invalid / non existent directory!");
            return;
        }

        downLocation = location;
        printf("New download folder set successfully!\n");
        fflush(stdout);
    }

    void executeSearch(char command[], int socket)
    {
        argsInfo searchInfo;
        std::string dummy;
        std::stringstream ss(command);

        ss >> dummy >> searchInfo.name;

        std::string arg;
        argParser p(CMD_SEARCH);
        while (ss >> arg)
        {
            if (p.parse(arg) == -1)
                return;
        }

        searchInfo.type = p.getFileType();
        searchInfo.minSize = p.getMinSize();
        searchInfo.maxSize = p.getMaxSize();

        Utils::writeToServer(socket, CMD_SEARCH);
        Utils::writeToServer(socket, searchInfo);

        responseType response;
        Utils::readResponse(socket, response);
        if (response == RESPONSE_ERR)
        {
            print_err("Server error for the search request");
            return;
        }

        char searchResponse[2 * MAX_SIZE];
        Utils::readFromServer(socket, searchResponse);

        printf("Search results:\nUsername | File | Type | Size(MB) | Hash\n%s", searchResponse);
        fflush(stdout);
    }

    void p2pConnection(const peerInfo& peer, char fileName[], char hash[])
    {
        int peerSocket;
        sockaddr_in serverPeer;

        serverPeer.sin_addr.s_addr = peer.ip;
        serverPeer.sin_family = AF_INET;
        serverPeer.sin_port = htons(peer.port);

        CHECK_RET(peerSocket = socket(AF_INET, SOCK_STREAM, 0), "Can't create socket for p2p connection\n");
        CHECK_RET(connect(peerSocket, (sockaddr*)&serverPeer, sizeof(serverPeer)), "Can't connect to peer for file transfer\n");
        printf("Connected!\n");
        fflush(stdout);

        //verific daca exista fisierul
        Utils::writeToServer(peerSocket, hash);

        responseType response;
        Utils::readFromServer(peerSocket, response);
        if (response == RESPONSE_ERR)
        {
            print_err("Error finding searched file. Try downloading again");
            return;
        }

        printf("Starting file transfer...\n");
        fflush(stdout);

        Utils::receiveFile(peerSocket, fileName);

        printf("Done!\n");
        fflush(stdout);

    }

    void executeDownload(char command[], int socket)
    {
        argsInfo searchInfo;
        std::string dummy;
        std::stringstream ss(command);

        ss >> dummy >> searchInfo.name;

        std::string arg;
        argParser p(CMD_DOWNLOAD);
        while (ss >> arg)
        {
            if (p.parse(arg) == -1)
                return;
        }

        searchInfo.type = p.getFileType();
        searchInfo.minSize = p.getMinSize();
        searchInfo.maxSize = p.getMaxSize();
        p.getHash(searchInfo.hash);

        Utils::writeToServer(socket, CMD_DOWNLOAD);
        Utils::writeToServer(socket, searchInfo);

        responseType response;
        Utils::readResponse(socket, response);
        if (response == RESPONSE_ERR)
        {
            print_err("Server error for the download request");
            return;
        }

        peerInfo peer;
        char hash[MD5_HASH_SIZE];
        char fileName[MAX_SIZE];

        Utils::readFromServer(socket, peer);
        Utils::readFromServer(socket, hash);
        Utils::readFromServer(socket, fileName);

        if (peer.ip == 0 && peer.port == 0)
        {
            printf("No peer found for the specified download requirements\n");
            return;
        }

        if (Utils::fileExists(downLocation + "/" + fileName))
        {
            printf("File with the name %s already exists at your download location, so it will get overwritten. Continue? (y/n)\n",
                    fileName);
            char ch;
            std::cin >> ch;

            if (ch != 'y' && ch != 'Y')
            {
                printf("Cancelled file download\n");
                return;
            }
        }

        printf("Peer found, initiating connection at ip %d and port %d...\n", peer.ip, peer.port);
        p2pConnection(peer, fileName, hash);
        
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
            case CMD_SEARCH:
                executeSearch(command, socket);
                break;
            case CMD_DOWNLOAD:
                executeDownload(command, socket);
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
            printf("\n>: ");
            fflush(stdout);

            CHECK_CONTINUE(Utils::readInput(buff), "Max input size exceeded");

            cmdType type;
            type = Utils::validateCommand(buff);
            if (type == CMD_UNKNOWN)
            {
                print_err("Unknown command!");
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
        int clientSocket = * (int*)arg;
        char hash[MD5_HASH_SIZE];
        responseType response = RESPONSE_ERR;
        std::string path = ".";

        //citesc de la peer hashul fisierului pe care il doreste
        Utils::readFromServer(clientSocket, hash);

        //verific daca fisierul nu mai exista din oarecare motiv
        pthread_mutex_lock(&mutex);
        for(auto& it : publishedFiles)
        {
            if (strcmp(it.file.hash, hash) == 0)
            {
                path = it.path;
                break;
            }
        }
        pthread_mutex_unlock(&mutex);

        if (Utils::fileExists(path))
            response = RESPONSE_OK;

        //confirm request-ul ca fiind ok/eroare
        Utils::writeToServer(clientSocket, response);

        if (response == RESPONSE_OK)
            Utils::sendFile(clientSocket, path);

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
    pthread_mutex_init(&mutex, NULL);

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
        pthread_create(&thread, NULL, &Server::run, &clientSocket);
    }

    return 0;
}