/*
 * tcpechosrv.c - A concurrent TCP echo server using threads
 */

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
#include <signal.h>
#include <time.h>
#include <openssl/md5.h>




#define MAXLINE  8192  /* max text line length */
#define MAXBUF   8192  /* max I/O buffer size */
#define LISTENQ  1024  /* second argument to listen() */
#define MAX_CACHE_SIZE 2098000
#define MAX_OBJ_SIZE 104900

/*structs*/
struct uri_info{
    char host[100];
    char path[300];
    int port;
};

struct ip_cache{
    char hostname[100];
    char ip[25];
    pthread_rwlock_t rwlock;
    struct ip_cache *next;
};

struct web_cache{
    char uri[120];
    clock_t tick_start;
    pthread_rwlock_t rwlock;
    struct web_cache * next;
};

/*globals*/
static volatile int keep_running = 1;
pthread_rwlock_t ipcache_start_rwlock;
pthread_rwlock_t webcache_start_rwlock;
pthread_rwlock_t blacklist_rwlock;
int timeout = 0;

//head ptr for ip cache and web cache
struct ip_cache  *ipCache_start = NULL;
struct web_cache *webCache_start = NULL;


/*function prototypes*/
int open_listenfd(int port);
void service_http_request(int connfd);
void *thread(void *vargp);
void intHandler(int dummy);
int connect_via_ip(char * ip, int port);
int connect_via_name(char * hostname, int port);
void parse_uri(char * uri, struct uri_info * server_info);
void parse_hdr_info(char * hdr_line, char * data, int * host_provided);
void addto_ipcache(char * hostname, char * ip);
struct ip_cache * get_ipcache(char * hostname);
void addto_webcache(char * uri, clock_t tickstart);
struct web_cache * get_webcache(char * uri);
int check_blacklisted(char * hostname);

int main(int argc, char **argv) 
{
    setbuf(stdout, 0);
    int listenfd, *connfdp, port, clientlen=sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
    pthread_t tid;
    pthread_rwlock_init(&(webcache_start_rwlock), NULL);
    pthread_rwlock_init(&(ipcache_start_rwlock), NULL);
    pthread_rwlock_init(&(blacklist_rwlock), NULL);

    if (argc != 3) {
        fprintf(stderr, "usage: %s <port> <timeout>\n", argv[0]);
        exit(0);
    }
    port = atoi(argv[1]);
    timeout = atoi(argv[2]);

    listenfd = open_listenfd(port);
    while (1) {
        /*register signal handler*/
        signal(SIGINT, intHandler);
        connfdp = malloc(sizeof(int));
        *connfdp = accept(listenfd, (struct sockaddr*)&clientaddr, &clientlen);
        pthread_create(&tid, NULL, thread, connfdp);
        if (keep_running==0){
            exit(0);
        }
    }
}

/* thread routine */
void * thread(void * vargp) 
{  
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self()); 
    free(vargp);
    service_http_request(connfd);
    close(connfd);
    return NULL;
}

/*
 * service_http_request - service a http request and send a response accordingly
 */

