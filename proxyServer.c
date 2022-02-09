#define _GNU_SOURCE

#include <stdio.h>

#include <stdlib.h>

#include <string.h>

#include <unistd.h>

#include <arpa/inet.h>

#include <netinet/in.h>

#include <netdb.h>

#include <sys/socket.h>

#include <sys/stat.h>

#include <fcntl.h>

#include <ctype.h>

#define TRUE 0
#define FALSE - 1
#define Bad_Request 400
#define Forbidden 403
#define Not_Found 404
#define Server_Error 500
#define Not_Supported 501
#define LEN 1024

#include "threadpool.h"

/************ LINKED LIST ************/
typedef struct Node {
    char * address;
    int mask;
    struct Node * next;
}
        Node;

typedef struct LinkList {
    Node * first;
    Node * last;
    int size;
}
        LinkList;

typedef struct params {
    LinkList * hosts;
    LinkList * ips;
    int filter;
    int sd;
}
        params;

/**
 * Add data to new node at the end of the given link list.
 * @param link_list Link list to add data to
 * @param char* pointer to dynamically allocated data
 * @return TRUE on success, FALSE otherwise
 */
int add(LinkList * link_list, char * data, int mask) {
    Node * new_node = malloc(sizeof(Node));
    if (new_node == NULL) {
        return FALSE;
    }
    * new_node = (Node) {
            data,
            mask,
            NULL
    };

    if (link_list -> first == NULL) {
        link_list -> first = new_node;
        link_list -> last = new_node;
    } else {
        link_list -> last -> next = new_node;
        link_list -> last = new_node;
    }

    link_list -> size++;
    return TRUE;
}

/**
 * free all allocated memory from lists
 * @param linkList* hosts list of all host name
 * @param linkList* hosts list of all ip
 */
void free_lists(LinkList * hosts, LinkList * ips) {
    Node * p;
    while (hosts -> first != NULL) {
        p = hosts -> first;
        hosts -> first = hosts -> first -> next;
        free(p -> address);
        free(p);
        p = NULL;
    }
    free(hosts);
    while (ips -> first != NULL) {
        p = ips -> first;
        ips -> first = ips -> first -> next;
        free(p -> address);
        free(p);
        p = NULL;
    }
    free(ips);
}

/**
 * calculate the specific byte that need to b change
 * @param char* byte to change
 */
void calc_byte(char * b, int mask) {
    mask = mask % 8;
    int pow = 7, res = 0, sum = 2;
    for (int i = 0; i < mask; i++) {
        for (int j = 1; j < pow; j++) {
            sum *= 2;
        }
        res += sum;
        sum = 2;
        pow--;
    }
    if (res == 0) {
        res = 255;
    }
    sum = (int) strtol(b, NULL, 10);
    pow = sum & res;
    sprintf(b, "%d", pow);
}

/**
 * calculate the base ip for the mask range
 * @param char* ip the ip address
 * @param int mask the subnet mask
 * @return int m the mask as int
 */
void base_ip(char * ip, int mask) {
    char * b1 = strtok(ip, ".");
    char * b2 = strtok(NULL, ".");
    char * b3 = strtok(NULL, ".");
    char * b4 = strtok(NULL, " ");
    double place = (double) mask / 8;
    if (0 <= place && place <= 1) {
        calc_byte(b1, mask);
        b2 = "0";
        b3 = "0";
        b4 = "0";
    }
    if (1 < place && place <= 2) {
        calc_byte(b2, mask);
        b3 = "0";
        b4 = "0";
    }
    if (2 < place && place <= 3) {
        calc_byte(b3, mask);
        b4 = "0";
    }
    if (3 < place && place <= 4) {
        calc_byte(b4, mask);
    }
    sprintf(ip, "%s.%s.%s.%s", b1, b2, b3, b4);
}

