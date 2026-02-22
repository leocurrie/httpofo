/* webserver.c - HTTP file server for Atari Portfolio */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
#include "network.h"

#define HTTP_PORT 80

/*============================================================================
 * HTTP Handling
 *============================================================================*/

unsigned short http_requests = 0;

/* File read buffer */
unsigned char file_buf[64];

/* HTTP response templates */
char http_200[] = "HTTP/1.0 200 OK\r\nContent-Type: ";
char http_404[] = "HTTP/1.0 404 Not Found\r\nContent-Type: text/html\r\n\r\n"
                  "<html><body><h1>404 Not Found</h1></body></html>";
char http_crlf[] = "\r\n\r\n";

/* MIME types */
char mime_html[] = "text/html";
char mime_text[] = "text/plain";
char mime_jpeg[] = "image/jpeg";
char mime_gif[]  = "image/gif";
char mime_bin[]  = "application/octet-stream";

/* Directory listing HTML */
char dir_header[] = "<html><head><title>Directory</title></head><body><h1>Index of ";
char dir_mid[] = "</h1><hr><pre>\n";
char dir_parent[] = "<a href=\"..\">..</a> (parent directory)\n";
char dir_footer[] = "</pre><hr></body></html>";

/* Get MIME type from filename extension */
char *get_mime_type(char *filename) {
    char *dot;

    dot = strrchr(filename, '.');
    if (dot == NULL) {
        return mime_bin;
    }
    dot++;

    if (stricmp(dot, "htm") == 0 || stricmp(dot, "html") == 0) {
        return mime_html;
    }
    if (stricmp(dot, "txt") == 0) {
        return mime_text;
    }
    if (stricmp(dot, "jpg") == 0 || stricmp(dot, "jpeg") == 0) {
        return mime_jpeg;
    }
    if (stricmp(dot, "gif") == 0) {
        return mime_gif;
    }

    return mime_bin;
}

/* Parse request path from HTTP request */
unsigned char parse_request(char *request, char *path, unsigned char path_size) {
    char *p, *end;
    unsigned char len;

    if (strncmp(request, "GET ", 4) != 0) {
        return 0;
    }

    p = request + 4;

    end = strchr(p, ' ');
    if (end == NULL) {
        return 0;
    }

    len = (unsigned char)(end - p);
    if (len >= path_size) {
        len = path_size - 1;
    }

    strncpy(path, p, len);
    path[len] = '\0';

    return 1;
}

/* Convert URL path to DOS filename */
void url_to_filename(char *url_path, char *filename, unsigned char size) {
    unsigned char i, j;

    /* Root directory */
    if (strcmp(url_path, "/") == 0) {
        filename[0] = '.';
        filename[1] = '\0';
        return;
    }

    /* Skip leading slash and convert / to \ */
    j = 0;
    for (i = 0; url_path[i] != '\0' && j < size - 1; i++) {
        if (url_path[i] == '/') {
            if (i > 0) {
                filename[j++] = '\\';
            }
        } else {
            filename[j++] = url_path[i];
        }
    }
    filename[j] = '\0';

    /* Remove trailing backslash if present */
    if (j > 0 && filename[j-1] == '\\') {
        filename[j-1] = '\0';
    }
}

/* Check if path is a directory */
unsigned char is_directory(char *path) {
    struct find_t fileinfo;

    if (_dos_findfirst(path, _A_SUBDIR, &fileinfo) == 0) {
        return (fileinfo.attrib & _A_SUBDIR) != 0;
    }
    return 0;
}

/* Send a file as HTTP response */
void send_file(char *filename) {
    FILE *fp;
    char *mime;
    size_t bytes_read;
    unsigned long total = 0;

    fp = fopen(filename, "rb");
    if (fp == NULL) {
        printf("  404: %s\n", filename);
        tcp_send((unsigned char *)http_404, sizeof(http_404) - 1);
        tcp_close();
        return;
    }

    printf("  200: %s\n", filename);

    mime = get_mime_type(filename);

    tcp_send((unsigned char *)http_200, sizeof(http_200) - 1);
    tcp_send((unsigned char *)mime, strlen(mime));
    tcp_send((unsigned char *)http_crlf, sizeof(http_crlf) - 1);

    while ((bytes_read = fread(file_buf, 1, sizeof(file_buf), fp)) > 0) {
        tcp_send(file_buf, (unsigned char)bytes_read);
        total += bytes_read;
    }

    fclose(fp);

    printf("  Sent %lu bytes\n", total);

    tcp_close();
}

