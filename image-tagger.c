/*
** Submission for COMP30023 Computer Systems Project 1 2019
** Owner: Dhira Metta Ksanti Tjoe
** Student ID: 849808
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

// a struct representing a player in the game
typedef struct player{
    char* username;
    bool visit;
    bool start;
    bool quit;
    bool end;
    char keywords[20][20];
    int n_keywords;
    int total_word_len;
    char *user_agent;
} player;

// initialise so that there can only be 2 players at once
player player_list[2];

// initialise attributes in player struct
void init_player_list(){
    for (int i = 0; i < 2; i++){
        player_list[i].username = NULL;
        player_list[i].visit = false;
        player_list[i].start = false;
        player_list[i].quit = false;
        player_list[i].end = false;
        for (int x = 0; x < 20; x++){
            for (int y = 0; y < 20; y++){
                player_list[i].keywords[x][y] = 0;
            }
        }
        player_list[i].n_keywords = 0;
        player_list[i].total_word_len = 0;
        player_list[i].user_agent = NULL;
    }
}

// identify player by their user agent
int get_index(char *ua){
    if (strcmp(player_list[0].user_agent, ua) == 0){
        free(ua);
        return 0;
    }
    else{
        free(ua);
        return 1;
    }
}


// get value between two patterns and assign the value to result buffer provided
void get_value_between(char* result, char* buff, char* pattern1, char* pattern2){
    char *start, *end;

    if ((start = strstr(buff, pattern1)+ strlen(pattern1))){
        if ((end = strstr(start, pattern2))){
            result = (char*) malloc (end-start+1);
            memcpy(result, start, end-start);
            result[end-start] = '\0';
        }
    }
}

// function to get html
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

// insert keyword into player's list of keywords and print them out
bool insert_keyword(char *keyword, int sockfd, char* buff, int index){

    int word_length = strlen(keyword);
    long added_length;

    if (player_list[index].n_keywords == 0){
        player_list[index].total_word_len += word_length;
    }
    else{
        // the length needs to include the ", " before the keyword
        player_list[index].total_word_len += word_length + 2;
    }

    //the length needs to include the p tag enclosing the keyword
    added_length = player_list[index].total_word_len + 9;
    strcpy(player_list[index].keywords[player_list[index].n_keywords], keyword);
    player_list[index].n_keywords++;

    // get the size of the file
    struct stat st;
    stat("html/4_accepted.html", &st);
    // increase file size to accommodate the words
    long size = st.st_size + added_length;
    int n = sprintf(buff, HTTP_200_FORMAT, size);
    // send the header first
    if (write(sockfd, buff, n) < 0)
    {
        perror("write");
        return false;
    }
    // read the content of the HTML file
    int filefd = open("html/4_accepted.html", O_RDONLY);
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
    for (p1 = size - 1, p2 = p1 - added_length; p1 >= size - 264; --p1, --p2)
    buff[p1] = buff[p2];
    ++p2;
    char line[added_length];

    // enclose keyword(s) in <p> tag
    if (player_list[index].n_keywords == 0){
        snprintf(line, sizeof(line), "%s%s%s", "<p>", keyword, "</p>\n");
    }
    else{
        // empty string to store concatenated words separated by ", "
        char words[player_list[index].total_word_len];

        strcpy(words, player_list[index].keywords[0]);
        for (int x = 1; x < player_list[index].n_keywords; x++){
            strcat(words, ", ");
            strcat(words, player_list[index].keywords[x]);
        }
        snprintf(line, sizeof(line), "%s%s%s", "<p>", words, "</p>\n");
    }

    // copy the words
    strncpy(buff + p2, line, added_length);

    if (write(sockfd, buff, size) < 0)
    {
        perror("write");
        return false;
    }

    free(keyword);
    return true;
}

// checks if keyword was submitted by other player
bool was_submitted(char* keyword, int index){
    for (int i = 0; i < player_list[1-index].n_keywords; i++){
        if (strcmp(player_list[1-index].keywords[i], keyword) == 0){
            return true;
        }
    }
    return false;
}

//handle http request
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
    int index;

    if (player_list[0].user_agent == NULL){
        get_value_between(player_list[0].user_agent, buff, "User-Agent: ", "\n");
        index = 0;
    } else if (player_list[1].user_agent == NULL){
        get_value_between(player_list[0].user_agent, buff, "User-Agent: ", "\n");
        index = 1;
    } else{
        char* ua = NULL;
        index = get_index(get_value_between(ua, buff, "User-Agent: ", "\n"));
    }

    // sanitise the URI
    while (*curr == '.' || *curr == '/')
    ++curr;

    if (*curr){
        if (method == GET){

            // get username from cookie and respond with the start page with username appended
            if ((strstr(buff, "username=")) && (player_list[index].username==NULL)){
                player_list[index].visit = true;
                get_value_between(player_list[index].username, buff, "username=","\r");

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

            // if this is player's first visit render intro page
            if (!(player_list[index].visit)){
                if (!(get_html("html/1_intro.html", sockfd, buff))){return false;};
            }

            // if player wants to start render image for first turn
            else if ((strstr(curr, "start=")) && (!(player_list[index].start))){
                player_list[index].start = true;
                player_list[index].end = false;
                if (!(get_html("html/3_first_turn.html", sockfd, buff))){return false;};
            }

            // if player wants to quit render gameover page and close connection
            else if (player_list[1-index].quit){
                player_list[index].quit = true;
                if (!(get_html("html/7_gameover.html", sockfd, buff))){return false;};
                return false;
            }
        }

        else if (method == POST){

            // if player inserts username
            if (strstr(buff, "user=")){
                player_list[index].visit = true;
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
                // set cookie
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

            // if player wants to quit render gameover page and close connection
            else if ((strstr(buff, "quit=")) || (player_list[1-index].quit)){
                player_list[index].quit = true;
                if (!(get_html("html/7_gameover.html", sockfd, buff))){return false;};
                return false;
            }

            // if player wants to submit keyword
            else if (strstr(buff, "keyword=")){

                // if other player has won, render endgame page
                if (player_list[1-index].end){
                    player_list[index].end = true;
                    player_list[index].start = false;
                    if (!(get_html("html/6_endgame.html", sockfd, buff))){return false;}
                }

                // if other player hasn't started yet, discard the keyword submitted
                else if (!player_list[1-index].start){
                    if (!(get_html("html/5_discarded.html", sockfd, buff))){return false;};
                }

                else{
                    char *keyword = NULL;
                    get_value_between(keyword, buff, "keyword=", "&guess");

                    // if keyword has been submitted, empty each player's keyword list and render endgame page
                    if ((was_submitted(keyword, index))){
                        //clear keyword list
                        for (int x = 0; x < player_list[index].n_keywords; x++){
                            strcpy(player_list[index].keywords[x],"");
                        }
                        for (int y = 0; y < player_list[1-index].n_keywords; y++){
                            strcpy(player_list[index].keywords[y],"");
                        }
                        player_list[index].n_keywords = 0;
                        player_list[1-index].n_keywords = 0;

                        player_list[index].end = true;
                        player_list[index].start = false;
                        if (!(get_html("html/6_endgame.html", sockfd, buff))){return false;}
                    }

                    // otherwise show inputted keyword list
                    else{
                        insert_keyword(keyword, sockfd, buff, index);
                    }
                    free(keyword);
                }
            }
        }
        else{
        // never used, just for completeness
            fprintf(stderr, "no other methods supported");
        }
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
        printf("image_tagger server is now running at IP: %s on port %s\n", argv[1], argv[2]);
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
                struct sockaddr_in cliaddr;
                socklen_t clilen = sizeof(cliaddr);
                int newsockfd = accept(sockfd, (struct sockaddr *)&cliaddr, &clilen);
                if (newsockfd < 0)
                perror("accept");
                else
                {
                    // add the socket to the set
                    FD_SET(newsockfd, &masterfds);
                    // update the maximum tracker
                    if (newsockfd > maxfd)
                    maxfd = newsockfd;
                    // print out the IP and the socket number
                    char ip[INET_ADDRSTRLEN];
                    printf(
                        "new connection from %s on socket %d\n",
                        // convert to human readable string
                        inet_ntop(cliaddr.sin_family, &cliaddr.sin_addr, ip, INET_ADDRSTRLEN),
                        newsockfd
                    );
                }
            }
            // a request is sent from the client
            else if (!handle_http_request(i))
            {
                // close connection and free memblocks
                for (int j = 0; j < 2; j++){
                    free(player_list[j].username);
                    free(player_list[j].user_agent);
                }
                close(i);
                FD_CLR(i, &masterfds);
            }
        }
    }

    return 0;
}