/**
 * fill lists of ip and host name from filter file
 * @param FILE* fp filter file
 * @param linkList* hosts list of all host name
 * @param linkList* hosts list of all ip
 */
void fill_lists(FILE * fp, LinkList * hosts, LinkList * ips) {
    if (fseek(fp, 0, SEEK_SET) != 0) {
        perror("fseek:\n");
        exit(EXIT_FAILURE);
    }

    hosts -> first = NULL;
    hosts -> last = NULL;
    hosts -> size = 0;

    ips -> first = NULL;
    ips -> last = NULL;
    ips -> size = 0;

    char * address, * line = NULL;
    size_t len = 0;
    while (getline( & line, & len, fp) != -1) {
        address = strtok(line, "\r\n");
        ///check if ip or host name
        if (isdigit(address[0])) {
            char * temp = strtok(address, "/");
            char * ip = (char * ) malloc(sizeof(char) * (strlen(temp) + 1));
            if (ip == NULL) {
                fprintf(stdout, "malloc:\n");
                exit(EXIT_FAILURE);
            }
            memset(ip, '\0', strlen(temp) + 1);
            strcpy(ip, temp);
            char * m = strtok(NULL, " ");
            int mask = (int) strtol(m, NULL, 10);
            base_ip(ip, mask);
            if (add(ips, ip, mask) == FALSE) {
                fprintf(stdout, "malloc:\n");
                exit(EXIT_FAILURE);
            }
        } else {
            char * name = (char * ) malloc(sizeof(char) * (strlen(address) + 1));
            if (name == NULL) {
                fprintf(stdout, "malloc:\n");
                exit(EXIT_FAILURE);
            }
            memset(name, '\0', strlen(address) + 1);
            strcpy(name, address);
            if (add(hosts, name, -1) == FALSE) {
                fprintf(stdout, "malloc:\n");
                exit(EXIT_FAILURE);
            }
        }
    }
    free(line);
}

/**
 * convert a host name to ip address
 * @param char* hostname the host name
 * @param char* ip to contain the ip
 * @return int true if validate, false if not
 */
int hostname_to_ip(char * hostname, char * ip) {
    struct hostent * hp;
    hp = gethostbyname(hostname);
    if (hp == NULL) {
        herror("gethostbyname");
        return FALSE;
    }
    sprintf(ip, "%s", inet_ntoa( * ((struct in_addr * ) hp -> h_addr_list[0])));
    return TRUE;
}
/**
 * check if an address is in filter file
 * @param char* addr the address to check
 * @param linkList* hosts list of all host name
 * @param linkList* hosts list of all ip
 * @return int true if not in filter, false if in filter
 */
int search_in_filter(char * addr, LinkList * hosts, LinkList * ips) {
    Node * temp;
    char * ip = (char * ) malloc(sizeof(char) * 17);
    if(ip == NULL){
        return 10;
    }
    memset(ip, '\0', 17);
    if (!isdigit(addr[0])) {
        temp = hosts -> first;
        while (temp != NULL) {
            if (strcmp(addr, temp -> address) == 0) {
                free(ip);
                return FALSE;
            }
            temp = temp -> next;
        }
        hostname_to_ip(addr, ip);
    } else {
        strcpy(ip, addr);
    }
    temp = ips -> first;
    char * t = (char * ) malloc(sizeof(char) * 17);
    if(t == NULL){
        free(ip);
        return 10;
    }
    while (temp != NULL) {
        memset(t, '\0', 17);
        strcpy(t, ip);
        base_ip(t, temp -> mask);
        if (strcmp(t, temp -> address) == 0) {
            free(t);
            free(ip);
            return FALSE;
        }
        temp = temp -> next;
    }
    free(t);
    free(ip);
    return TRUE;
}

/**
 * build a response for error case
 * @param char* msg the string that will contain the msg
 * @param int err the number of the message (type)
 */
