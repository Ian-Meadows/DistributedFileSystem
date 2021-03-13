//to compile: "gcc -pthread -o server DFServer.c"

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
#include <stdbool.h>
#include <sys/stat.h> 
#include <sys/types.h>
#include <dirent.h>

#define LISTENQ  1024  /* second argument to listen() */

#define BUFFER_SIZE (256 * 1024)//256kB

extern int errno ;


struct UserInfo{
    char* username;
    char* password;
};

struct Info{
    int* connfdp;
    char* folder;

    struct UserInfo** userInfos;
    unsigned int userInfoSize;
};



int OpenListenFD(int port);
void* Thread(void* vargp);
struct Info* ParseConfig(char* folder);
struct Info* CreateInfo(int size);
void FreeInfo(struct Info* info);
struct Info* CloneInfo(struct Info* info);
void HandlerConnection(int connfd, struct Info* info);
bool UserExists(char* username, char* password, struct Info* info);


//read file and return array of file
char* ReadFile(int* size, char* fileName){
  FILE* fp;
  fp = fopen(fileName, "rb");

  if(fp == NULL){
    //printf("ERROR: %s\n", strerror(errno));
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

int Get(struct Info* info, int connfd){

    //parse command
    char* filename = strtok(NULL, " ");
    char* username = strtok(NULL, " ");
    char* password = strtok(NULL, " ");
    char* partsNeeded = strtok(NULL, "\0");

    //check username and password
    if(!UserExists(username, password, info)){
        printf("User does not exist\n");
        //must send error back to client
        write(connfd, "ERROR: Invalid Username/Password. Please try again.", BUFFER_SIZE);
        return -1;
    }

    //create new directory name with username
    char* newDir = malloc(strlen(username) + strlen(info->folder));
    strcpy(newDir, info->folder);
    strcat(newDir, "/");
    strcat(newDir, username);

    //create path
    int filePathSize = strlen(newDir) + strlen(filename) + 4;
    char* filePath = malloc(filePathSize);
    strcpy(filePath, newDir);
    strcat(filePath, "/.");
    strcat(filePath, filename);
    strcat(filePath, ".");
    
    //loop though until a file with filename and extension of either .1, .2, .3, or .4
    bool foundFile = false;
    for(int i = 0; i < strlen(partsNeeded); ++i){
        filePath[filePathSize - 1] = partsNeeded[i];
        int size = 0;
        char* file = ReadFile(&size, filePath);
        if(file != NULL){
            foundFile = true;

            char strPart[2];
            char strSize[32];
            sprintf(strPart, "%c", partsNeeded[i]);
            sprintf(strSize, "%d", size);

            int commandSize = size + strlen(strPart) + strlen(strSize) + 2;
            //create command to send back
            char* command = malloc(commandSize);
            strcpy(command, strPart);
            strcat(command, " ");
            strcat(command, strSize);
            strcat(command, " ");
            memcpy(command + strlen(strPart) + strlen(strSize) + 2, file, size);

            //send
            write(connfd, command, BUFFER_SIZE);
            
            free(command);

            break;

        }
    }

    //handle errors
    if(!foundFile){
        //write(connfd, "29", 32);
        write(connfd, "ERROR: could not find file", 29);
    }

    free(filePath);
    free(newDir);

    return 0;
}

//put filename username password part filesize file
int Put(struct Info* info, char* buffer, int connfd){

    //parse command
    char* filename = strtok(NULL, " ");

    char* username = strtok(NULL, " ");
    char* password = strtok(NULL, " ");

    //check username and password
    if(!UserExists(username, password, info)){
        printf("User does not exist\n");
        //must send error back to client
        write(connfd, "ERROR: Invalid Username/Password. Please try again.", BUFFER_SIZE);
        return -1;
    }

    //create new directory name with username
    char* newDir = malloc(strlen(username) + strlen(info->folder));
    strcpy(newDir, info->folder);
    strcat(newDir, "/");
    strcat(newDir, username);

    //try to create directory
    mkdir(newDir, 0777);

    //get more information
    char* strPart = strtok(NULL, " ");
    char* strFileSize = strtok(NULL, " ");

    int part = atoi(strPart);
    int fileSize = atoi(strFileSize);

    
    //calculate location file is at
    int sizeToFile = &strFileSize[0] - &buffer[0] + strlen(strFileSize) + 1;
    

    //create file to save
    char* file = malloc(fileSize);

    int j = 0;
    for(int i = sizeToFile; j < fileSize; ++i){
        file[j] = buffer[i];
        ++j;
    }

    //create file path
    char* filePath = malloc(strlen(newDir) + strlen(filename) + 4);
    strcpy(filePath, newDir);
    strcat(filePath, "/.");
    strcat(filePath, filename);
    strcat(filePath, ".");
    strcat(filePath, strPart);

    if(WriteFile(file, fileSize, filePath)){
        //success
        //printf("success\n");
        write(connfd, "Success in writing file", BUFFER_SIZE);
    }
    else{
        //error
        //printf("error\n");
        write(connfd, "ERROR: unable to write file", BUFFER_SIZE);
    }
    free(filePath);
    free(newDir);
    free(file);

    return 0;
}

int List(struct Info* info, int connfd){

    //parse command
    char* username = strtok(NULL, " ");
    char* password = strtok(NULL, "\0");

    //check if user exists
    if(!UserExists(username, password, info)){
        printf("User does not exist\n");
        //must send error back to client
        write(connfd, "ERROR: Invalid Username/Password. Please try again.", BUFFER_SIZE);
        return -1;
    }

    

    //create new path directory name with username
    char* newDir = malloc(strlen(username) + strlen(info->folder));
    strcpy(newDir, info->folder);
    strcat(newDir, "/");
    strcat(newDir, username);

    //generate list
    DIR* d;
    struct dirent* dir;
    d = opendir(newDir);
    char ls[BUFFER_SIZE];
    if(d){
        while((dir = readdir(d)) != NULL){
            strcat(ls, dir->d_name);
            strcat(ls, "\n");
        }
        closedir(d);
    }
    else{
        strcpy(ls, "Error\n");
    }

    //send list back
    write(connfd, ls, BUFFER_SIZE);

    return 0;
}

int main(int argc, char** argv){

    if(argc != 3){
        fprintf(stderr, "usage: %s <folder path> <port>\n", argv[0]);
	    exit(0);
    }

    int port = atoi(argv[2]);

    char* folder = strtok(argv[1], "/");
    struct Info* info = ParseConfig(folder);
    if(info == NULL){
        fprintf(stderr, "error loading config file from %s\n", argv[1]);
        exit(0);
    }
    

    int listenfd;

    int clientlen = sizeof(struct sockaddr_in);

    struct sockaddr_in clientaddr;
    pthread_t tid;

    listenfd = OpenListenFD(port);
    
    while(1){
        int* connfdp = malloc(sizeof(int));
        *connfdp = accept(listenfd, (struct sockaddr*)&clientaddr, &clientlen);

        //clone info
        struct Info* newInfo = CloneInfo(info);

        newInfo->connfdp = connfdp;

        //make thread
        pthread_create(&tid, NULL, Thread, newInfo);
    }
    
    return 0;
}


void* Thread(void* vargp){
    struct Info* info = (struct Info*)vargp;
    int connfd = *info->connfdp;

    pthread_detach(pthread_self());
    free(info->connfdp);
    info->connfdp = NULL;

    HandlerConnection(connfd, info);

    close(connfd);
    FreeInfo(info);

    return NULL;
}

//put filename username password part filesize file
void HandlerConnection(int connfd, struct Info* info){

    

    char buffer[BUFFER_SIZE];

    size_t n = read(connfd, buffer, BUFFER_SIZE);

    char* command = strtok(buffer, " ");

    if(strcmp(command, "get") == 0){
        Get(info, connfd);
    }
    else if(strcmp(command, "put") == 0){
        Put(info, buffer, connfd);
    }
    else if(strcmp(command, "list") == 0){
        List(info, connfd);
    }
    else{
        printf("this should not happen\n");
    }
}


//open a socket file descriptor
int OpenListenFD(int port){
    int listenfd;
    int optval = 1;
    
    struct sockaddr_in serveraddr;
    
    //create socket descriptor
    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        return -1;
    }

    /*eliminate "address alread in use" error from bind(does not fully fix it tho lookup 
    SO_REUSEADDR if there is a problem)
    */
    if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int)) < 0){
        return -1;
    }

    //set up port
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET; 
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    serveraddr.sin_port = htons((unsigned short)port);

    //bind
    if(bind(listenfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) < 0){
        return -1;
    }

    //make it a listening socket ready to accept connection requests
    if(listen(listenfd, LISTENQ) < 0){
        return -1;
    }

    return listenfd;

}