/* Send directory listing as HTTP response */
void send_directory(char *dirname, char *url_path) {
    struct find_t fileinfo;
    char searchpath[80];
    char linkbuf[128];
    unsigned short count = 0;

    printf("  DIR: %s\n", dirname);

    /* Send HTTP header */
    tcp_send((unsigned char *)http_200, sizeof(http_200) - 1);
    tcp_send((unsigned char *)mime_html, sizeof(mime_html) - 1);
    tcp_send((unsigned char *)http_crlf, sizeof(http_crlf) - 1);

    /* Send HTML header */
    tcp_send((unsigned char *)dir_header, sizeof(dir_header) - 1);
    tcp_send((unsigned char *)url_path, strlen(url_path));
    tcp_send((unsigned char *)dir_mid, sizeof(dir_mid) - 1);

    /* Parent directory link (if not root) */
    if (strcmp(url_path, "/") != 0) {
        tcp_send((unsigned char *)dir_parent, sizeof(dir_parent) - 1);
    }

    /* Build search path */
    if (strcmp(dirname, ".") == 0) {
        strcpy(searchpath, "*.*");
    } else {
        strcpy(searchpath, dirname);
        strcat(searchpath, "\\*.*");
    }

    /* Find files */
    if (_dos_findfirst(searchpath, _A_NORMAL | _A_SUBDIR, &fileinfo) == 0) {
        do {
            /* Skip . and .. */
            if (fileinfo.name[0] == '.') {
                continue;
            }

            /* Format: <a href="name">name</a>  SIZE  or  (dir) */
            if (fileinfo.attrib & _A_SUBDIR) {
                sprintf(linkbuf, "<a href=\"%s/\">%s/</a>\t\t(dir)\n",
                        fileinfo.name, fileinfo.name);
            } else {
                sprintf(linkbuf, "<a href=\"%s\">%s</a>\t\t%lu\n",
                        fileinfo.name, fileinfo.name, fileinfo.size);
            }
            tcp_send((unsigned char *)linkbuf, strlen(linkbuf));
            count++;
        } while (_dos_findnext(&fileinfo) == 0);
    }

    /* Send HTML footer */
    tcp_send((unsigned char *)dir_footer, sizeof(dir_footer) - 1);

    printf("  Listed %u entries\n", count);

    tcp_close();
}

/* Handle a request - file or directory */
void handle_request(char *url_path) {
    char filename[64];
    char indexpath[80];

    url_to_filename(url_path, filename, sizeof(filename));

    /* Check if it's a directory */
    if (is_directory(filename) || strcmp(filename, ".") == 0) {
        /* Try index.htm first */
        if (strcmp(filename, ".") == 0) {
            strcpy(indexpath, "index.htm");
        } else {
            strcpy(indexpath, filename);
            strcat(indexpath, "\\index.htm");
        }

        /* Check if index.htm exists */
        if (fopen(indexpath, "rb") != NULL) {
            fclose(fopen(indexpath, "rb"));
            send_file(indexpath);
        } else {
            /* Send directory listing */
            send_directory(filename, url_path);
        }
    } else {
        /* Regular file */
        send_file(filename);
    }
}

/* HTTP request buffer */
unsigned char http_req[1024];
unsigned short http_req_len = 0;

/* Path buffer */
char url_path[64];

/* Process incoming HTTP data */
void http_process(unsigned char *data, unsigned short len) {
    unsigned short i;

    for (i = 0; i < len && http_req_len < sizeof(http_req) - 1; i++) {
        http_req[http_req_len++] = data[i];
    }
    http_req[http_req_len] = '\0';

    /* Check for complete request */
    if (http_req_len >= 4 &&
        http_req[http_req_len - 4] == '\r' &&
        http_req[http_req_len - 3] == '\n' &&
        http_req[http_req_len - 2] == '\r' &&
        http_req[http_req_len - 1] == '\n') {

        http_requests++;

        if (parse_request((char *)http_req, url_path, sizeof(url_path))) {
            printf("#%u GET %s\n", http_requests, url_path);
            handle_request(url_path);
        } else {
            printf("#%u Bad request\n", http_requests);
            tcp_send((unsigned char *)http_404, sizeof(http_404) - 1);
            tcp_close();
        }

        http_req_len = 0;
    }
}

/*============================================================================
 * Network Callbacks
 *============================================================================*/

void app_tcp_data_received(unsigned char *data, unsigned short len) {
    http_process(data, len);
}

void app_tcp_state_changed(unsigned char old_state, unsigned char new_state,
                           unsigned long remote_ip, unsigned short remote_port) {
    (void)old_state;

    if (new_state == TCP_STATE_SYN_RECEIVED) {
        printf("[SYN from ");
        print_ip(remote_ip);
        printf(":%u]\n", remote_port);
    } else if (new_state == TCP_STATE_ESTABLISHED) {
        printf("[Connected]\n");
    } else if (new_state == TCP_STATE_LISTEN) {
        printf("[Closed]\n");
        http_req_len = 0;
    }
}

unsigned char app_tcp_accept(unsigned long remote_ip, unsigned short remote_port) {
    (void)remote_ip;
    (void)remote_port;
    return 1;
}

/*============================================================================
 * Main
 *============================================================================*/

int main(int argc, char *argv[]) {
    int key;

    (void)argc;
    (void)argv;

    printf("Portfolio File Server\n");
    printf("Listening on ");
    print_ip(LOCAL_IP);
    printf(":%u\n", HTTP_PORT);
    printf("Serving from current directory\n");
    printf("Ctrl+Q to quit\n\n");

    init_serial();

    tcp_listen(HTTP_PORT);

    for (;;) {
        if (slip_poll()) {
            ip_receive(pkt_buf, pkt_len);
            pkt_len = 0;
        }

        tcp_check_retransmit();

        if (kbhit()) {
            key = getch();

            if (key == 0x11) {
                if (tcp_state == TCP_STATE_ESTABLISHED) {
                    tcp_close();
                }
                break;
            }
        }
    }

    cleanup_serial();
    printf("\nBye!\n");

    return 0;
}