void error_handle(char * msg, int err) {
    memset(msg, '\0', 400);
    char * body = "<HTML><HEAD><TITLE></TITLE></HEAD>\r\n<BODY><H4></H4>\r\n.\r\n</BODY></HTML>\r\n";
    char type[30], content[30];
    memset(type, '\0', 30);
    memset(content, '\0', 30);
    if (err == Bad_Request) {
        strcpy(type, "400 Bad Request");
        strcpy(content, "Bad Request");
    }
    if (err == Forbidden) {
        strcpy(type, "403 Forbidden");
        strcpy(content, "Access denied");
    }
    if (err == Not_Found) {
        strcpy(type, "404 Not Found");
        strcpy(content, "File not found");
    }
    if (err == Server_Error) {
        strcpy(type, "500 Internal Server Error");
        strcpy(content, "Some server side error");

    }
    if (err == Not_Supported) {
        strcpy(type, "501 Not supported");
        strcpy(content, "Method is not supported");

    }
    int size = ((int) strlen(type) * 2) + (int) strlen(body) + (int) strlen(content);
    sprintf(msg,
            "HTTP/1.0 %s\r\nContent-Type: text/html\nContent-Length: %d\nConnection: close\r\n\r\n<HTML><HEAD><TITLE>%s</TITLE></HEAD>\r\n<BODY><H4>%s</H4>\r\n%s.\r\n</BODY></HTML>\r\n",
            type, size, type, type, content);
}

/**
 * check if a given text is valid number
 * @param char* text represent a string to check
 * @return int true if validate, false if not
 */
int valid_num(char * text) {
    int i;
    for (i = 0; i < (int)strlen(text); i++)
        if (text[i] < 48 || text[i] > 57)
            return FALSE;
    return TRUE;
}
/**
 * check validation of the given arguments as numbers
 * @param char* argv[] parameters from the user
 * @param int* port port number
 * @param int* pool_size size of pool
 * @param int* max_requests size of max requests
 * @return int true if validate, false if not
 */
int validate_args(char * argv[], int * port, int * pool_size, int * max_requests) {
    if (valid_num(argv[1]) == FALSE) {
        return FALSE;
    }
    * port = atoi(argv[1]);
    if ( * port < 0) {
        return FALSE;
    }

    if (valid_num(argv[2]) == FALSE) {
        return FALSE;
    }
    * pool_size = atoi(argv[2]);
    if ( * pool_size > MAXT_IN_POOL || * pool_size < 0)
        return FALSE;

    //check that the max requests number contain only digits
    if (valid_num(argv[3]) == FALSE) {
        return FALSE;
    }
    * max_requests = atoi(argv[3]);
    return TRUE;
}

/**
 * init server connection
 * @param int port port number to the server
 * @return int sd of the server, FALSE in case of function failure
 * */
int init_server(int port) {
    int sd;
    struct sockaddr_in srv;
    if ((sd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        perror("error: socket\n");
        return FALSE;
    }
    srv.sin_family = AF_INET;
    srv.sin_port = htons(port);
    srv.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sd, (struct sockaddr * ) & srv, sizeof(srv)) < 0) {
        perror("error: bind\n");
        close(sd);
        return FALSE;
    }
    if (listen(sd, 5) < 0) {
        perror("error: listen\n");
        close(sd);
        return FALSE;
    }
    return sd;
}

/**
 * send an error response to the client
 * @param int sd the client socket
 * @param int err the error type
 * */
void send_error_msg(int sd, int err) {
    char msg[400];
    error_handle(msg, err);
    write(sd, msg, strlen(msg));
}

/**
 * parsing the header for check errors
 * @param char* buf the request
 * @param ssize_t is_read the size of the buffer
 * @param int filter a flag that indicate if filter is exist
 * @param linkList* hosts list of all host name
 * @param linkList* hosts list of all ip
 * @return full path of the file if all checks where good, NULL else
 * */