bool UserExists(char* username, char* password, struct Info* info){
    for(int i = 0; i < info->userInfoSize; ++i){
        if(strcmp(info->userInfos[i]->username, username) == 0 && strcmp(info->userInfos[i]->password, password) == 0){
            return true;
        }
    }
    return false;
}


struct Info* ParseConfig(char* folder){

    

    char* filePath = malloc(strlen(folder) + strlen("/dfs.conf") - 1);
    strcpy(filePath, folder);
    strcat(filePath, "/dfs.conf");
    //printf("New path: %s\n", filePath);

    FILE* fp = fopen(filePath, "r");
    if(fp == NULL){
        printf("Could not open file: %s\n", filePath);
        return NULL;
    }

    char line[512];
    int lines = 0;
    while(fgets(line, 512, fp)){
        ++lines;
    }


    struct Info* info = CreateInfo(lines);
    rewind(fp);

    lines = 0;
    while(fgets(line, 512, fp)){
        char* cLine = strtok(line, "\n\r");
        
        char* username = strtok(cLine, " ");
        info->userInfos[lines]->username = malloc(strlen(username));
        strcpy(info->userInfos[lines]->username, username);


        char* password = strtok(NULL, "\0");
        info->userInfos[lines]->password = malloc(strlen(password));
        strcpy(info->userInfos[lines]->password, password);


        ++lines;
    }