void service_http_request(int connfd){
    char request_method[5];
    char request_uri[120];
    char request_ver[10];
    char buf[MAXBUF];
    struct uri_info serv_info;
    char new_request[MAXBUF];
    char response[1<<15];
    ssize_t n;


    recv(connfd, buf, MAXLINE,0);

    /*Parse first line info*/
    char * first_line;
    first_line = strtok(buf, "\r\n");
    if (!first_line)
        return;
    sscanf(first_line, "%s %s %s", request_method, request_uri, request_ver);
    if (strcasecmp(request_method, "GET")!=0){
        //handle for methods other than GET
        char httperr[50];
        sprintf(httperr, "HTTP/1.0 400 Bad Request");
        bzero(buf, MAXBUF);
        strcpy(buf, httperr);
        write(connfd, buf, strlen(httperr));
        return;
    }

    parse_uri(request_uri, &serv_info);

    /*Parse additional hdr info*/
    char * hdr_ln;
    char hdr_data[MAXLINE];
    int host_info_provided = 0;
    hdr_ln = strtok(NULL, "\r\n");
    if (hdr_ln) {
        while (strcmp(hdr_ln, "\r\n") != 0) {
            parse_hdr_info(hdr_ln, hdr_data, &host_info_provided);
            hdr_ln = strtok(NULL, "\r\n");
            if (hdr_ln == NULL)
                break;
        }
    }

    //Generate a new modified HTTP request to forward to the server
    sprintf(new_request, "GET %s HTTP/1.0\r\n", serv_info.path);

    //if no host info provided add host info to request
    if (!host_info_provided){
        sprintf(new_request, "%sHost: %s\r\n", new_request, serv_info.host);
    }

    strcat(new_request, hdr_data);
    strcat(new_request, "\r\n");

    //check if blacklisted
    int blacklist = check_blacklisted(serv_info.host);
    if (blacklist){
        //send forbidden error to client
        char httperr[50];
        sprintf(httperr, "HTTP/1.0 403 Forbidden");
        bzero(buf, MAXBUF);
        strcpy(buf, httperr);
        write(connfd, buf, strlen(httperr));
        return;
    }

    /*Connect to host server*/
    int serv_sockfd;
    struct ip_cache * ptr = get_ipcache(serv_info.host);
    if (ptr)
        serv_sockfd = connect_via_ip(ptr->ip, serv_info.port);
    else
        serv_sockfd = connect_via_name(serv_info.host, serv_info.port);

    if (serv_sockfd<0){
        //handle for unsuccessful connection to server
        char httperr[50];
        sprintf(httperr, "HTTP/1.0 404 Not Found");
        bzero(buf, MAXBUF);
        strcpy(buf, httperr);
        write(connfd, buf, strlen(httperr));
        return;
    }

    MD5_CTX c;
    unsigned char out[MD5_DIGEST_LENGTH];

    MD5_Init(&c);
    MD5_Update(&c, request_uri, strlen(request_uri));
    MD5_Final(out, &c);
    char md5string[33];
    for(int i = 0; i < 16; ++i)
        sprintf(&md5string[i*2], "%02x", (unsigned int)out[i]);

    char filename[] = "Cache/";
    strcat(filename, md5string);

    struct web_cache * webptr = get_webcache(request_uri);

    if( webptr ) { //webpage in cache
        int bytes_read;

        //send cached webpage
        /*open file and determine its size*/
        FILE * fp = fopen(filename, "r");
        fseek(fp, 0, SEEK_END);
        int size = ftell(fp);   //get size of file
        fseek(fp, 0, SEEK_SET);
        printf("sending the following CACHED response to client:\n");
        while (size) {
            bzero(response, sizeof(response));
            bytes_read = fread(response, sizeof(char), sizeof(response), fp);

            /*send buffer to server*/
            send(connfd, response, bytes_read, 0);
            printf("%s", response);
            size -= bytes_read;
        }
    }

    else { //webpage not in cache
        //send the modified http request to server
        send(serv_sockfd, new_request, sizeof(new_request), 0);

        //receive the reply
        n = recv(serv_sockfd, response, sizeof(response), 0);
        printf("sending the following response to client:\n");

        //cache the webpage
        FILE * fp = fopen(filename, "w");
//        printf("Value of errno: %d\n ", errno);

        while (n > 0) {
            printf("%s", response);
            fwrite(response, sizeof(char), n, fp);
            send(connfd, response, n, 0); //sending it to client web browser
            bzero(response, sizeof(response));
            n = recv(serv_sockfd, response, sizeof(response), 0);
        }
        fclose(fp);
        clock_t start = clock();
        addto_webcache(request_uri, start);
    }

//    printf("received the following response from end server:\n%s", response);
    //close connection to server
    close(serv_sockfd);

    //forward to client
//    write(connfd, response, sizeof(response));




    //close the connection to client
    close(connfd);
}

/* 
 * open_listenfd - open and return a listening socket on port
 * Returns -1 in case of failure 
 */
int open_listenfd(int port) 
{
    int listenfd, optval=1;
    struct sockaddr_in serveraddr;
  
    /* Create a socket descriptor */
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;

    /* Eliminates "Address already in use" error from bind. */
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
                   (const void *)&optval , sizeof(int)) < 0)
        return -1;

    /* listenfd will be an endpoint for all requests to port
       on any IP address for this host */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET; 
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    serveraddr.sin_port = htons((unsigned short)port); 
    if (bind(listenfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) < 0)
        return -1;

    /* Make it a listening socket ready to accept connection requests */
    if (listen(listenfd, LISTENQ) < 0)
        return -1;
    return listenfd;
} /* end open_listenfd */


/*signal handler (ctrl+c)*/
void intHandler(int dummy) {
    keep_running = 0;
    printf("\nWEB SERVER SHUTDOWN\n");
    exit(0);
}

/*IP caching function*/
void addto_ipcache(char * hostname, char * ip){
    pthread_rwlock_wrlock(&ipcache_start_rwlock);
    struct ip_cache * pair = malloc(sizeof(struct ip_cache));
    strcpy(pair->hostname, hostname);
    strcpy(pair->ip, ip);
    pair->next = ipCache_start;
    pthread_rwlock_init(&(pair->rwlock), NULL);
    ipCache_start = pair;
    pthread_rwlock_unlock(&ipcache_start_rwlock);
}

struct ip_cache * get_ipcache(char * hostname){
    struct ip_cache * ptr = ipCache_start;
    while (ptr){
        if(pthread_rwlock_trywrlock(&(ptr->rwlock)) == 0) {
            if (strcmp(hostname, ptr->hostname) == 0)
                return ptr;
            pthread_rwlock_unlock(&(ptr->rwlock));
        }
        ptr = ptr->next;
    }
    return NULL;
}