char * parse_header(char ** buf, ssize_t is_read, int filter, LinkList * hosts, LinkList * ips, int sd) {
    char * temp = calloc(is_read + 1, sizeof(char));

    if (temp == NULL) {
        send_error_msg(sd, Server_Error);
        return NULL;
    }
    memset(temp, '\0', is_read + 1);
    strcpy(temp, * buf);
    char * first_line = strtok(temp, "\r\n");
    char * host_name = strtok(NULL, "\r\n\r\n");
    char * method = strtok(first_line, " ");
    char * path = strtok(NULL, " ");
    char * protocol = strtok(NULL, " \r\n");
    host_name = strcasestr(host_name, "Host:");
    if ((method == NULL) || (path == NULL) || (protocol == NULL) || (host_name == NULL)) {
        free(temp);
        send_error_msg(sd, Bad_Request);
        return NULL;
    }
    protocol = strcasestr(protocol, "HTTP/");
    if (protocol == NULL) {
        free(temp);
        send_error_msg(sd, Bad_Request);
        return NULL;
    }
    protocol = strstr(protocol, "/");
    if ((strcmp(protocol, "/1.0") != 0) && (strcmp(protocol, "/1.1") != 0)) {
        free(temp);
        send_error_msg(sd, Bad_Request);
        return NULL;
    }
    if (strcmp(method, "GET") != 0) {
        free(temp);
        send_error_msg(sd, Not_Supported);
        return NULL;
    }
    char * pass = strchr(host_name, ' ');
    if (pass != NULL) {
        pass = strtok(host_name, " ");
        pass = strtok(NULL, "\r\n");
    } else {
        pass = strtok(host_name, ":");
        pass = strtok(NULL, "\r\n");
    }
    struct hostent * hp;
    if (!isdigit(pass[0])) {
        hp = gethostbyname(pass);
        if (hp == NULL) {
            free(temp);
            send_error_msg(sd, Not_Found);
            return NULL;
        }
    } else {
        struct in_addr a;
        inet_aton(pass, & a);
        hp = gethostbyaddr( & a, sizeof(a), AF_INET);
        if (hp == NULL) {
            free(temp);
            send_error_msg(sd, Not_Found);
            return NULL;
        }
    }
    if (filter == TRUE) {
        if (search_in_filter(pass, hosts, ips) == FALSE) {
            free(temp);
            send_error_msg(sd, Forbidden);
            return NULL;
        }
    }
    char is_index[15];
    memset(is_index, '\0', 15);
    if (strcmp(path, "/") == 0 || path[strlen(path)-1] == '/') {
        sprintf(is_index, "%s", "index.html");
    }
    char * full_path = calloc(strlen(path) + strlen(pass) + strlen(is_index) + 1, sizeof(char));
    if (full_path == NULL) {
        send_error_msg(sd, Server_Error);
        free(temp);
        return NULL;
    }
    sprintf(full_path, "%s%s%s", pass, path, is_index);

    char * new_req = "GET  HTTP\r\nHost: \r\nConnection: close\r\n\r\n";
    * buf = realloc(( * buf), strlen(new_req) + strlen(path) + strlen(protocol) + strlen(pass) + 1);
    sprintf( * buf, "GET %s HTTP%s\r\nHost: %s\r\nConnection: close\r\n\r\n", path, protocol, pass);

    free(temp);
    return full_path;
}

/**
 * get the type of a file
 * @param char* name the name of the file
 * @return char* the file mine type
 */
char * get_mime_type(char * name) {
    char * ext = strrchr(name, '.');
    if (!ext) return NULL;
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".au") == 0) return "audio/basic";
    if (strcmp(ext, ".wav") == 0) return "audio/wav";
    if (strcmp(ext, ".avi") == 0) return "video/x msvideo";
    if (strcmp(ext, ".mpeg") == 0 || strcmp(ext, ".mpg") == 0) return "video/mpeg";
    if (strcmp(ext, ".mp3") == 0) return "audio/mpeg";
    return NULL;
}

