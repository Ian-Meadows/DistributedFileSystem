// compile with "gcc -pthread -o client DFClient.c -lcrypto"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>      /* for fgets */
#include <strings.h>     /* for bzero, bcopy */
#include <unistd.h>      /* for read, write */
#include <sys/socket.h>  /* for socket use */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include <openssl/md5.h>
#include <stdbool.h>

#define BUFSIZE 1024

#define BUFFER_SIZE (256 * 1024)//256kB

//one second
#define TIMEOUT = 1;


extern int errno ;


struct ServerInfo{
    char* IP;
    int port;

    char* serverName;

    struct sockaddr_in servaddr;
};

struct Info{
    struct ServerInfo* server1;
    struct ServerInfo* server2;
    struct ServerInfo* server3;
    struct ServerInfo* server4;

    char* username;
    char* password;

    char* command;
    unsigned int commandSize;
};

struct Info* CreateInfo();
struct Info* CloneInfo(struct Info* info);
struct ServerInfo* CreateServerInfo();
struct Info* ParseConfig(char* configPath);
void FreeInfo(struct Info* info);
void* Thread(void* vargp);
void CommandHandler(struct Info* info);
void CalculateMD5Hash(char* file, int size, char* hash);
char* GeneratePutCommand(char* fileName, char* username, char* password, int part, int fileSize,
 char* file, int* commandSize);
char* GenerateGetCommand(char* fileName, char* username, char* password, char* partsNeeded, int* commandSize);
char* GenerateListCommand(char* username, char* password, int* commandSize);

//read file and return array of file
char* ReadFile(int* size, char* fileName){
  FILE* fp;
  fp = fopen(fileName, "rb");

  if(fp == NULL){
    printf("ERROR: %s\n", strerror(errno));
    return NULL;
  }
  //get file size
  fseek(fp, 0L, SEEK_END);
  long fs = ftell(fp);

  //its rewind time
  rewind(fp);
  char* file = (char*)malloc(fs * sizeof(char));

  //read files
  fread(file, 1, fs, fp);
  *size = (int)fs;
  fclose(fp);

  return file;
}

int WriteFile(char* file, int size, char* fileName){
    FILE* fp;
    fp = fopen(fileName, "wb");

    if(fp == NULL){
        return 0;
    }

    //write file
    fwrite(file, size, 1, fp);
    fclose(fp);
    return 1;

}

int SendToServer(int server, char* file, int fileSize, struct Info* info, int *sockfd){
    struct ServerInfo* serverInfo;
    if(server == 1){
        serverInfo = info->server1;
    }
    else if(server == 2){
        serverInfo = info->server2;
    }
    else if(server == 3){
        serverInfo = info->server3;
    }
    else if(server == 4){
        serverInfo = info->server4;
    }
    else{
        printf("umm what do you think your doing?\n");
        return 0;
    }

    //limit file size to 256kB
    if(fileSize >= BUFFER_SIZE){
        printf("Unable to send file to server: File is to big\n");
        return 0;
    }

    //connect to server
    if(connect(*sockfd, (struct sockaddr*)&serverInfo->servaddr, sizeof(struct sockaddr)) != 0){
        printf("Failed to connect to server: %i\n", server);
        return 0;
    }

    //send input to server
    write(*sockfd, file, BUFFER_SIZE);
    return 1;
}

struct GetPiece{
    char* filePiece;
    int filePieceSize;
    
};

struct GetInfo{
    struct GetPiece* pieces[4]; 
};

void FreeGetInfo(struct GetInfo* getInfo){
    for(int i = 0; i < 4; ++i){
        if(getInfo->pieces[i] != NULL){
            if(getInfo->pieces[i]->filePiece != NULL){
                free(getInfo->pieces[i]->filePiece);
            }
            free(getInfo->pieces[i]);
        }
    }

    free(getInfo);
}

struct GetInfo* CreateGetInfo(){
    struct GetInfo* getInfo = malloc(sizeof(struct GetInfo));
    for(int i = 0; i < 4; ++i){
        getInfo->pieces[i] = NULL;
    }

    return getInfo;
}