/*connect to server via IP*/
int connect_via_ip(char * ip, int port){
    if (!port)
        port = 80;

    struct sockaddr_in serveraddr;

    /* socket: create the socket */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        printf("ERROR opening socket");
        return -1;
    }


    /* build the server's Internet address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy(ip,
          (char *)&serveraddr.sin_addr.s_addr, sizeof(ip));
    serveraddr.sin_port = htons(port);

    //connect to host server
    int connected = connect(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
    if (connected<0)
        return -1;
    else
        return sockfd;
}

/*connect to server via name*/
int connect_via_name(char * hostname, int port){
    if (!port)
        port = 80;

    struct hostent * server;
    struct sockaddr_in serveraddr;

    /* socket: create the socket */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        printf("ERROR opening socket");
        exit(0);
    }

    /*gethostbyname*/
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host as %s\n", hostname);
        //handle for bad hostname
        return -1;
    }
    addto_ipcache(hostname, server->h_addr);

    /* build the server's Internet address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,
          (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(port);

    //connect to host server
    int connected = connect(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
    if (connected<0)
        return -1;
    else
        return sockfd;
}

void parse_uri(char * uri, struct uri_info * server_info){
    char temp[MAXLINE];

    //Extract the path to the resource
    if(strstr(uri,"http://") != NULL)
        sscanf( uri, "http://%[^/]%s", temp, server_info->path);

    else if(strstr(uri,"https://") != NULL)
        sscanf( uri, "https://%[^/]%s", temp, server_info->path);

    else
        sscanf( uri, "%[^/]%s", temp, server_info->path);



    //Extract the port number and the hostname
    if( strstr(temp, ":") != NULL)
        sscanf(temp,"%[^:]:%d", server_info->host, &server_info->port);
    else {
        strcpy(server_info->host,temp);
        server_info->port = 0;
    }

    // incase the path to resource is empty
    if(!server_info->path[0])
        strcpy(server_info->path,"/");
}

/*parse addition request hdr info from clinet after first line*/
void parse_hdr_info(char * hdr_line, char * data, int * host_provided){
    if(!strcmp(hdr_line, "\r\n"))
        return;

    if(strstr(hdr_line, "User-Agent:"))
        return;

    if(strstr(hdr_line, "Accept:"))
        return;

    if(strstr(hdr_line, "Accept-Encoding:"))
        return;

    if(strstr(hdr_line, "Connection:"))
        return;

    if(strstr(hdr_line, "Proxy-Connection:"))
        return;

    if(strstr(hdr_line, "Host:")) {
        sprintf(data, "%s%s\r\n", data, hdr_line);
        * host_provided = 1;
        return;
    }

    sprintf(data, "%s%s\r\n", data, hdr_line);
}

/*adds uri to linked list*/
void addto_webcache(char * uri, clock_t tickstart){
    pthread_rwlock_wrlock(&webcache_start_rwlock);
    struct web_cache * pair = malloc(sizeof(struct web_cache));
    strcpy(pair->uri, uri);
    pair->tick_start = tickstart;
    pair->next = webCache_start;
    pthread_rwlock_init(&(pair->rwlock), NULL);
    webCache_start = pair;
    pthread_rwlock_unlock(&webcache_start_rwlock);
}

struct web_cache * get_webcache(char * uri){
    struct web_cache * ptr = webCache_start;
    clock_t diff;
    while (ptr){
        if(pthread_rwlock_trywrlock(&(ptr->rwlock)) == 0) {
            diff = clock() - ptr->tick_start;
            diff = diff/CLOCKS_PER_SEC;
            if (strcmp(uri, ptr->uri) == 0 && diff<timeout)
                return ptr;
            pthread_rwlock_unlock(&(ptr->rwlock));
        }
        ptr = ptr->next;
    }
    return NULL;
}

void parse_blacklisted_host(char * blacklist_uri, char * answer){
    char temp[100];

    //Extract the path to the resource
    if(strstr(blacklist_uri,"www.") != NULL)
        sscanf( blacklist_uri, "www.%s", temp);

    else if(strstr(blacklist_uri,"http://") != NULL)
        sscanf( blacklist_uri, "http://%s", temp);

    else
        sscanf( blacklist_uri, "%s", temp);

    strcpy(answer, temp);
}

int check_blacklisted(char * hostname){
    char * line = NULL;
    size_t len = 0;
    ssize_t n;
    char blacklist_host[100];
    pthread_rwlock_rdlock(&blacklist_rwlock);
    FILE * fp = fopen("blacklist.txt", "r");
    n = getline(&line, &len, fp);
    while (n >= 0){
        parse_blacklisted_host(line, blacklist_host);
        blacklist_host[strcspn(blacklist_host, "\n")] = 0; //remove trailing newline
        if (strcmp(blacklist_host, hostname) == 0){
            pthread_rwlock_unlock(&blacklist_rwlock);
            return 1;
        }
        n = getline(&line, &len, fp);
    }
    pthread_rwlock_unlock(&blacklist_rwlock);
    return 0;
}