/**
 * bring file from local system and sent to the client
 * @param char* full_path the path of the file
 * @param int sd the socket of the client
 */
void file_from_local_sys(char * full_path, int sd) {
    FILE * fp = fopen(full_path, "r");
    if (fp == NULL) {
        send_error_msg(sd, Server_Error);
        return;
    }
    struct stat st = {
            0
    };
    stat(full_path, & st);
    char size[20] = {
            0
    };
    sprintf(size, "%d", (int) st.st_size);
    char * path = strchr(full_path, '/');
    char * ext = get_mime_type(path);
    int ext_size;
    char temp[100];
    memset(temp, '\0', 100);
    if (ext != NULL) {
        strcpy(temp, "HTTP/1.0 200 OK\r\nContent-Length: \r\nContent-type: \r\nconnection: Close\r\n\r\n");
        ext_size = (int) strlen(ext);
    } else {
        strcpy(temp, "HTTP/1.0 200 OK\r\nContent-Length: \r\nConnection: Close\r\n\r\n");
        ext_size = 0;
    }

    char * response = malloc((int) strlen(temp) + ext_size + (int) strlen(size) + 1);
    if (response == NULL) {
        send_error_msg(sd, Server_Error);
        return;
    }
    memset(response, '\0', (int) strlen(temp) + ext_size + (int) strlen(size) + 1);
    strcat(response, "HTTP/1.0 200 OK\r\nContent-Length: ");
    strcat(response, size);
    strcat(response, "\r\n");
    if (ext_size != 0) {
        strcat(response, "Content-type: ");
        strcat(response, ext);
        strcat(response, "\r\n");
    }

    strcat(response, "Connection: close\r\n\r\n");

    write(sd, response, (int) strlen(response));
    int total = (int) strlen(response);
    free(response);
    unsigned char buf[LEN];
    int n, read = 0;
    while (read != (int) st.st_size) {
        memset(buf, '\0', LEN);
        n = (int) fread(buf, sizeof(unsigned char), LEN, fp);
        if (ferror(fp)) {
            send_error_msg(sd, Server_Error);
            return;
        }
        read += n;
        if (write(sd, buf, n) < 0) {
            send_error_msg(sd, Server_Error);
            return;
        }
    }

    total += (int) strlen(size);
    total += read;
    printf("File is given from local filesystem\n");
    printf("\n Total response bytes: %d\n", total);
    fclose(fp);
}
/**
 * create directories path to the requested file
 * @param char* full_path the path of the file (include the file name)
 * @param int sd socket of the client
 * @return int TRUE in success, FALSE else
 */
int creat_directories(char * full_path, int sd) {
    char * path = (char * ) malloc(strlen(full_path) + 1);
    if (path == NULL) {
        send_error_msg(sd, Server_Error);
        return FALSE;
    }
    strcpy(path, full_path);
    int counter = 0, i, size = 0;
    for (i = 0; i < (int)strlen(path) - 1; i++) {
        if (path[i] == '/') {
            counter++;
        }
    }

    i = 0;
    for (; size < (int)strlen(path) - 1; size++) {
        if (path[size] == '/') {
            i++;
        }
        if (i == counter) {
            break;
        }
    }
    char * directories = (char * ) calloc(size + 1, sizeof(char));
    if (directories == NULL) {
        send_error_msg(sd, Server_Error);
        return FALSE;
    }
    struct stat st = {
            0
    };
    char * token = strtok(path, "/");
    for (i = 0; i < counter; i++) {
        strcat(directories, token);
        if (stat(directories, & st) == -1) {
            if (mkdir(directories, 0700) == -1) {
                send_error_msg(sd, Server_Error);
                return FALSE;
            }
        }
        if (i + 1 != counter) {
            strcat(directories, "/");
        }
        token = strtok(NULL, "/");
    }
    free(path);
    free(directories);
    return TRUE;
}

