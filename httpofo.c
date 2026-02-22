/* webserver.c - HTTP file server for Atari Portfolio */

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

/* Document root path */
char doc_root[64] = ".";

/* PUT upload enabled flag */
unsigned char allow_put = 0;

/* PUT upload state */
unsigned char put_in_progress = 0;
unsigned long put_content_length = 0;
unsigned long put_bytes_received = 0;
int put_file = -1;

/* HTTP response templates */
char http_200[] = "HTTP/1.0 200 OK\r\nContent-Type: ";
char http_404[] = "HTTP/1.0 404 Not Found\r\nContent-Type: text/html\r\n\r\n"
                  "<html><body><h1>404 Not Found</h1></body></html>";
char http_405[] = "HTTP/1.0 405 Method Not Allowed\r\n\r\n";
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

/* Parse Content-Length header */
unsigned long parse_content_length(char *headers) {
    char *p;
    unsigned long len = 0;

    p = strstr(headers, "Content-Length:");
    if (p == NULL) {
        p = strstr(headers, "content-length:");
    }
    if (p == NULL) {
        return 0;
    }

    p += 15; /* Skip "Content-Length:" */
    while (*p == ' ') p++; /* Skip spaces */

    while (*p >= '0' && *p <= '9') {
        len = len * 10 + (*p - '0');
        p++;
    }

    return len;
}

/* Parse request path from HTTP request - returns method (1=GET, 2=PUT) */
unsigned char parse_request(char *request, char *path, unsigned char path_size) {
    char *p, *end;
    unsigned char len;
    unsigned char method;

    if (strncmp(request, "GET ", 4) == 0) {
        method = 1;
        p = request + 4;
    } else if (strncmp(request, "PUT ", 4) == 0) {
        method = 2;
        p = request + 4;
    } else {
        return 0;
    }

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

    return method;
}

/* Convert URL path to DOS filename */
void url_to_filename(char *url_path, char *filename, unsigned char size) {
    unsigned char i, j;

    /* Start with document root */
    j = 0;
    for (i = 0; doc_root[i] != '\0' && j < size - 1; i++) {
        filename[j++] = doc_root[i];
    }

    /* Root directory - just use doc_root as-is */
    if (strcmp(url_path, "/") == 0) {
        filename[j] = '\0';
        return;
    }

    /* Add separator if doc_root doesn't end with one */
    if (j > 0 && filename[j-1] != '\\' && filename[j-1] != '/' && j < size - 1) {
        filename[j++] = '\\';
    }

    /* Skip leading slash and convert / to \ */
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
    int fh;
    char *mime;
    unsigned int bytes_read;

    if (_dos_open(filename, 0, &fh) != 0) {
        tcp_send((unsigned char *)http_404, sizeof(http_404) - 1);
        tcp_close();
        return;
    }

    mime = get_mime_type(filename);

    tcp_send((unsigned char *)http_200, sizeof(http_200) - 1);
    tcp_send((unsigned char *)mime, strlen(mime));
    tcp_send((unsigned char *)http_crlf, sizeof(http_crlf) - 1);

    while (_dos_read(fh, file_buf, sizeof(file_buf), &bytes_read) == 0 && bytes_read > 0) {
        tcp_send(file_buf, (unsigned char)bytes_read);
    }

    _dos_close(fh);
    tcp_close();
}

/* Send unsigned long as ASCII over TCP */
void tcp_send_ulong(unsigned long n) {
    char buf[11];
    unsigned char i = 0;
    if (n == 0) { tcp_send((unsigned char *)"0", 1); return; }
    while (n > 0) { buf[i++] = '0' + (unsigned char)(n % 10); n /= 10; }
    while (i > 0) { --i; tcp_send((unsigned char *)&buf[i], 1); }
}

/* Send directory listing as HTTP response */
void send_directory(char *dirname, char *url_path) {
    struct find_t fileinfo;
    char searchpath[80];
    char *name;

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

            name = fileinfo.name;
            tcp_send((unsigned char *)"<a href=\"", 9);
            tcp_send((unsigned char *)name, strlen(name));
            if (fileinfo.attrib & _A_SUBDIR) {
                tcp_send((unsigned char *)"/\">", 3);
                tcp_send((unsigned char *)name, strlen(name));
                tcp_send((unsigned char *)"/</a>\t\t(dir)\n", 13);
            } else {
                tcp_send((unsigned char *)"\">", 2);
                tcp_send((unsigned char *)name, strlen(name));
                tcp_send((unsigned char *)"</a>\t\t", 6);
                tcp_send_ulong(fileinfo.size);
                tcp_send((unsigned char *)"\n", 1);
            }
        } while (_dos_findnext(&fileinfo) == 0);
    }

    /* Send HTML footer */
    tcp_send((unsigned char *)dir_footer, sizeof(dir_footer) - 1);
    tcp_close();
}

/* Handle PUT upload */
void handle_put(char *url_path) {
    char filename[64];

    url_to_filename(url_path, filename, sizeof(filename));

    if (_dos_creat(filename, 0, &put_file) != 0) {
        tcp_send((unsigned char *)http_404, sizeof(http_404) - 1);
        tcp_close();
        put_in_progress = 0;
        return;
    }

    put_in_progress = 1;
    put_bytes_received = 0;
}

