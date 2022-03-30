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

/*function prototypes*/
int open_listenfd(int port);
void service_http_request(int connfd);
void *thread(void *vargp);
const char * get_filename_ext(const char *filename);
void intHandler(int dummy);

/*globals*/
static volatile int keep_running = 1;

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
    char buf[MAXLINE];
    char * httpmsg="HTTP/1.1 200 Document Follows\r\nContent-Type:text/html\r\nContent-Length:32\r\n\r\n<html><h1>Hello CSCI4273 Course!</h1>";


    while(keep_running) {
        /*setup select function*/
        fd_set select_fds;
        struct timeval timeout;

        FD_ZERO(&select_fds);
        FD_SET(connfd, &select_fds);
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        //close connection after 10s timeout
        if (select(32, &select_fds, 0, 0, &timeout) == 0) {
            printf("No activity from client. Closing Connection\n");
            break;
        }

        n = read(connfd, buf, MAXLINE);
        if (!n)
            continue;
        printf("server received the following request:\n%s\n", buf);
        /*Parse request method*/
        char buf_cpy[MAXLINE];
        strcpy(buf_cpy, buf);
        char * http_body;
        http_body = strchr(buf_cpy, '\r\n');
        request_method = strtok(buf_cpy, " ");

        /*GET method*/
        if (strcmp(request_method, "GET") == 0) {

            /*parse filepath*/
            request_uri = strtok(NULL, " ");
            for (int i = 0; i < 3; i++){
                request_uri = strchr(request_uri, '/');
            }


            /*parse http version*/
            request_ver = strtok(NULL, "\r\n");
//            request_ver[strcspn(request_ver, "\r\n")] = 0;    //remove trailing newline

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

            //if client tries to connect via IP and port
            if (hostport){
                char hostport_copy[50];
                strcpy(hostport_copy, hostport);
                *hostport = '/0';
                printf("host name: %s\n", hostname);
                printf("port no: %s\n", hostport_copy);
                int ip_valid;
                struct in_addr ipaddr;
                struct hostent * server;
                struct sockaddr_in serveraddr;

                /* socket: create the socket */
                int sockfd = socket(AF_INET, SOCK_STREAM, 0);
                if (sockfd < 0) {
                    printf("ERROR opening socket");
                    exit(0);
                }

                ip_valid = inet_aton(hostname, &ipaddr);
                if (!ip_valid){
                    printf("IP not valid");
                    //handle for bad IPs
                    char httperr[50];
                    sprintf(httperr, "%s 400 Bad Request", request_ver);
                    bzero(buf, MAXBUF);
                    strcpy(buf, httperr);
                    write(connfd, buf, strlen(httperr));
                    continue;
                }
                /* gethostbyaddr: get the server based on IP Address*/
                server = gethostbyaddr((const void *)&ipaddr, sizeof(ipaddr), AF_INET);
                if (server == NULL) {
                    fprintf(stderr,"ERROR, no such host as %s\n", hostname);
                    //handle for bad IPs
                    char httperr[50];
                    sprintf(httperr, "%s 400 Bad Request", request_ver);
                    bzero(buf, MAXBUF);
                    strcpy(buf, httperr);
                    write(connfd, buf, strlen(httperr));
                    continue;
                }
                int portno = atoi(hostport_copy);

                /* build the server's Internet address */
                bzero((char *) &serveraddr, sizeof(serveraddr));
                serveraddr.sin_family = AF_INET;
                bcopy((char *)server->h_addr,
                      (char *)&serveraddr.sin_addr.s_addr, server->h_length);
                serveraddr.sin_port = htons(portno);

                //connect to host server
                int connected = connect(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
            }

            //if client tries to connect via name
            else{
                printf("host name: %s\n", hostname);
                int ip_valid;
                struct in_addr ipaddr;
                struct hostent * server;
                struct sockaddr_in serveraddr;

                /* socket: create the socket */
                int sockfd = socket(AF_INET, SOCK_STREAM, 0);
                if (sockfd < 0) {
                    printf("ERROR opening socket");
                    exit(0);
                }

                int portno = 80;

                /*gethostbyname*/
                server = gethostbyname(hostname);
                if (server == NULL) {
                    fprintf(stderr,"ERROR, no such host as %s\n", hostname);
                    //handle for bad IPs
                    char httperr[50];
                    sprintf(httperr, "%s 400 Bad Request", request_ver);
                    bzero(buf, MAXBUF);
                    strcpy(buf, httperr);
                    write(connfd, buf, strlen(httperr));
                    continue;
                }

                /* build the server's Internet address */
                bzero((char *) &serveraddr, sizeof(serveraddr));
                serveraddr.sin_family = AF_INET;
                bcopy((char *)server->h_addr,
                      (char *)&serveraddr.sin_addr.s_addr, server->h_length);
                serveraddr.sin_port = htons(portno);

                //connect to host server
                int connected = connect(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr));

            }

            /*parse connection*/
            ka_identifier = strtok(NULL, ": "); //get rid of "Connection: " indicator
            keep_alive = strtok(NULL, "\r\n");
            keep_alive += 1; //remove extra space at start



            if (strcmp(keep_alive, "keep-alive")==0) {
                printf("connection: %s\n", keep_alive);
                continue;
            }
            else{
                printf("No activity from client. Closing Connection\n");
                break;
            }
        }

        else{
            //handle for methods other than GET
            char httperr[50];
            sprintf(httperr, "%s 400 Bad Request", request_ver);
            bzero(buf, MAXBUF);
            strcpy(buf, httperr);
            write(connfd, buf, strlen(httperr));
            continue;
        }

//        strcpy(buf, httpmsg);
//        printf("server returning a http message with the following content.\n%s\n", buf);
//        write(connfd, buf, strlen(httpmsg));
        break;
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