bool SetupGetStruct(struct GetInfo* getInfo, char* buffer){
    char* strPart = strtok(buffer, " ");

    //error check
    if(strcmp(strPart, "ERROR:") == 0){
        //might not work properly
        printf("%s\n", strtok(NULL, "\0"));
        return false;
    }

    char* strFileSize = strtok(NULL, " ");
    
    int part = atoi(strPart);
    int fileSize = atoi(strFileSize);

    
    char* file = malloc(fileSize);
    memcpy(file, strFileSize + strlen(strFileSize) + 1, fileSize);
    
    if(getInfo->pieces[part - 1] == NULL){
        struct GetPiece* newPiece = malloc(sizeof(struct GetPiece));
        getInfo->pieces[part - 1] = newPiece;

        newPiece->filePieceSize = fileSize;
        newPiece->filePiece = file;
    }

    return true;
}

char* ReconstructFile(struct GetInfo* getInfo, int* fileSize){
    int size = 0;
    int totalPieces = 0;
    for(int i = 0; i < 4; ++i){
        if(getInfo->pieces[i] != NULL){
            size += getInfo->pieces[i]->filePieceSize;
            ++totalPieces;
        }
    }
    if(totalPieces != 4){
        printf("ERROR: File is incomplete\n");
        return NULL;
    }
    char* file = malloc(size);
    
    //put all file pieces into one file
    memcpy(file, getInfo->pieces[0]->filePiece, getInfo->pieces[0]->filePieceSize);

    memcpy(file + getInfo->pieces[0]->filePieceSize,
     getInfo->pieces[1]->filePiece, getInfo->pieces[1]->filePieceSize);

    memcpy(file + getInfo->pieces[0]->filePieceSize + getInfo->pieces[1]->filePieceSize,
     getInfo->pieces[2]->filePiece, getInfo->pieces[2]->filePieceSize);

    memcpy(file + getInfo->pieces[0]->filePieceSize + getInfo->pieces[1]->filePieceSize + getInfo->pieces[2]->filePieceSize,
     getInfo->pieces[3]->filePiece, getInfo->pieces[3]->filePieceSize);

    *fileSize = size;

    return file;

    
}

char* GeneratePartsNeeded(struct GetInfo* getInfo){
    char* partsNeeded = malloc(5);
    char nums[] = "1234";
    int index = 0;
    int i;
    for(i = 0; i < 4; ++i){
        if(getInfo->pieces[i] == NULL){
            partsNeeded[index] = nums[i];
            ++index;
        }
    }
    partsNeeded[index] = '\0';

    return partsNeeded;
}

//get filename username password
void Get(char* fileName, struct Info* info){

    //init getInfo 
    struct GetInfo* getInfo = CreateGetInfo();
    
    //loop through all 4 servers twice
    for(int j = 0; j < 2; ++j){
        for(int i = 1; i < 5; ++i){
            

            char* partsNeeded = GeneratePartsNeeded(getInfo);
            if(strcmp(partsNeeded, "") == 0){
                break;
            }
            int sockfd = socket(AF_INET, SOCK_STREAM, 0);
            if(sockfd == -1){
                printf("Failed to create socket\n");
                free(partsNeeded);
                continue;
            }

            //create command
            int commandSize = 0;
            char* command = GenerateGetCommand(fileName, info->username, info->password, partsNeeded, &commandSize);

            //send command
            if(!SendToServer(i, command, commandSize, info, &sockfd)){
                close(sockfd);
                free(partsNeeded);
                free(command);
                close(sockfd);
                continue;
            }

            free(partsNeeded);

            free(command);
            
            //read socket
            char buffer[BUFFER_SIZE];
            read(sockfd, buffer, BUFFER_SIZE);
            //printf("Buffer1: %s\n", buffer);
            if(!SetupGetStruct(getInfo, buffer)){
                close(sockfd);
                continue;
            }
            close(sockfd);
            
        }
    }

    //reconstruct file
    int fileSize = 0;
    char* file = ReconstructFile(getInfo, &fileSize);

    if(file != NULL){
        //write file

        if(!WriteFile(file, fileSize, fileName)){
            printf("Error writing file\n");
        }
        free(file);
    }

    FreeGetInfo(getInfo);
    
}

//create the correct server number order based off of the mod value
int** GenerateServerNumbers(int mod){
    int number[4][2] = {
            {1, 2}, {2, 3}, {3, 4}, {4, 1}
    };
    int** numbers = malloc(4 * sizeof(int*));
    for(int i = 0; i < 4; ++i){
        int index = i - mod;
        if(index < 0){
            index += 4;
        }
        numbers[i] = malloc(2 * sizeof(int));
        numbers[i][0] = number[index][0];
        numbers[i][1] = number[index][1];
    }

    return numbers;
}

void FreeServerNumbers(int** numbers){
    for(int i = 0; i < 4; ++i){
        free(numbers[i]);
    }
    free(numbers);
}