/* Handle a request - file or directory */
void handle_request(char *url_path) {
    char filename[64];
    char indexpath[80];
    int fh;

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
        if (_dos_open(indexpath, 0, &fh) == 0) {
            _dos_close(fh);
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
    unsigned char method;
    char *body_start;
    unsigned short header_len;
    unsigned short body_len;
    unsigned int nwritten;
    char success_msg[] = "HTTP/1.0 201 Created\r\n\r\n";

    /* If PUT upload in progress, write data directly to file */
    if (put_in_progress) {
        _dos_write(put_file, data, len, &nwritten);
        put_bytes_received += nwritten;

        if (put_bytes_received >= put_content_length) {
            /* Upload complete */
            _dos_close(put_file);
            put_file = -1;
            tcp_send((unsigned char *)success_msg, sizeof(success_msg) - 1);
            tcp_close();
            put_in_progress = 0;
            http_req_len = 0;
        }
        return;
    }

    /* Accumulate request headers */
    for (i = 0; i < len && http_req_len < sizeof(http_req) - 1; i++) {
        http_req[http_req_len++] = data[i];
    }
    http_req[http_req_len] = '\0';

    /* Check for complete headers (blank line) */
    if (http_req_len >= 4 &&
        http_req[http_req_len - 4] == '\r' &&
        http_req[http_req_len - 3] == '\n' &&
        http_req[http_req_len - 2] == '\r' &&
        http_req[http_req_len - 1] == '\n') {

        http_requests++;

        method = parse_request((char *)http_req, url_path, sizeof(url_path));

        if (method == 1) {
            /* GET request */
            putch('#'); print_uint(http_requests); print_str(" GET "); print_str(url_path); putch('\r'); putch('\n');
            handle_request(url_path);
            http_req_len = 0;
        } else if (method == 2) {
            /* PUT request */
            putch('#'); print_uint(http_requests); print_str(" PUT "); print_str(url_path); putch('\r'); putch('\n');
            if (!allow_put) {
                tcp_send((unsigned char *)http_405, sizeof(http_405) - 1);
                tcp_close();
                http_req_len = 0;
                return;
            }
            put_content_length = parse_content_length((char *)http_req);

            if (put_content_length == 0) {
                /* No content or no Content-Length header */
                tcp_send((unsigned char *)http_404, sizeof(http_404) - 1);
                tcp_close();
                http_req_len = 0;
                return;
            }

            handle_put(url_path);

            if (!put_in_progress) {
                /* Failed to open file */
                http_req_len = 0;
                return;
            }

            /* Check if any body data already received with headers */
            body_start = strstr((char *)http_req, "\r\n\r\n");
            if (body_start != NULL) {
                body_start += 4; /* Skip the blank line */
                header_len = (unsigned short)(body_start - (char *)http_req);
                body_len = http_req_len - header_len;

                if (body_len > 0) {
                    _dos_write(put_file, (unsigned char *)body_start, body_len, &nwritten);
                    put_bytes_received += nwritten;

                    if (put_bytes_received >= put_content_length) {
                        /* Upload already complete */
                        _dos_close(put_file);
                        put_file = -1;
                        tcp_send((unsigned char *)success_msg, sizeof(success_msg) - 1);
                        tcp_close();
                        put_in_progress = 0;
                    }
                }
            }

            http_req_len = 0;
        } else {
            putch('#'); print_uint(http_requests); print_str(" Bad request\r\n");
            tcp_send((unsigned char *)http_404, sizeof(http_404) - 1);
            tcp_close();
            http_req_len = 0;
        }
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
    (void)remote_ip;
    (void)remote_port;

    if (new_state == TCP_STATE_LISTEN) {
        http_req_len = 0;

        /* Clean up incomplete PUT upload */
        if (put_in_progress) {
            if (put_file != -1) {
                _dos_close(put_file);
                put_file = -1;
            }
            put_in_progress = 0;
        }
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
    int i;
    int posarg = 0;

    /* Parse arguments - scan for flags and positional args */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0) {
            allow_put = 1;
        } else {
            posarg++;
            if (posarg == 1) {
                local_ip = parse_ip(argv[i]);
                if (local_ip == 0) {
                    print_str("Invalid IP: "); print_str(argv[i]); putch('\r'); putch('\n');
                    print_str("Usage: httpofo [ip] [path] [-w]\r\n");
                    return 1;
                }
            } else if (posarg == 2) {
                strncpy(doc_root, argv[i], sizeof(doc_root) - 1);
                doc_root[sizeof(doc_root) - 1] = '\0';
            }
        }
    }

    print_str("Portfolio File Server\r\n");
    print_str("Listening on "); print_ip(local_ip);
    putch(':'); print_uint(HTTP_PORT); putch('\r'); putch('\n');
    print_str("Serving from "); print_str(doc_root); putch('\r'); putch('\n');
    if (allow_put) print_str("PUT enabled\r\n");
    print_str("Ctrl+Q to quit\r\n\r\n");

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
    print_str("\r\nBye!\r\n");

    return 0;
}