    info->folder = malloc(strlen(folder));
    strcpy(info->folder, folder);

    return info;
}





struct UserInfo* CreateUserInfo(){
    struct UserInfo* user = malloc(sizeof(struct UserInfo));

    user->password = NULL;
    user->username = NULL;

    return user;
}

struct UserInfo* CloneUserInfo(struct UserInfo* user){
    struct UserInfo* newUser = malloc(sizeof(struct UserInfo));

    newUser->username = malloc(strlen(user->username));
    strcpy(newUser->username, user->username);

    newUser->password = malloc(strlen(user->password));
    strcpy(newUser->password, user->password);

    return newUser;
}


struct Info* CreateInfo(int size){
    struct Info* info = malloc(sizeof(struct Info));

    info->connfdp = NULL;
    info->folder = NULL;

    info->userInfoSize = size;
    info->userInfos = malloc(size * sizeof(struct UserInfo*));
    
    for(int i = 0; i < size; ++i){
        info->userInfos[i] = CreateUserInfo();
    }

    return info;
}


struct Info* CloneInfo(struct Info* info){
    struct Info* newInfo = malloc(sizeof(struct Info));

    newInfo->connfdp = NULL;

    newInfo->folder = malloc(strlen(info->folder));
    strcpy(newInfo->folder, info->folder);

    newInfo->userInfos = malloc(info->userInfoSize * sizeof(struct UserInfo*));

    for(int i = 0; i < info->userInfoSize; ++i){
        newInfo->userInfos[i] = CloneUserInfo(info->userInfos[i]);
    }

    newInfo->userInfoSize = info->userInfoSize;

    return newInfo;
}

void FreeUserInfo(struct UserInfo* userInfo){
    
    if(userInfo == NULL){
        return;
    }
    
    if(userInfo->password != NULL){
        free(userInfo->password);
    }
    
    if(userInfo->username != NULL){
        free(userInfo->username);
    }
    free(userInfo);
}

void FreeInfo(struct Info* info){
    if(info == NULL){
        return;
    }

    if(info->folder != NULL){
        free(info->folder);
    }
    for(int i = 0; i < info->userInfoSize; ++i){
        FreeUserInfo(info->userInfos[i]);
    }
    
    free(info);
}