//put filename username password part filesize file
void Put(char* fileName, struct Info* info){
    int size = 0;
    char* file = ReadFile(&size, fileName);
    if(file == NULL){
        return;
    }

    char hash[16];
    CalculateMD5Hash(file, size, hash);

    //jenky makeshift mod
    int mod = hash[15] & 3;

    //split file into 4 pieces
    char* filePieces[4];
    int pieceSize = size / 4;
    int lastPieceSize = pieceSize + size%4;
    //copy over file
    for(int i = 0; i < 3; ++i){
        filePieces[i] = malloc(pieceSize);
        memcpy(filePieces[i], file + (pieceSize * i), pieceSize);
    }
    filePieces[3] = malloc(lastPieceSize);
    memcpy(filePieces[3], file + (pieceSize * 3), lastPieceSize);

    int pieceSizes[] = {
        pieceSize, pieceSize, pieceSize, lastPieceSize
    };
    
    int** serverNumbers = GenerateServerNumbers(mod);
    //send file pieces

    
    //loop through all servers and send both file pieces
    for(int i = 0; i < 4; ++i){

        //create socket
        int sockfd1 = socket(AF_INET, SOCK_STREAM, 0);
        int sockfd2 = socket(AF_INET, SOCK_STREAM, 0);
        if(sockfd1 == -1 || sockfd2 == -1){
            printf("Failed to create socket\n");
            continue;
        }

        //command 1
        int commandSize1 = 0;
        char* command1 = GeneratePutCommand(fileName, info->username, info->password, serverNumbers[i][0],
         pieceSizes[serverNumbers[i][0] - 1], filePieces[serverNumbers[i][0] - 1], &commandSize1);

        if(SendToServer(i + 1, command1, commandSize1, info, &sockfd1)){
            char buffer[BUFFER_SIZE];
            read(sockfd1, buffer, BUFFER_SIZE);
            char* command = strtok(buffer, " ");
            //error from server
            if(strcmp(command, "ERROR:") == 0){
                printf("%s\n", strtok(NULL, "\0"));
            }
        }
        else{
            close(sockfd1);
            continue;
        }
        close(sockfd1);

        //command 2
        int commandSize2 = 0;
        char* command2 = GeneratePutCommand(fileName, info->username, info->password, serverNumbers[i][1],
         pieceSizes[serverNumbers[i][1] - 1], filePieces[serverNumbers[i][1] - 1], &commandSize2);


        if(SendToServer(i + 1, command2, commandSize2, info, &sockfd2)){
            char buffer[BUFFER_SIZE];
            read(sockfd2, buffer, BUFFER_SIZE);
            char* command = strtok(buffer, " ");
            //error from server
            if(strcmp(command, "ERROR:") == 0){
                printf("%s\n", strtok(NULL, "\0"));
            }
        }
        else{
            close(sockfd2);
            continue;
        }
        close(sockfd2);
    } 

    FreeServerNumbers(serverNumbers);
    
    free(file);

    //free pieces
    for(int i = 0; i < 4; ++i){
        free(filePieces[i]);
    }
    

}

struct FileInfo{
    char* fileName;
    bool part1;
    bool part2;
    bool part3;
    bool part4;
};

struct ListInfo{
    struct FileInfo** fileInfos;
    int size;
    int nextIndex;
};

void FreeListInfo(struct ListInfo* listInfo){
    for(int i = 0; i < listInfo->size; ++i){
        if(listInfo->fileInfos[i] != NULL){
            free(listInfo->fileInfos[i]->fileName);
            free(listInfo->fileInfos[i]);
        }
    }
    if(listInfo->fileInfos != NULL){
        free(listInfo->fileInfos);
    }
    
    free(listInfo);
}

struct ListInfo* CreateListInfo(){
    struct ListInfo* listInfo = malloc(sizeof(struct ListInfo));
    listInfo->fileInfos = NULL;
    listInfo->size = 0;
    listInfo->nextIndex = 0;

    return listInfo;
}

