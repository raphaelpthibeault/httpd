#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>

#define LISTENADDR "127.0.0.1"

/* structs */
struct sHttpRequest {
    char method[8];
    char url[128];
};

struct sFile {
    char *file_name;
    char *fc;
    int size;
};

typedef struct sHttpRequest httpreq;
typedef struct sFile File; 

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

void http_header(int c, int code) {
    char buf[512];
    int n;
    
    memset(buf, 0, 512);
    snprintf(buf, 511, 
             "HTTP/1.0 %d OK\n"
             "Server: httpd.c\n"
             "Cache-Control: no-store, no-cache, max-age=0, private\n"
             "Content-Language: en\n"
             "Expires: -1\n"
             "X-Frame-Options: SAMEORIGIN\n"
             , code);
    
    n = strlen(buf);
    write(c, buf, n);
}

void http_response(int c, char *content_type, char *data) {
    char buf[512];
    int n;
    
    n = strlen(data);
    memset(buf, 0, 512);
    snprintf(buf, 511, 
             "Content-Type: %s\n"
             "Content-Length: %d\n"
             "\n%s\n"
            , content_type, n, data);
    
    n = strlen(buf);
    write(c, buf, n);
}

/* returns 0 on error or a File struct */
File *read_file(char *file_name) {
    char buf[512];
    char *p;
    int n, x, fd;
    File *f;
    
    fd = open(file_name, O_RDONLY);
    if (fd < 0)
        return 0;
    
    f = malloc(sizeof(struct sFile));
    if (!f) {
        close(fd);
        return 0;
    }

    memset(f, 0, sizeof(struct sFile));

    f->file_name = malloc(strlen(file_name) * sizeof(char));
    memcpy(f->file_name, file_name, strlen(file_name));
    f->file_name[strlen(file_name)] = '\0';  
    
    f->fc = malloc(512);
    x = 0; /* bytes read */
    while (1) {
        memset(buf, 0, 512);
        n = read(fd, buf, 512);

        if (n <= 0) {
            if (n == -1) {
                perror("Read error");
                free(f->fc);
                free(f);
                close(fd);
                return 0;
            }
            break;
        }

        char *new_fc = realloc(f->fc, x + n);
        if (!new_fc) {
            perror("Realloc failed");
            free(f->fc);
            free(f);
            close(fd);
            return 0;
        }
        f->fc = new_fc;
        memcpy(f->fc + x, buf, n);
        x += n;
    }
    
    f->size = x;
    close(fd);
    
    return f;
}

/* 1: ok; 0: error*/
int send_file(int c, char *content_type, File *file) {
    if (!file)
        return 0;
    
    char buf[512];
    char *p;
    int n, x;

    memset(buf, 0, 512);
    snprintf(buf, 511, 
             "Content-Type: %s\n"
             "Content-Length: %d\n\n"
            , content_type, file->size);
    
    n = strlen(buf);
    write(c, buf, n);

    n = file->size; 
    p = file->fc;
    while (1) {
        x = write(c, p, (n < 512)?n:512);
        if (x < 1)
            return 0;

        n -= x;
        if (n < 1)
            break;
        
        p += x;
    }

    return 1;
}

void cli_conn(int s, int c) {
    httpreq *req;
    char *p;
    char *res;
    char str[96];
    File *f;

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
    
    if (strstr(req->url, "..")) { // "security"
        http_header(c, 403); 
        res = "Access denied!\n";
        http_response(c, "text/plain", res);
        return;
    }

    if (!strcmp(req->method, "GET") && !strncmp(req->url, "/img/", 5)) {
        memset(str, 0, 96);
        snprintf(str, 95, ".%s", req->url);
        str[95] = '\0';

        printf("opening '%s'\n", str);

        f = read_file(str);
        if (!f) {
            res = "File not found!\n";
            http_header(c, 404);
            http_response(c, "text/plain", res);
        } else {
            http_header(c, 200);
            if (!send_file(c, "image/png", f)) {
                res = "HTTP server error!\n";
                http_response(c, "text/plain", res);
            }
        }
    } else if (!strcmp(req->method, "GET") && !strcmp(req->url, "/app/webpage")) {
        res = "<html><img src=\"/img/test.png\" alt=\"image\"/></html>\n";
        http_header(c, 200);
        http_response(c, "text/html", res);
    } else {
        res = "File not found!\n";
        http_header(c, 404);
        http_response(c, "text/plain", res);
    }

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
