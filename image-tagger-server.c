/*
** image-tagger-server.c
*/

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// constants
static char const * const HTTP_200_FORMAT_COOKIE = "HTTP/1.1 200 OK\r\n\
Set-cookie: username=%s\r\n\
Content-Type: text/html\r\n\
Content-Length: %ld\r\n\r\n";
static char const * const HTTP_200_FORMAT = "HTTP/1.1 200 OK\r\n\
Content-Type: text/html\r\n\
Content-Length: %ld\r\n\r\n";
static char const * const HTTP_400 = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
static int const HTTP_400_LENGTH = 47;
static char const * const HTTP_404 = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
static int const HTTP_404_LENGTH = 45;

// represents the types of method
typedef enum
{
    GET,
    POST,
    UNKNOWN
} METHOD;

typedef struct player{
    int socket_id;
    struct sockaddr_in client_addr;
    socklen_t len;
    char* username;
    bool visit;
    bool start;
    bool quit;
    bool end;
    char **keywords;
    int n_keywords;
} player;

int player_count = 0;

player player_list[2];

void init_player_list(){
  for (int i = 0; i < 2; i++){
      player_list[i].username = NULL;
      player_list[i].visit = false;
      player_list[i].start = false;
      player_list[i].quit = false;
      player_list[i].end = false;
      player_list[i].keywords = calloc(1000000,sizeof(char*));
      player_list[i].n_keywords = 0;
  }
}

int get_index(int i){
  for (int x = 0; x < player_count;x++){
    if (player_list[x].socket_id == i){
      return x;
    }
  }
  return i;
}

bool get_html(char* filename, int sockfd, char* buff){
    // get the size of the file
    struct stat st;
    stat(filename, &st);
    int n = sprintf(buff, HTTP_200_FORMAT, st.st_size);
    // send the header first
    if (write(sockfd, buff, n) < 0)
    {
        perror("write");
        return false;
    }
    // send the file
    int filefd = open(filename, O_RDONLY);
    do
    {
        n = sendfile(sockfd, filefd, NULL, 2048);
    }
    while (n > 0);
    if (n < 0)
    {
        perror("sendfile");
        close(filefd);
        return false;
    }
    close(filefd);
    return true;
}

bool insert_keyword(char *keyword, int sockfd, char* buff){
    int word_length = strlen(keyword);
    int index = get_index(sockfd);
    long added_length;

    if (player_list[index].n_keywords == 0){
        //the length needs to include the p tag enclosing the keyword
        added_length = word_length + 7;
    }
    else{
        // the length needs to include the ", " before the keyword
        added_length = word_length + 2;
    }

    // get the size of the file
    struct stat st;
    stat("html/4_accepted", &st);
    // increase file size to accommodate the username
    long size = st.st_size + added_length;
    int n = sprintf(buff, HTTP_200_FORMAT, size);
    // send the header first
    if (write(sockfd, buff, n) < 0)
    {
        perror("write");
        return false;
    }
    // read the content of the HTML file
    int filefd = open("html/4_accepted", O_RDONLY);
    n = read(filefd, buff, 2048);
    if (n < 0)
    {
        perror("read");
        close(filefd);
        return false;
    }
    close(filefd);

    if (player_list[index].n_keywords == 0){
        // move the trailing part backward
        int p1, p2;
        for (p1 = size - 1, p2 = p1 - added_length; p1 >= size - 263; --p1, --p2)
            buff[p1] = buff[p2];
        ++p2;

        //write line
        int line_size = 7+word_length;
        char line[line_size];
        snprintf(line, sizeof(line), "%s%s%s", "<p>", keyword, "</p>\n");

        strncpy(buff + p2, line, line_size);
    }
    else{
        // move the trailing part backward
        int p1, p2;
        for (p1 = size - 1, p2 = p1 - added_length; p1 >= size - 267; --p1, --p2)
            buff[p1] = buff[p2];
        ++p2;

        // put the separator
        buff[p2++] = ',';
        buff[p2++] = ' ';

        // copy the username
        strncpy(buff + p2, keyword, word_length);
    }

    if (write(sockfd, buff, size) < 0)
    {
        perror("write");
        return false;
    }

    player_list[index].keywords[player_list[index].n_keywords] = keyword;
    player_list[index].n_keywords++;
    return true;
}

bool was_submitted(char* keyword, int index){
    for (int i = 0; i < player_list[1-index].n_keywords; i++){
        if (strcmp(player_list[1-index].keywords[i], keyword) == 0){
            return true;
        }
    }
    return false;
}

