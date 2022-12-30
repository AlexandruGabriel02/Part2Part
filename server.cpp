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

struct argsInfo
{
    char name[MAX_SIZE];
    double maxSize;
    double minSize;
    fileType type;
    char hash[2 * MD5_DIGEST_LENGTH];
};

struct peerInfo
{
    unsigned int ip;
    int port;
};

enum responseType
{
    RESPONSE_ERR = -1,
    RESPONSE_OK
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

    int insert_entry(const char* peerName, unsigned int ip, int port, 
                      const char* fileName, fileType type, double size, const char* hash)
    {
        char command[MAX_SIZE];
        const char* types[] = {"text", "audio", "video", "game", "software", "other", "unknown"};
        int index = (int) type;

        sprintf(command, "INSERT INTO published VALUES (\'%s\', %d, %d, \'%s\', \'%s\', %f, \'%s\')",
                peerName, ip, port, fileName, types[index], size, hash);

        if (mysql_query(con, command))
        {
            db_error_warn();
            return -1;
        }

        return 0;
    }

    int delete_entries(unsigned int ip, int port, const char* fileName, const char* hash)
    {
        char command[MAX_SIZE];

        sprintf(command, "DELETE FROM published WHERE ip=%d AND port=%d AND file=\'%s\' AND hash=\'%s\'",
                ip, port, fileName, hash);
        
        if (mysql_query(con, command))
        {
            db_error_warn();
            return -1;
        }

        return 0;
    }

    int delete_entries(unsigned int ip, int port)
    {
        char command[MAX_SIZE];

        sprintf(command, "DELETE FROM published WHERE ip=%d AND port=%d",
                ip, port);
        
        if (mysql_query(con, command))
        {
            db_error_warn();
            return -1;
        }

        return 0;
    }

