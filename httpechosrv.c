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
        request_method = strtok(buf_cpy, " ");

        /*GET method*/
        if (strcmp(request_method, "GET") == 0) {

            /*parse filepath*/
            request_uri = strtok(NULL, " ");
            if (strcmp(request_uri, "/")==0)
                request_uri = "index.html";
            else
                request_uri += 1;

            /*parse http version*/
            request_ver = strtok(NULL, "\r\n");
//            request_ver[strcspn(request_ver, "\r\n")] = 0;    //remove trailing newline
            //determine if file is in system
            if (access(request_uri, F_OK) == 0) {
                //file present
                //determine file type
                const char * filetype = get_filename_ext(request_uri);
                char *content_type;
                if(strcmp(filetype, "html") == 0)
                    content_type = "text/html";
                else if (strcmp(filetype, "txt")==0)
                    content_type = "text/plain";
                else if (strcmp(filetype, "png")==0)
                    content_type = "image/png";
                else if (strcmp(filetype, "gif")==0)
                    content_type = "image/gif";
                else if (strcmp(filetype, "jpg")==0)
                    content_type = "image/jpg";
                else if (strcmp(filetype, "css")==0)
                    content_type = "text/css";
                else if (strcmp(filetype, "js")==0)
                    content_type = "application/javascript";

                printf("request method: %s\n", request_method);
                printf("request uri: %s\n", request_uri);
                printf("request ver: %s\n", request_ver);
                printf("content type: %s\n", content_type);
                /*parse host*/
                host_identifier = strtok(NULL, ": "); //get rid of "Host: " indicator
                hostname = strtok(NULL, "\r\n");
                hostname += 1; //remove extra space at start
                printf("host name: %s\n", hostname);

                /*parse connection*/
                ka_identifier = strtok(NULL, ": "); //get rid of "Connection: " indicator
                keep_alive = strtok(NULL, "\r\n");
                keep_alive += 1; //remove extra space at start

                /*create and send header*/
                char header[MAXLINE];
                FILE * fp = fopen(request_uri, "r");
                fseek(fp, 0, SEEK_END);
                int size = ftell(fp);   //get size of file
                fseek(fp, 0, SEEK_SET);
                sprintf(header, "%s Document Follows\r\nContent-Type:%s\r\nContent-Length:%d\r\n\r\n", request_ver, content_type, size);
                strcpy(buf, header);
                printf("server returning a http message with the following header.\n%s\n", buf);
                write(connfd, buf, strlen(header));

                /*read file to buf*/
                while (size) {
                    bzero(buf, MAXLINE);
                    int bytes_read = fread(buf, sizeof(char), MAXLINE, fp);

                    /*send buffer to server*/
                    write(connfd, buf, bytes_read);

                    size -= bytes_read;
                }
                printf("Finished sending file\n");
                fclose(fp);
            }
            else {
                // file doesn't exist
                printf("file path: %s\n", request_uri);
                char httperr[50];
                sprintf(httperr, "%s 500 Internal Server Error", request_ver);
                bzero(buf, MAXBUF);
                strcpy(buf, httperr);
                write(connfd, buf, strlen(httperr));
                continue;
            }


            if (strcmp(keep_alive, "keep-alive")==0) {
                printf("connection: %s\n", keep_alive);
                continue;
            }
            else{
                printf("No activity from client. Closing Connection\n");
                break;
            }
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