static bool handle_http_request(int sockfd)
{
    // try to read the request
    char buff[2049];
    int n = read(sockfd, buff, 2049);
    if (n <= 0)
    {
        if (n < 0)
            perror("read");
        else
            printf("socket %d close the connection\n", sockfd);
        return false;
    }

    // terminate the string
    buff[n] = 0;

    char * curr = buff;
    printf("%s\n",curr);
    // parse the method
    METHOD method = UNKNOWN;
    if (strncmp(curr, "GET ", 4) == 0)
    {
        curr += 4;
        method = GET;
    }
    else if (strncmp(curr, "POST ", 5) == 0)
    {
        curr += 5;
        method = POST;
    }
    else if (write(sockfd, HTTP_400, HTTP_400_LENGTH) < 0)
    {
        perror("write");
        return false;
    }

    //get player index
    int index = get_index(sockfd);

    // sanitise the URI
    while (*curr == '.' || *curr == '/')
        ++curr;

    // assume the only valid request URI is "/" but it can be modified to accept more files
    if (*curr == ' ' || *curr == '?')
        if (method == GET)
        {
            if (strstr(buff, "username=")){
                player_list[index].visit = true;
		char *start, *end;

		if ((start = strstr(buff, "username=")+9)){
		    if ((end = strstr(start, "\n"))){
			player_list[index].username = (char*) malloc(end-start+1);
			memcpy(player_list[index].username, start, end-start);
			player_list[index].username[end-start] = '\0';
		    }
		}

                int username_length = strlen(player_list[index].username);
                // the length needs to include the p tag enclosing the username
                long added_length = username_length + 9;

                // get the size of the file
                struct stat st;
                stat("html/2_start.html", &st);
                // increase file size to accommodate the username
                long size = st.st_size + added_length;
                n = sprintf(buff, HTTP_200_FORMAT, size);
                // send the header first
                if (write(sockfd, buff, n) < 0)
                {
                    perror("write");
                    return false;
                }
                // read the content of the HTML file
                int filefd = open("html/2_start.html", O_RDONLY);
                n = read(filefd, buff, 2048);
                if (n < 0)
                {
                    perror("read");
                    close(filefd);
                    return false;
                }
                close(filefd);
                // move the trailing part backward
                int p1, p2;
                for (p1 = size - 1, p2 = p1 - added_length; p1 >= size - 213; --p1, --p2)
                    buff[p1] = buff[p2];
                ++p2;

                //write line
                int line_size = 9+username_length;
                char line[line_size];
                snprintf(line, sizeof(line), "%s%s%s", "<p>", player_list[index].username, "</p>\n");

                strncpy(buff + p2, line, line_size);
                if (write(sockfd, buff, size) < 0)
                {
                    perror("write");
                    return false;
                }
            }

            if (!(player_list[index].visit)){
                if (!(get_html("html/1_intro.html", sockfd, buff))){return false;};
            }

            else if (strstr(buff, "start=")){
                if (!(get_html("html/3_first_turn.html", sockfd, buff))){return false;};
                player_list[index].start = true;
            }

            else if (player_list[1-index].quit){
                player_list[index].quit = true;
                if (!(get_html("html/7_gameover.html", sockfd, buff))){return false;};
                return false;
            }

        }
        else if (method == POST)
        {
            if (strstr(buff, "user=")){
                char * username = strstr(buff, "user=") + 5;
		player_list[index].username = (char*) malloc(strlen(username)+1);
                memcpy(player_list[index].username, username, strlen(username));
		player_list[index].username[strlen(username)] = '\0'; 
                int username_length = strlen(username);
                // the length needs to include the p tag enclosing the username
                long added_length = username_length + 9;

                // get the size of the file
                struct stat st;
                stat("html/2_start.html", &st);
                // increase file size to accommodate the username
                long size = st.st_size + added_length;
                n = sprintf(buff, HTTP_200_FORMAT_COOKIE, username, size);
                // send the header first
                if (write(sockfd, buff, n) < 0)
                {
                    perror("write");
                    return false;
                }
                // read the content of the HTML file
                int filefd = open("html/2_start.html", O_RDONLY);
                n = read(filefd, buff, 2048);
                if (n < 0)
                {
                    perror("read");
                    close(filefd);
                    return false;
                }
                close(filefd);
                // move the trailing part backward
                int p1, p2;
                for (p1 = size - 1, p2 = p1 - added_length; p1 >= size - 213; --p1, --p2)
                    buff[p1] = buff[p2];
                ++p2;

                //write line
                int line_size = 9+username_length;
                char line[line_size];
                snprintf(line, sizeof(line), "%s%s%s", "<p>", player_list[index].username, "</p>\n");

                strncpy(buff + p2, line, line_size);
                if (write(sockfd, buff, size) < 0)
                {
                    perror("write");
                    return false;
                }

            }
            else if ((strstr(buff, "quit=")) || (player_list[1-index].quit)){
                player_list[index].quit = true;
                if (!(get_html("html/7_gameover.html", sockfd, buff))){return false;};
                return false;
            }

            else if (strstr(buff, "keyword=")){
                if (!player_list[1-index].start){
                    if (!(get_html("html/5_discarded.html", sockfd, buff))){return false;};
                }
                else{
                    char *keyword = strstr(buff, "keyword=") + 8;
                    if ((was_submitted(keyword, index)) || (player_list[1-index].end)){
                        //clear keyword list
                        for (int x = 0; x < player_list[index].n_keywords; x++){
                            player_list[index].keywords[x] = (char*) NULL;
                        }
                        for (int y = 0; y < player_list[1-index].n_keywords; y++){
                            player_list[index].keywords[y] = (char*) NULL;
                        }
                        player_list[index].n_keywords = 0;
                        player_list[1-index].n_keywords = 0;

                        if (!(get_html("html/6_endgame.html", sockfd, buff))){return false;};
                    }
                    else{
                        insert_keyword(keyword, sockfd, buff);
                    }
                }
            }
        }
        else
            // never used, just for completeness
            fprintf(stderr, "no other methods supported");
    // send 404
    else if (write(sockfd, HTTP_404, HTTP_404_LENGTH) < 0)
    {
        perror("write");
        return false;
    }

    return true;
}