void AddFileInfo(struct FileInfo* fileInfo, struct ListInfo* listInfo){
    if(listInfo->fileInfos == NULL || listInfo->size == 0){
        listInfo->fileInfos = malloc(sizeof(struct FileInfo*));
        listInfo->fileInfos[0] = fileInfo;
        listInfo->size = 1;
        listInfo->nextIndex = 1;
        return;
    }

    if(listInfo->size <= listInfo->nextIndex){
        //double size
        struct FileInfo** oldInfos = listInfo->fileInfos;
        int oldSize = listInfo->size;
        listInfo->size*=2;
        listInfo->fileInfos = malloc(sizeof(struct FileInfo*) * listInfo->size);

        //copy pointers over
        for(int i = 0; i < oldSize; ++i){
            listInfo->fileInfos[i] = oldInfos[i];
        }

        //set left overs to null
        for(int i = listInfo->nextIndex; i < listInfo->size; ++i){
            listInfo->fileInfos[i] = NULL;
        }
        listInfo->fileInfos[listInfo->nextIndex] = fileInfo;
        ++listInfo->nextIndex;
        free(oldInfos);

        return;
    }
    listInfo->fileInfos[listInfo->nextIndex] = fileInfo;
    ++listInfo->nextIndex;
}

struct FileInfo* GetFileInfo(struct ListInfo* listInfo, char* filename){
    for(int i = 0; i < listInfo->nextIndex; ++i){
        if(strcmp(listInfo->fileInfos[i]->fileName, filename) == 0){
            return listInfo->fileInfos[i];
        }
    }
    return NULL;
}

void SetupListInfo(struct ListInfo* listInfo, char buffers[4][BUFFER_SIZE]){

    for(int i = 0; i < 4; ++i){
        //check for empty
        if(strcmp(buffers[i], "") == 0){
            continue;
        }
        char* line = strtok(buffers[i], "\n");
        int len = 0;
        while(line != NULL){
            len += strlen(line) + 1;
            
            if(strcmp(line, ".") == 0 || strcmp(line, "..") == 0){
                line = strtok(buffers[i] + len, "\n");
                
                continue;
            }
            
            char* strPart = strrchr(line, '.') + 1;
            int part = atoi(strPart);

            char* cLine = line + 1;

            char* fileName = malloc(strlen(cLine) - 1);
            memcpy(fileName, cLine, strlen(cLine) - 2);
            fileName[strlen(cLine) - 2] = '\0';
            
            struct FileInfo* fileInfo = GetFileInfo(listInfo, fileName);


            if(fileInfo == NULL){
                struct FileInfo* newFileInfo = malloc(sizeof(struct FileInfo));
                newFileInfo->fileName = fileName;
                newFileInfo->part1 = false;
                newFileInfo->part2 = false;
                newFileInfo->part3 = false;
                newFileInfo->part4 = false;
                AddFileInfo(newFileInfo, listInfo);
            }
            else{
                if(part == 1){
                    fileInfo->part1 = true;
                }
                else if(part == 2){
                    fileInfo->part2 = true;
                }
                else if(part == 3){
                    fileInfo->part3 = true;
                }
                else if(part == 4){
                    fileInfo->part4 = true;
                }
                else{
                    printf("what did you just do????\n");
                }
                free(fileName);
            }
            line = strtok(buffers[i] + len, "\n");
            
        }
    }

}

void PrintOutListInfo(struct ListInfo* listInfo){
    for(int i = 0; i < listInfo->nextIndex; ++i){
        printf("%s", listInfo->fileInfos[i]->fileName);
        
        if(!(listInfo->fileInfos[i]->part1 && listInfo->fileInfos[i]->part2 &&
         listInfo->fileInfos[i]->part3 && listInfo->fileInfos[i]->part4)){
            //incomplete
            printf(" [incomplete]");
             
        }
        printf("\n");
    }
}

//list username password
void List(struct Info* info){

    struct ListInfo* listInfo = CreateListInfo();

    int commandSize = 0;
    char* command = GenerateListCommand(info->username, info->password, &commandSize);


    char buffers[4][BUFFER_SIZE];
    
    //load up the 4 buffers
    for(int i = 0; i < 4; ++i){
        //create socket
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if(sockfd == -1){
            printf("Failed to create socket");
            return;
        }
        if(!SendToServer(i+1, command, commandSize, info, &sockfd)){
            printf("error\n");
            continue;
        }
        bzero(buffers[i], BUFFER_SIZE);
        read(sockfd, buffers[i], BUFFER_SIZE);

        close(sockfd);

        if(strcmp(strtok(buffers[i], " "), "ERROR:") == 0){
            printf("%s\n", strtok(NULL, "\0"));
            
            buffers[i][0] = '\0';
            continue;
        }
    }
    SetupListInfo(listInfo, buffers);

    PrintOutListInfo(listInfo);

   free(command);
   free(listInfo);
}