    int retrieve_entries(char output[], const argsInfo& searchInfo)
    {
        char command[2 * MAX_SIZE];
        strcpy(output, "");
        sprintf(command, "SELECT username, file, type, size, hash FROM published WHERE size >= %f", 
               searchInfo.minSize);

        if (strcmp(searchInfo.name, "*") != 0)
        {
            char add[MAX_SIZE + 20];
            sprintf(add, " AND file = \'%s\'", searchInfo.name);
            strcat(command, add);
        }

        if (searchInfo.maxSize != -1)
        {
            char add[MAX_SIZE];
            sprintf(add, " AND size <= %f", searchInfo.maxSize);
            strcat(command, add);
        }

        if (searchInfo.type != FILE_UNKNOWN)
        {
            const char* types[] = {"text", "audio", "video", "game", "software", "other", "unknown"};
            char add[MAX_SIZE];
            sprintf(add, " AND type = \'%s\'", types[(int)searchInfo.type]);
            strcat(command, add);
        }

        printf("%s\n", command);

        if (mysql_query(con, command))
        {
            db_error_warn();
            return -1;
        }

        MYSQL_RES *result = mysql_store_result(con);
        if (result == NULL)
        {
            db_error_warn();
            return -1;
        }

        int num_fields = mysql_num_fields(result);
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(result)))
        {
            for(int i = 0; i < num_fields; i++)
            {
                strcat(output, row[i] ? row[i] : "NULL");
                strcat(output, " ");
            }

            strcat(output, "\n");
        }

        mysql_free_result(result);
        return 0;
    }

    int retrieve_peer(peerInfo& peer, char fileName[], char hash[], const argsInfo& searchInfo)
    {
        peer.ip = peer.port = 0;

        char command[2 * MAX_SIZE];
        sprintf(command, "SELECT * FROM published WHERE size >= %f", 
               searchInfo.minSize);

        if (strcmp(searchInfo.name, "*") != 0)
        {
            char add[MAX_SIZE + 20];
            sprintf(add, " AND file = \'%s\'", searchInfo.name);
            strcat(command, add);
        }

        if (searchInfo.maxSize != -1)
        {
            char add[MAX_SIZE];
            sprintf(add, " AND size <= %f", searchInfo.maxSize);
            strcat(command, add);
        }

        if (searchInfo.type != FILE_UNKNOWN)
        {
            const char* types[] = {"text", "audio", "video", "game", "software", "other", "unknown"};
            char add[MAX_SIZE];
            sprintf(add, " AND type = \'%s\'", types[(int)searchInfo.type]);
            strcat(command, add);
        }

        if (strlen(searchInfo.hash) > 0)
        {
            char add[MAX_SIZE];
            sprintf(add, " AND hash = \'%s\'", searchInfo.hash);
            strcat(command, add);
        }

        if (mysql_query(con, command))
        {
            db_error_warn();
            return -1;
        }

        MYSQL_RES *result = mysql_store_result(con);
        if (result == NULL)
        {
            db_error_warn();
            return -1;
        }

        MYSQL_ROW row;
        if ((row = mysql_fetch_row(result)))
        {
            peer.ip = atoi(row[1]);
            peer.port = atoi(row[2]);
            sprintf(fileName, "%s", row[3]); //fix this
            fileName[strlen(fileName)] = '\0';
            sprintf(hash, "%s", row[6]);
            std::cout << fileName << " " << hash << "\n";
        }

        mysql_free_result(result);
        return 0;
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

void executeDisconnect(const sockaddr_in& client, int openPeerPort)
{
    //sterge inregistrarile din baza de date
    db.delete_entries(client.sin_addr.s_addr, openPeerPort);
}

void writeResponse(int socket, responseType response)
{
    write(socket, &response, sizeof(response));
}

void killThread(const char* message, const sockaddr_in& client, int openPeerPort)
{
    printf("%s\n", message);
    executeDisconnect(client, openPeerPort);
    pthread_exit(NULL);
}

int readFromClient(int socket, char buff[])
{
    int msgLength, bytes;
    bytes = read(socket, &msgLength, sizeof(msgLength));
    if (bytes <= 0)
        return -1;

    bytes = read(socket, buff, msgLength);
    if (bytes <= 0)
        return -1;

    buff[msgLength] = '\0';

    return 0;
}

template<class T>
int readFromClient(int socket, T& data)
{
    int dataSize, bytes;
    bytes = read(socket, &dataSize, sizeof(dataSize));
    if (bytes <= 0)
        return -1;

    bytes = read(socket, &data, dataSize);
    if (bytes <= 0)
        return -1;
    
    return 0;
}

void writeToClient(int socket, char buff[])
{
    int msgLength = strlen(buff);
    write(socket, &msgLength, sizeof(msgLength));
    write(socket, buff, msgLength);
}

template <class T>
void writeToClient(int socket, const T& data)
{
    int dataSize = sizeof(data);
    write(socket, &dataSize, sizeof(dataSize));
    write(socket, &data, dataSize);
}

void executePublish(int socket, const sockaddr_in& client,
                    const char* peerName, int openPeerPort)
{
    //adauga in baza de date
    publishedFile file;
    if (readFromClient(socket, file) == -1)
        killThread("Read error. Peer probably closed unexpectedly\n", client, openPeerPort);
    
    responseType response;
    response = (responseType) db.insert_entry(peerName, client.sin_addr.s_addr, 
                            openPeerPort, file.name, file.type, file.size, file.hash);
    
    writeResponse(socket, response);
}

void executeUnpublish(int socket, const sockaddr_in& client,
                    const char* peerName, int openPeerPort)
{
    //scoate fisierul din baza de date
    publishedFile file;
    if (readFromClient(socket, file) == -1)
        killThread("Read error. Peer probably closed unexpectedly\n", client, openPeerPort);

    responseType response;
    response = (responseType) db.delete_entries(client.sin_addr.s_addr, openPeerPort, file.name, file.hash);

    writeResponse(socket, response);
}

void executeSearch(int socket, const sockaddr_in& client, int openPeerPort)
{
    argsInfo searchInfo;
    if (readFromClient(socket, searchInfo) == -1)
        killThread("Read error. Peer probably closed unexpectedly\n", client, openPeerPort);
    
    char searchResult[2 * MAX_SIZE];

    responseType response;
    response = (responseType) db.retrieve_entries(searchResult, searchInfo);
    writeResponse(socket, response);

    if (response == RESPONSE_OK)
        writeToClient(socket, searchResult);
}

void executeDownload(int socket, const sockaddr_in& client, int openPeerPort)
{
    argsInfo searchInfo;
    if (readFromClient(socket, searchInfo) == -1)
        killThread("Read error. Peer probably closed unexpectedly\n", client, openPeerPort);
    
    peerInfo peer;
    char hash[2 * MD5_DIGEST_LENGTH];
    char fileName[MAX_SIZE];

    responseType response;
    response = (responseType) db.retrieve_peer(peer, fileName, hash, searchInfo);
    writeResponse(socket, response);

    if (response == RESPONSE_OK)
    {
        writeToClient(socket, peer);
        writeToClient(socket, hash);
        writeToClient(socket, fileName);
    }
}

void executeCommand(int socket, const sockaddr_in& client, const char* peerName, 
                    int openPeerPort, cmdType type)
{
    switch(type)
    {
        case CMD_DISCONNECT:
            executeDisconnect(client, openPeerPort);
            break;
        case CMD_PUBLISH:
            executePublish(socket, client, peerName, openPeerPort);
            break;
        case CMD_UNPUBLISH:
            executeUnpublish(socket, client, peerName, openPeerPort);
            break;
        case CMD_SEARCH:
            executeSearch(socket, client, openPeerPort);
            break;
        case CMD_DOWNLOAD:
            executeDownload(socket, client, openPeerPort);
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

    if (readFromClient(t.clientSocket, peerName) == -1)
        killThread("Read error. Peer probably closed unexpectedly\n", t.client, openPeerPort);
    if (readFromClient(t.clientSocket, openPeerPort) == -1)
        killThread("Read error. Peer probably closed unexpectedly\n", t.client, openPeerPort);

    printf("Conectat cu user-ul %s ce va da share prin port-ul %d\n", peerName, openPeerPort);

    while (connected)
    {
        cmdType command;
        if (readFromClient(t.clientSocket, command) == -1)
            killThread("Read error. Peer probably closed unexpectedly\n", t.client, openPeerPort);

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