/**
 * open a connection to the server with socket
 * @param char* name of the host
 * @param int sd socket of the client
 * @return int csd in success, FALSE else
 */
int open_connection(char * name, int sd) {
    int csd;
    struct sockaddr_in srv;
    struct hostent * hp;
    srv.sin_family = AF_INET;
    if (!isdigit(name[0])) {
        hp = gethostbyname(name);
        if (hp == NULL) {
            send_error_msg(sd, Not_Found);
            return FALSE;
        }
    } else {
        struct in_addr a;
        inet_aton(name, & a);
        hp = gethostbyaddr( & a, sizeof(a), AF_INET);
        if (hp == NULL) {
            send_error_msg(sd, Not_Found);
            return FALSE;
        }
    }
    srv.sin_addr.s_addr = ((struct in_addr * )(hp -> h_addr)) -> s_addr;
    srv.sin_port = htons(80);

    if ((csd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        send_error_msg(sd, Server_Error);
        return FALSE;

    }
    if (connect(csd, (struct sockaddr * ) & srv, sizeof(srv)) < 0) {
        send_error_msg(sd, Server_Error);
        return FALSE;
    }
    return csd;
}

/**
 * send http request to established socket, read the response, send to client and create file on the local system
 * @param char* request the request from the client
 * @param char* full_path the path of the file
 * @param sd the socket of the client
 */
void get_file_from_server(char * request, char * full_path, int sd) {
    //crate http request
    char * path = malloc((int) strlen(full_path) + 1);
    if (path == NULL) {
        send_error_msg(sd, Server_Error);
        return;
    }
    memset(path, '\0', (int) strlen(full_path) + 1);
    strcpy(path, full_path);
    char * name = strtok(path, "/");
    int csd = open_connection(name, sd);
    free(path);
    if (csd == FALSE) {
        send_error_msg(sd, Server_Error);
        return;
    }
    //write http request to the socket
    if (write(csd, request, strlen(request)) < 0) {
        send_error_msg(sd, Server_Error);
        return;
    }
    free(request);

    //read all from socket
    unsigned char buf[LEN], half_buf1[LEN / 2], half_buf2[LEN / 2];
    memset(buf, '\0', LEN);
    memset(half_buf1, '\0', LEN / 2);
    memset(half_buf2, '\0', LEN / 2);

    int is_read = 0, n, size_buf2;

    if ((n = (int) read(csd, half_buf1, LEN / 2)) < 0) {
        send_error_msg(sd, Server_Error);
        close(csd);
        return;
    }
    is_read += n;
    memcpy(buf, half_buf1, n);
    if ((n = (int) read(csd, half_buf2, LEN / 2)) < 0) {
        send_error_msg(sd, Server_Error);
        close(csd);
        return;
    }
    is_read += n;
    size_buf2 = n;
    memcpy(buf + (is_read - n), half_buf2, n);
    if (write(sd, buf, n) < 0) {
        send_error_msg(sd, Server_Error);
        close(csd);
        return;
    }
    char * stat = strstr((char * ) buf, "1.");
    int status = (int) strtol(stat + 4, NULL, 10);
    if (200 <= status && status < 300) {

        //create folders and file
        if (creat_directories(full_path, sd) == FALSE) {
            send_error_msg(sd, Server_Error);
            close(csd);
            return;
        }

        int fd = open(full_path, O_CREAT | O_WRONLY, 0644);
        if (fd < 0) {
            send_error_msg(sd, Server_Error);
            close(csd);
            return;
        }
        char * end = strstr((char * ) buf, "\r\n\r\n");
        while (end == NULL) {
            memcpy(half_buf1, half_buf2, size_buf2);
            memset(half_buf2, '\0', LEN / 2);
            memset(buf, '\0', LEN);
            if ((n = (int) read(csd, half_buf2, LEN / 2)) < 0) {
                send_error_msg(sd, Server_Error);
                close(fd);
                close(csd);
                return;
            }
            memcpy(buf, half_buf1, size_buf2);
            memcpy(buf + size_buf2, half_buf2, n);
            if (write(sd, half_buf2, n) < 0) {
                send_error_msg(sd, Server_Error);
                close(csd);
                close(fd);
                return;
            }
            size_buf2 = n;
            is_read += n;
            end = strstr((char * ) buf, "\r\n\r\n");

        }
        ///check if there is something to write to the file
        end += 4;
        char temp = * end;
        * end = '\0';
        int wr = (int) strlen((char * ) buf);
        * end = temp;
        int hmtw = LEN - wr;
        if (hmtw > 0) {
            if (write(fd, buf + wr, hmtw) < 0) {
                send_error_msg(sd, Server_Error);
                close(csd);
                close(fd);
                return;
            }
        }

        memset(buf, '\0', LEN);
        while ((n = (int) read(csd, buf, LEN)) > 0) {
            if (write(fd, buf, n) < 0) {
                send_error_msg(sd, Server_Error);
                close(csd);
                close(fd);
                return;
            }
            if (write(sd, buf, n) < 0) {
                send_error_msg(sd, Server_Error);
                close(csd);
                close(fd);
                return;
            }
            is_read += n;
            memset(buf, '\0', n);
        }
        close(fd);
    } else {

        while ((n = (int) read(csd, buf, LEN)) != 0) {
            if (write(sd, buf, n) < 0) {
                send_error_msg(sd, Server_Error);
                close(csd);
                return;
            }
            is_read += n;
        }
    }
    printf("File is given from origin server\n");
    printf("\n Total response bytes: %d\n", is_read);
    close(csd);
}

/**
 * function of thread work, deal with all work with the client
 * @param void* param struct of parameters to work with client
 * @return
 * */
int handle_client(void * param) {
    struct params p = * ((params * ) param);
    ///read from socket
    char * request = (char * ) malloc(LEN);
    if (request == NULL) {
        send_error_msg(p.sd, Server_Error);
        free(request);
        close(p.sd);
        return FALSE;
    }
    memset(request, '\0', LEN);
    ssize_t nbytes, is_read = 0;
    int len = LEN;
    char * end;
    while ((nbytes = read(p.sd, request + is_read, LEN)) != 0) {
        is_read += nbytes;
        if (nbytes < 0) {
            break;
        }
        end = strstr(request, "\r\n\r\n");
        if (end == NULL) {
            len *= 2;
            request = (char * ) realloc(request, len);
            if (request == NULL) {
                break;
            }
            memset(request + is_read, '\0', len - is_read);
        } else {
            break;
        }
    }
    if (request == NULL || nbytes < 0) {
        send_error_msg(p.sd, Server_Error);
        free(request);
        close(p.sd);
        return FALSE;
    }

    ///check if header okay
    char * full_path = parse_header( & request, is_read, p.filter, p.hosts, p.ips, p.sd);
    if (full_path == NULL) {
        free(request);
        close(p.sd);
        return FALSE;
    }
    printf("HTTP request = \n%s\nLEN = %d\n", request, (int) strlen(request));

    if (access(full_path, F_OK) == 0) { //file in system files
        file_from_local_sys(full_path, p.sd);
        free(request);
    } else { //file not in system files
        get_file_from_server(request, full_path, p.sd);

    }
    free(full_path);
    close(p.sd);
    return TRUE;
}

/**
 * handle all server work
 * @param int port port number
 * @param int pool_size size of pool
 * @param int max_requests size of max requests
 * @param int filter a flag that indicate if filter is exist
 * @param linkList* hosts list of all host name
 * @param linkList* hosts list of all ip
 * */
void server_handle(int port, int pool_size, int max_req, int filter, LinkList * hosts, LinkList * ips) {
    int welcome_sd, counter = 0, sd;
    struct sockaddr_in cli;
    unsigned int cli_len = sizeof(cli);
    welcome_sd = init_server(port);
    if (welcome_sd == FALSE) {
        if (filter == TRUE) {
            free_lists(hosts, ips);
        }
        exit(EXIT_FAILURE);
    }
    threadpool * pool = create_threadpool(pool_size);
    if (pool == NULL) {
        fprintf(stderr, "error: threadpool\n");
        if (filter == TRUE) {
            free_lists(hosts, ips);
        }
        close(welcome_sd);
        exit(EXIT_FAILURE);
    }
    params** args = calloc(max_req , sizeof(struct params*));
    if(args == NULL){
        if (filter == TRUE) {
            free_lists(hosts, ips);
        }
        close(welcome_sd);
        exit(EXIT_FAILURE);
    }
    while (counter < max_req) {
        ///accept
        sd = accept(welcome_sd, (struct sockaddr * ) & cli, & cli_len);
        if (sd < 0) {
            perror("error: accept\n");
            if (filter == TRUE) {
                free_lists(hosts, ips);
            }
            close(welcome_sd);
            exit(EXIT_FAILURE);
        }
        args[counter] = calloc(1, sizeof(params));
        if(args[counter] == NULL){
            send_error_msg(sd, Server_Error);
            close(sd);
            continue;
        }
        args[counter]->sd = sd;
        args[counter]->filter = filter;
        args[counter]->hosts = hosts;
        args[counter]->ips = ips;
        dispatch(pool, handle_client, (void * ) args[counter]);
        counter++;
    }
    destroy_threadpool(pool);
    for(int i = 0; i < max_req; i++){
        if(args[i] != NULL){
            free(args[i]);
        }
    }
    free(args);

    close(welcome_sd);
}

int main(int argc, char * argv[]) {

    ///check legacy of usage
    if (argc != 5) {
        fprintf(stdout, "Usage: proxyServer <port> <pool-size> <max-number-of-request> <filter>\n");
        exit(EXIT_FAILURE);
    }
    int port, pool_size, max_req;
    if (validate_args(argv, & port, & pool_size, & max_req) == FALSE) {
        fprintf(stdout, "Usage: proxyServer <port> <pool-size> <max-number-of-request> <filter>\n");
        exit(EXIT_FAILURE);
    }
    int filter = TRUE;

    ///create hosts list
    LinkList * hosts = (LinkList * ) calloc(1, sizeof(LinkList));
    if (hosts == NULL) {
        fprintf(stdout, "calloc:\n");
        exit(EXIT_FAILURE);
    }
    ///create ips list
    LinkList * ips = (LinkList * ) calloc(1, sizeof(LinkList));
    if (ips == NULL) {
        fprintf(stdout, "calloc:\n");
        exit(EXIT_FAILURE);
    }

    ///open filter file
    FILE * fp = fopen(argv[4], "r");
    if (fp == NULL) {
        fprintf(stdout, "fopen:\n");
        free(hosts);
        free(ips);
        exit(EXIT_FAILURE);
    }

    ///check if filter size is empty
    if (fseek(fp, 0, SEEK_END) != 0) {
        perror("error: fseek\n");
        free(hosts);
        free(ips);
        fclose(fp);
        exit(EXIT_FAILURE);
    }
    int fsize = (int) ftell(fp);

    ///file is not empty
    if (fsize != 0) {
        fill_lists(fp, hosts, ips);
    } else {
        free(hosts);
        free(ips);
        filter = FALSE;
    }
    fclose(fp);
    if (filter == TRUE) {
        server_handle(port, pool_size, max_req, filter, hosts, ips);
    } else {
        server_handle(port, pool_size, max_req, filter, NULL, NULL);
    }
    if (filter == TRUE) {
        free_lists(hosts, ips);
    }

    return 0;
}