int main(int argc, char** argv){

    if(argc != 2){
        fprintf(stderr, "usage: %s <config file path>\n", argv[0]);
	    exit(0);
    }

    struct Info* info = ParseConfig(argv[1]);
    if(info == NULL){
        fprintf(stderr, "Config file %s does not exist or is formatted improperly\n", argv[1]);
        exit(0);
    }

    char buf[BUFSIZE];
    pthread_t tid;

    while(1){
        printf("\nPlease enter one: \n");
        printf("get [file_name]\n");
        printf("put [file_name]\n");
        printf("list\n");


        bzero(buf, BUFSIZE);
        fgets(buf, BUFSIZE, stdin);

        struct Info* newInfo = CloneInfo(info);
        newInfo->command = malloc(strlen(buf));
        strcpy(newInfo->command, buf);
        newInfo->commandSize = strlen(buf);

        CommandHandler(newInfo);
        FreeInfo(newInfo);
    }

    FreeInfo(info);

    return 0;
}

void CommandHandler(struct Info* info){

    char* tempCommand = strtok(info->command, "\n");

    char* command = malloc(strlen(tempCommand));
    strcpy(command, tempCommand);

    if(strcmp(command, "list") == 0){
        List(info);
    }
    else{
        command = strtok(command, " ");
        if(strcmp(command, "get") == 0){
            char* fileName = strtok(NULL, "\0");
            Get(fileName, info);
        }
        else if(strcmp(command, "put") == 0){
            char* fileName = strtok(NULL, "\0");
            Put(fileName, info);
        }
        else{
            printf("%s is not a valid command\n", tempCommand);
        }
    }

    
    
    free(command);

}




struct Info* ParseConfig(char* configPath){
    FILE* fp = fopen(configPath, "r");

    if(fp == NULL){
        return NULL;
    }

    struct Info* info = CreateInfo();

    char line[512];
    while(fgets(line, 512, fp)){
        char* cLine = strtok(line, "\n\r");

        char* word1 = strtok(cLine, " ");

        if(strcmp(word1, "Server") == 0){

            //server name
            char* serverName = strtok(NULL, " ");

            //ip
            char* ip = strtok(NULL, ":");

            //port
            char* port = strtok(NULL, "\0");

            struct ServerInfo* server = NULL;
            if(strcmp(serverName, "DFS1") == 0){
                if(info->server1 != NULL){
                    fclose(fp);
                    FreeInfo(info);
                    return NULL; 
                }
                info->server1 = CreateServerInfo();
                server = info->server1;
            }
            else if(strcmp(serverName, "DFS2") == 0){
                if(info->server2 != NULL){
                    fclose(fp);
                    FreeInfo(info);
                    return NULL; 
                }
                info->server2 = CreateServerInfo();
                server = info->server2;
            }
            else if(strcmp(serverName, "DFS3") == 0){
                if(info->server3 != NULL){
                    fclose(fp);
                    FreeInfo(info);
                    return NULL; 
                }
                info->server3 = CreateServerInfo();
                server = info->server3;
            }
            else if(strcmp(serverName, "DFS4") == 0){
                if(info->server4 != NULL){
                    fclose(fp);
                    FreeInfo(info);
                    return NULL; 
                }
                info->server4 = CreateServerInfo();
                server = info->server4;
            }
            else{
                fclose(fp);
                FreeInfo(info);
                return NULL;
            }
            
            if(server == NULL){
                fclose(fp);
                FreeInfo(info);
                return NULL;
            }

            server->serverName = malloc(strlen(serverName));
            strcpy(server->serverName, serverName);

            server->IP = malloc(strlen(ip));
            strcpy(server->IP, ip);

            server->port = atoi(port);

            server->servaddr.sin_family = AF_INET;
            server->servaddr.sin_addr.s_addr = inet_addr(server->IP);
            server->servaddr.sin_port = htons(server->port);

        }
        else if(strcmp(word1, "Username:") == 0){
            char* username = strtok(NULL, "\0");

            info->username = malloc(strlen(username));
            strcpy(info->username, username);

        }
        else if(strcmp(word1, "Password:") == 0){
            char* password = strtok(NULL, "\0");

            info->password = malloc(strlen(password));
            strcpy(info->password, password);
        }
        else{
            fclose(fp);
            FreeInfo(info);
            return NULL;
        }

    }

    fclose(fp);
    if(info->server1 == NULL || info->server2 == NULL || info->server3 == NULL || 
            info->server4 == NULL || info->password == NULL || info->username == NULL){
        
        FreeInfo(info);
        return NULL;
    }
    return info;

}

