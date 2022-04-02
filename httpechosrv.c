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

#define MAXLINE  8192  /* max text line length */
#define MAXBUF   8192  /* max I/O buffer size */
#define LISTENQ  1024  /* second argument to listen() */
#define MAX_CACHE_SIZE 2098000
#define MAX_OBJ_SIZE 204800

/*globals*/
static volatile int keep_running = 1;

/*structs*/
struct uri_info{
    char host[100];
    char path[300];
    int port;
};

/*function prototypes*/
int open_listenfd(int port);
void service_http_request(int connfd);
void *thread(void *vargp);
const char * get_filename_ext(const char *filename);
void intHandler(int dummy);
int connect_via_ip(char * ip, int port);
int connect_via_name(char * hostname, int port);
void parse_uri(char * uri, struct uri_info * server_info);
void parse_hdr_info(char * hdr_line, char * data, int * host_provided);

int main(int argc, char **argv) 
{
    setbuf(stdout, 0);
    int listenfd, *connfdp, port, clientlen=sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
    pthread_t tid; 

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    port = atoi(argv[1]);

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
    size_t n;
    char * request_method;
    char * request_uri;
    char *request_ver;
    char * hostname;
    char * host_identifier;
    char * ka_identifier;
    char * keep_alive;
    char buf[MAXBUF];
    struct uri_info serv_info;
    char new_request[MAXBUF];


    n = read(connfd, buf, MAXLINE);

    printf("server received the following request:\n%s\n", buf);
    /*Parse first line info*/
    char * first_line;
    first_line = strtok(buf, "\r\n");
    sscanf(first_line, "%s %s %s", request_method, request_uri, request_ver);
    if (strcasecmp(request_method, "GET")!=0){
        //handle for methods other than GET
        char httperr[50];
        sprintf(httperr, "%s 400 Bad Request", request_ver);
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
    while (strcmp(hdr_ln, "\r\n") != 0){
        if (hdr_ln == NULL)
            break;
        parse_hdr_info(hdr_ln, hdr_data, &host_info_provided);
        hdr_ln = strtok(NULL, "\r\n");
    }

    //Generate a new modified HTTP request to forward to the server
    sprintf(new_request, "GET %s %s\r\n", serv_info.path, request_ver);

    //append host info if available
    if (host_info_provided){
        strcat(new_request, hdr_data);
        strcat(new_request, "\r\n");
    }

    else{
        sprintf(new_request, "%sHost: %s\r\n", new_request, serv_info.host);
    }

    /*GET method*/
    if (strcmp(request_method, "GET") == 0) {

        printf("request method: %s\n", request_method);
        printf("request uri: %s\n", request_uri);
        printf("request ver: %s\n", request_ver);

        char http_new_firstline[100];
        sprintf(http_new_firstline, "%s %s %s\r\n", request_method, request_uri, request_ver);
        strcat(http_new_firstline, http_body);

        /*parse host*/
        host_identifier = strtok(NULL, ": "); //get rid of "Host: " indicator
        hostname = strtok(NULL, "\r\n");
        hostname += 1; //remove extra space at start
        char * hostport = strchr(hostname, ':');

        int sockfd;
        //client tries to connect via name
        sockfd = connect_via_name();

        /*forward http req to end server*/
        strcpy(buf, http_new_firstline);
        write(sockfd, buf, sizeof(buf));

        /*parse connection*/
        ka_identifier = strtok(NULL, ": "); //get rid of "Connection: " indicator
        keep_alive = strtok(NULL, "\r\n");
        keep_alive += 1; //remove extra space at start

    }

    else{
        //handle for methods other than GET
        char httperr[50];
        sprintf(httperr, "%s 400 Bad Request", request_ver);
        bzero(buf, MAXBUF);
        strcpy(buf, httperr);
        write(connfd, buf, strlen(httperr));
    }
    /*shutdown server gracefully*/
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

/*function to determine file extension*/
const char * get_filename_ext(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if(!dot || dot == filename) return "";
    return dot + 1;
}

/*signal handler (ctrl+c)*/
void intHandler(int dummy) {
    keep_running = 0;
    printf("\nWEB SERVER SHUTDOWN\n");
    exit(0);
}

/*connect to server via IP*/
int connect_via_ip(char * ip, int port){
    if (!port)
        port = 80;

    int ip_valid;
    struct in_addr ipaddr;
    struct hostent * server;
    struct sockaddr_in serveraddr;

    /* socket: create the socket */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        printf("ERROR opening socket");
        return -1;
    }

    ip_valid = inet_aton(ip, &ipaddr);
    if (!ip_valid){
        printf("IP not valid");
        //handle for bad IPs
        return -1;
    }

    /* gethostbyaddr: get the server based on IP Address*/
    server = gethostbyaddr((const void *)&ipaddr, sizeof(ipaddr), AF_INET);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host as %s\n", ip);
        //handle for bad IPs
        return -1;
    }

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
        //handle for bad IPs
        return -1;
    }

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