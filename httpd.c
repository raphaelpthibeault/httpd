#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define LISTENADDR "127.0.0.1"

/* structs */
struct sHttpRequest {
    char method[8];
    char url[128];
};

typedef struct sHttpRequest httpreq;
 

/* global */
char *error;

/*
 * returns 0 on error, or returns a socket fd
 */
int srv_init(int portno) {
    int s;
    struct sockaddr_in srv;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        error = "socket() error\n";
        return 0;
    }
    
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = inet_addr(LISTENADDR);
    srv.sin_port = htons(portno);

    if (bind(s, (struct sockaddr *)&srv, sizeof(srv)) > 0) {
        close(s);
        error = "bind() error\n";
        return 0;
    }

    if (listen(s, 5)) {
        close(s);
        error = "listen() error\n";
        return 0;
    }

    return s;
}

/*
 * returns 0 on error or returns new client socket fd
 */
int cli_accept(int s) {
    int c;
    socklen_t addrlen;
    struct sockaddr_in cli;
    
    addrlen = 0;
    memset(&cli, 0, sizeof(cli));
    c = accept(s, (struct sockaddr *)&cli, &addrlen);
    if (c < 0) {
        error = "accept() error\n";
        return 0;
    }

    return c;
}

/*
 * returns 0 on error, or httpreq struct 
 * */
httpreq *http_parse(char *str) {
    httpreq *req;
    char *p;
    
    req = malloc(sizeof(httpreq));

    for (p = str; *p && *p != ' '; ++p);
    if (*p == ' ') {
        *p = 0; // change to nullbyte
    }
    else {
        error = "parse_http() NOSPACE error\n";
        free(req);
        return 0;
    }
    strncpy(req->method, str, 7); // 8 - nullbyte

    for (str = ++p; *p && *p != ' '; ++p);
    if (*p == ' ') {
        *p = 0; // change to nullbyte
    }
    else {
        error = "parse_http() 2ND NOSPACE error\n";
        free(req);
        return 0;
    }
    strncpy(req->url, str, 127); // 128 - nullbyte


    return req;
}

/*
 * return 0 on error, or return the data 
 * */
char *cli_read(int c) {
    static char buf[512];
    memset(buf, 0, 512);

    if (read(c, buf, 511) < 0) {
        error = "read() error\n";
        return 0;
    }

    return buf;
}


void cli_conn(int s, int c) {
    httpreq *req;
    char *p;

    p = cli_read(c);
    if (!p) {
        fprintf(stderr, "%s\n", error);
        close(c);
        return;
    }

    req = http_parse(p);
    if (!req) {
        fprintf(stderr, "%s\n", error);
        close(c);
        return;
    }

    printf("'%s'\n'%s'\n", req->method, req->url);
    free(req);
    close(c);
    return;
}

int main(int argc, char **argv) {
    int s, c;
    char *port;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <listening port>\n", argv[0]);
        return -1; 
    } else {
        port = argv[1];
    }

    s = srv_init(atoi(port));
    if (!s) {
        fprintf(stderr, "%s\n", error);
        return -1;
    }
    
    printf("Listening on %s:%s\n", LISTENADDR, port);
    while (1) {
        c = cli_accept(s);
        if (!c) {
            fprintf(stderr, "%s\n", error);
            continue;
        }

        printf("Incoming connection\n");

        if (!fork()) {
            cli_conn(s, c);
        } 

    }

    return -1;
}