struct ServerInfo* CreateServerInfo(){
    struct ServerInfo* server = malloc(sizeof(struct ServerInfo));

    server->IP = NULL;
    server->serverName = NULL;
    server->port = 0;

    bzero(&server->servaddr, sizeof(server->servaddr));

    return server;
}

struct Info* CreateInfo(){
    struct Info* info = malloc(sizeof(struct Info));

    info->server1 = NULL;
    info->server2 = NULL;
    info->server3 = NULL;
    info->server4 = NULL;

    info->username = NULL;
    info->password = NULL;

    info->command = NULL;
    info->commandSize = 0;

    return info;
}

struct ServerInfo* CloneServerInfo(struct ServerInfo* server){
    struct ServerInfo* newServer = malloc(sizeof(struct ServerInfo));

    newServer->servaddr = server->servaddr;

    newServer->port = server->port;

    newServer->serverName = malloc(strlen(server->serverName));
    strcpy(newServer->serverName, server->serverName);

    newServer->IP = malloc(strlen(server->IP));
    strcpy(newServer->IP, server->IP);

    return newServer;
}

struct Info* CloneInfo(struct Info* info){
    struct Info* newInfo = malloc(sizeof(struct Info));

    newInfo->server1 = CloneServerInfo(info->server1);
    newInfo->server2 = CloneServerInfo(info->server2);
    newInfo->server3 = CloneServerInfo(info->server3);
    newInfo->server4 = CloneServerInfo(info->server4);
    
    newInfo->command = NULL;
    newInfo->commandSize = 0;

    newInfo->username = malloc(strlen(info->username));
    strcpy(newInfo->username, info->username);

    newInfo->password = malloc(strlen(info->password));
    strcpy(newInfo->password, info->password);

    return newInfo;
}

void FreeServer(struct ServerInfo* server){
    if(server == NULL){
        return;
    }
    if(server->serverName != NULL){
        free(server->serverName); 
    }
    if(server->IP != NULL){
        free(server->IP);
    }
    free(server);
}

void FreeInfo(struct Info* info){
    if(info == NULL){
        return;
    }

    FreeServer(info->server1);
    FreeServer(info->server2);
    FreeServer(info->server3);
    FreeServer(info->server4);

    if(info->username != NULL){
        free(info->username);
    }
    if(info->password != NULL){
        free(info->password);
    }
    if(info->command != NULL){
        free(info->command);
    }

    free(info);
}

//hash must be of size 16
void CalculateMD5Hash(char* file, int size, char* hash){
    MD5_CTX c;

    MD5_Init(&c);

    MD5_Update(&c, file, size);

    //char md5[16];
    MD5_Final(hash, &c);

}

//list username password
char* GenerateListCommand(char* username, char* password, int* commandSize){
    int size = strlen("list") + strlen(username) + strlen(password) + 3;
    *commandSize = size;

    char* command = malloc(size);

    strcpy(command, "list ");
    strcat(command, username);
    strcat(command, " ");
    strcat(command, password);

    return command;
}

//get filename username password partsneeded(add)
char* GenerateGetCommand(char* fileName, char* username, char* password, char* partsNeeded, int* commandSize){
    int size = strlen("get") + strlen(fileName) + strlen(username) + strlen(password) + strlen(partsNeeded)+ 5;
    *commandSize = size;
    char* command = malloc(size);

    strcpy(command, "get ");
    strcat(command, fileName);
    strcat(command, " ");
    strcat(command, username);
    strcat(command, " ");
    strcat(command, password);
    strcat(command, " ");
    strcat(command, partsNeeded);

    return command;
}

//put filename username password part filesize file
char* GeneratePutCommand(char* fileName, char* username, char* password, int part, int fileSize, char* file, int* commandSize){
    

    char strPart[2];
    sprintf(strPart, "%d", part);

    char strSize[32];
    sprintf(strSize, "%d", fileSize);

    int size = strlen("put") + strlen(fileName) + strlen(username) + strlen(password) +
    strlen(strPart) + strlen(strSize) + fileSize + 6;


    char* command = malloc(size);


    strcpy(command, "put ");
    strcat(command, fileName);
    strcat(command, " ");
    strcat(command, username);
    strcat(command, " ");
    strcat(command, password);
    strcat(command, " ");
    strcat(command, strPart);
    strcat(command, " ");
    strcat(command, strSize);
    strcat(command, " ");

    //file
    int j = 0;
    for(int i = size - fileSize; i < size; ++i){
        command[i] = file[j];
        ++j;
    }
    
    *commandSize = size;


    return command;


}