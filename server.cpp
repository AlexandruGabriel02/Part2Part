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
#include <mysql/mysql.h>
#include <arpa/inet.h>
#include <openssl/md5.h>

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

struct publishedFile
{
    char name[MAX_SIZE];
    double size;
    fileType type;
    char hash[2 * MD5_DIGEST_LENGTH];
};

class DBManager
{
private:
    MYSQL* con;
    void db_error_warn()
    {
        fprintf(stderr, "%s\n", mysql_error(con));
    }
    void db_error_kill()
    {
        db_error_warn();
        mysql_close(con);
        exit(-1);
    }
public:
    DBManager()
    {
        con = mysql_init(NULL);

        if (con == NULL)
            db_error_kill();
    }

    void connect(const char* user, const char* pass, const char* dbName)
    {
        if (mysql_real_connect(con, "localhost", user, pass, dbName, 0, NULL, 0) == NULL)
            db_error_kill();
        printf("Connected to the MySQL database\n");
        fflush(stdout);
    }

    void insert_entry(const char* peerName, unsigned int ip, int port, 
                      const char* fileName, fileType type, double size, const char* hash)
    {
        char command[MAX_SIZE];
        const char* types[] = {"text", "audio", "video", "game", "software", "other", "unknown"};
        int index = (int) type;

        sprintf(command, "INSERT INTO published VALUES (\'%s\', %d, %d, \'%s\', \'%s\', %f, \'%s\')",
                peerName, ip, port, fileName, types[index], size, hash);

        if (mysql_query(con, command))
            db_error_warn();
    }

    void delete_entries(unsigned int ip, int port, const char* fileName, const char* hash)
    {
        char command[MAX_SIZE];

        sprintf(command, "DELETE FROM published WHERE ip=%d AND port=%d AND file=\'%s\' AND hash=\'%s\'",
                ip, port, fileName, hash);
        
        if (mysql_query(con, command))
            db_error_warn();
    }
};

DBManager db;

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

void readFromClient(int socket, char buff[])
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

template<class T>
void readFromClient(int socket, T& data)
{
    int dataSize, bytes;
    bytes = read(socket, &dataSize, sizeof(dataSize));
    if (bytes <= 0)
        killThread("Read error. Peer probably closed unexpectedly\n");

    bytes = read(socket, &data, dataSize);
    if (bytes <= 0)
        killThread("Read error. Peer probably closed unexpectedly\n");
}

void executeDisconnect(const sockaddr_in& client, const char* peerName, 
                      int openPeerPort)
{
    //sterge inregistrarile din baza de date
}

void executePublish(int socket, const sockaddr_in& client,
                    const char* peerName, int openPeerPort)
{
    //adauga in baza de date
    publishedFile file;
    readFromClient(socket, file);

    printf("Am primit fisierul %s de dimensiune %f cu hashul %s si tipul %d de la %s\n", file.name, file.size, file.hash, file.type, peerName);

    db.insert_entry(peerName, client.sin_addr.s_addr, openPeerPort, file.name, file.type, file.size, file.hash);
}

void executeUnpublish(int socket, const sockaddr_in& client,
                    const char* peerName, int openPeerPort)
{
    //scoate fisierul din baza de date
    publishedFile file;
    readFromClient(socket, file);

    printf("Am primit fisierul %s de dimensiune %f de la %s\n", file.name, file.size, peerName);

    db.delete_entries(client.sin_addr.s_addr, openPeerPort, file.name, file.hash);
}

void executeCommand(int socket, const sockaddr_in& client, const char* peerName, 
                    int openPeerPort, cmdType type)
{
    switch(type)
    {
        case CMD_DISCONNECT:
            executeDisconnect(client, peerName, openPeerPort);
            break;
        case CMD_PUBLISH:
            executePublish(socket, client, peerName, openPeerPort);
            break;
        case CMD_UNPUBLISH:
            executeUnpublish(socket, client, peerName, openPeerPort);
            break;
        default:
            break;
    }
}

void* runThread(void* arg)
{
    pthread_detach(pthread_self());
    threadArgs t = *( (threadArgs*)arg );
    bool connected = true;
    char peerName[MAX_SIZE];
    int openPeerPort;

    readFromClient(t.clientSocket, peerName);
    readFromClient(t.clientSocket, openPeerPort);

    printf("Conectat cu user-ul %s ce va da share prin port-ul %d\n", peerName, openPeerPort);

    while (connected)
    {
        cmdType command;
        readFromClient(t.clientSocket, command);

        printf("Am primit: %d\n", (int)command);

        executeCommand(t.clientSocket, t.client, peerName, openPeerPort, command);

        if (command == CMD_DISCONNECT)
            connected = false;
    }

    printf("Inchid thread-ul pentru user-ul %s\n", peerName);
    close(t.clientSocket);
    return NULL;
}

int main(int argc, char* argv[])
{
    sockaddr_in server;
    int serverSocket;

    initServer(server, serverSocket);    
    db.connect("test_user", "XRVSskp42ABC@!", "peers");


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