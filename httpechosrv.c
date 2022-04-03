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
#define MAX_OBJ_SIZE 104900

/*globals*/
static volatile int keep_running = 1;
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_conn_hdr = "Proxy-Connection: close\r\n";

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
    char request_method[5];
    char request_uri[120];
    char request_ver[10];
    char buf[MAXBUF];
    struct uri_info serv_info;
    char new_request[MAXBUF];
    char response[1<<15];


    read(connfd, buf, MAXLINE);

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
    strcat(new_request, user_agent_hdr);
    strcat(new_request, accept_hdr);
    strcat(new_request, accept_encoding_hdr);
    strcat(new_request, connection_hdr);
    strcat(new_request, proxy_conn_hdr);
    strcat(new_request, "\r\n");

    /*Connect to host server*/
    int serv_sockfd = connect_via_name(serv_info.host, serv_info.port);
    if (serv_sockfd<0){
        //handle for unsuccessful connection to server
        char httperr[50];
        sprintf(httperr, "HTTP/1.0 400 Bad Request");
        bzero(buf, MAXBUF);
        strcpy(buf, httperr);
        write(connfd, buf, strlen(httperr));
        return;
    }

    //send the modified http request to server
    n = write(serv_sockfd, new_request, sizeof(new_request));
    bzero(response, sizeof(response));

    //receive the reply
    n = read(serv_sockfd, response, sizeof(response));

    printf("received the following respons from end server:\n%s", response);

    //forward to client
    n = write(connfd, response, sizeof(response));

    //close connection to server
    close(serv_sockfd);


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
        //handle for bad hostname
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