int main(int argc, char * argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s ip port\n", argv[0]);
        return 0;
    }

    // create TCP socket which only accept IPv4
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // reuse the socket if possible
    int const reuse = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0)
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // create and initialise address we will listen on
    struct sockaddr_in serv_addr;
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    // if ip parameter is not specified
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    // bind address to socket
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    else{
        printf("image_tagger is now running at IP: %s on port %s\n", argv[1], argv[2]);
    }

    // listen on the socket
    listen(sockfd, 5);

    // initialise an active file descriptors set
    fd_set masterfds;
    FD_ZERO(&masterfds);
    FD_SET(sockfd, &masterfds);
    // record the maximum socket number
    int maxfd = sockfd;

    init_player_list();

    while (1)
    {
        // monitor file descriptors
        fd_set readfds = masterfds;
        if (select(FD_SETSIZE, &readfds, NULL, NULL, NULL) < 0)
        {
            perror("select");
            exit(EXIT_FAILURE);
        }

        // loop all possible descriptor
        for (int i = 0; i <= maxfd; ++i)
            // determine if the current file descriptor is active
            if (FD_ISSET(i, &readfds))
            {
                // create new socket if there is new incoming connection request
                if (i == sockfd)
                {
                    player_list[player_count].socket_id = accept(sockfd, (struct sockaddr *)&player_list[player_count].client_addr, &(player_list[player_count].len));

                    if (player_list[player_count].socket_id < 0)
                        perror("accept");
                    else
                    {
                        // add the socket to the set
                        FD_SET(player_list[player_count].socket_id, &masterfds);
                        // update the maximum tracker
                        if (player_list[player_count].socket_id > maxfd)
                            maxfd = player_list[player_count].socket_id;
                        // print out the IP and the socket number
                        char ip[INET_ADDRSTRLEN];
                        printf(
                            "new connection from %s on socket %d\n",
                            // convert to human readable string
                            inet_ntop(player_list[player_count].client_addr.sin_family, (struct sockaddr *)&player_list[player_count].client_addr.sin_addr, ip, INET_ADDRSTRLEN),
                            player_list[player_count].socket_id

                        );
                        player_count++;
                    }
                }
                // a request is sent from the client
                else if (!handle_http_request(i))
                {
                    for (int j = 0; j < 2; j++){
			free(player_list[j].username);
                        free(player_list[j].keywords);
                    }
                    close(i);
                    FD_CLR(i, &masterfds);
                }
            }
    }

    return 0;
}
