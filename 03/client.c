#include <stdio.h>      /* printf, sprintf */
#include <stdlib.h>     /* exit, atoi, malloc, free */
#include <unistd.h>     /* read, write, close */
#include <string.h>     /* memcpy, memset */
#include <sys/socket.h> /* socket, connect */
#include <netinet/in.h> /* struct sockaddr_in, struct sockaddr */
#include <netdb.h>      /* struct hostent, gethostbyname */
#include <arpa/inet.h>

#include "helpers.h"
#include "requests.h"

#include "parson.h"

#define SERV_HOST "34.118.48.238"
#define SERV_PORT 8080

/**
 * parse the credentials from stdin
 */
void get_credentials(char *user, int *len_user,
                     char *pass, int *len_pass) {
    printf("username=");
    fgets(user, LINELEN, stdin);
    (*len_user) = strlen(user);
    // remove the '\n'
    user[(*len_user) - 1] = '\0';
    (*len_user)--;

    printf("password=");
    fgets(pass, LINELEN, stdin);
    (*len_pass) = strlen(pass);
    // remove the '\n'
    pass[(*len_pass)- 1] = '\0';
    (*len_pass)--;
}

int get_field(char *field) {
    int len;

    fgets(field, LINELEN, stdin);
    len = strlen(field);
    field[len - 1] = '\0';
    len--;

    return len;
}

/**
 * get the payload needed for login/register
 *    --> JSON format
 */
void auth_payload(char *user, char *pass, char *pay) {
    JSON_Value *val = json_value_init_object();
    JSON_Object *obj = json_value_get_object(val);

    char *res = NULL;

    json_object_set_string(obj, "username", user);
    json_object_set_string(obj, "password", pass);

    res = json_serialize_to_string_pretty(val);
    strcpy(pay, res);

    json_free_serialized_string(res);
    json_value_free(val);
}

void book_payload(char *title, char *author,
                  char *genre, int page_count,
                  char *publisher, char *pay) {
    JSON_Value *val = json_value_init_object();
    JSON_Object *obj = json_value_get_object(val);

    char *res = NULL;

    json_object_set_string(obj, "title", title);
    json_object_set_string(obj, "author", author);
    json_object_set_string(obj, "genre", genre);
    json_object_set_number(obj, "page_count", (double) page_count);
    json_object_set_string(obj, "publisher", publisher);

    res = json_serialize_to_string_pretty(val);
    strcpy(pay, res);

    json_free_serialized_string(res);
    json_value_free(val);
}

/**
 * parse the response from server
 *   - if it's an error --> print
 *   - if not, get the token
 */
void lib_access(char *response, char **token) {
    char *lib_check = basic_extract_json_response(response);
            
    JSON_Value *val = json_parse_string(lib_check);
    JSON_Object *obj = json_value_get_object(val);

    char *aux = json_object_get_string(obj, "error");

    if (aux == NULL) {
        // get the token
        *token = json_object_get_string(obj, "token");
        printf("[LIBRARY] You now have access to the library\n");
    } else {
        fprintf(stderr, "[LIBRARY] %s\n", aux);
    }
}

void books_check(char *response) {
    char *book_check = basic_extract_json_response(response);
            
    JSON_Value *val = json_parse_string(book_check);
    JSON_Object *obj = json_value_get_object(val);

    char *aux = json_object_get_string(obj, "error");

    if (aux != NULL) {
        fprintf(stderr, "[BOOKS] %s\n", aux);
    } else if (book_check == NULL) {
        // the library is empty 
        //    --> no books
        printf("[BOOKS] You don't have any books at the moment\n");
    } else {
        // parse the books _array_
        JSON_Value *root_value = json_parse_string(book_check - 1);
        JSON_Array *books_arr = json_value_get_array(root_value);

        size_t ct = json_array_get_count(books_arr);
        printf("%-5.5s %s\n", "ID", "Title");

        JSON_Object *book;
        for (int i = 0; i < ct; ++i) {
            book = json_array_get_object(books_arr, i);

            int id = (int) json_object_get_number(book, "id");
            char *title = json_object_get_string(book, "title");

            printf("%.5d %s\n", id, title);
        }
    }

    json_value_free(val);
}

/**
 * check if the wanted book
 * was found in the library
 */
void id_book_check(char *response) {
    char *book_check = basic_extract_json_response(response);
            
    JSON_Value *val = json_parse_string(book_check);
    JSON_Object *obj = json_value_get_object(val);

    char *aux = json_object_get_string(obj, "error");

    if (aux != NULL) {
        // check what error has been encountered
        printf("[BOOK] %s\n", aux);
    } else {
        char *res = json_serialize_to_string_pretty(val);

        printf("%s\n", res);
    }
}

/**
 * check the message received from server
 * for <add_book>
 */
void add_check(char *response) {
    // check for "Too Many Requests" message
    char *msg = (char *) calloc(BUFLEN, sizeof(char));
    strcpy(msg, response);

    char *p = strtok(msg, "\r");
    // check the Status Text
    // from the first line of response
    char *r = strstr(p, "Too Many Requests");

    if (r != NULL) {
        fprintf(stderr, "[SERVER] Too Many Requests, please try again later.\n");
        free(msg);
        return;
    }

    char *book_check = basic_extract_json_response(response);
            
    JSON_Value *val = json_parse_string(book_check);
    JSON_Object *obj = json_value_get_object(val);

    char *aux = json_object_get_string(obj, "error");

    if (aux != NULL) {
        // check what error has been encountered
        printf("[ADD_BOOK] %s\n", aux);
    } else {
        printf("[ADD_BOOK] The book was added\n");
    }
}

/**
 * check the response given for <delete_book>
 */
void delete_check(char *response) {
    char *book_check = basic_extract_json_response(response);
            
    JSON_Value *val = json_parse_string(book_check);
    JSON_Object *obj = json_value_get_object(val);

    char *aux = json_object_get_string(obj, "error");

    if (aux != NULL) {
        // check what error has been encountered
        printf("[DELETE] %s\n", aux);
    } else {
        // the book was deleted
        printf("[DELETE] The book was deleted\n");
    }
}

/**
 * Extract the error received
 *      from server about log(in/out)
 *                     or register
 */
void check_log(char *chk) {
    JSON_Value *val = json_parse_string(chk);
    JSON_Object *obj = json_value_get_object(val);

    char *aux = json_object_get_string(obj, "error");

    if (aux != NULL) {
        // check what error has been encountered
        printf("[LOG] %s\n", aux);
    }
}

int main(int argc, char *argv[])
{
    char cmd[20];
    int len_cmd;

    char *message;
    char *response;

    int sockfd;

    char *curr_cookie = NULL;
    char *token = NULL;

    while (1) {
        memset(cmd, 0, 20);
        fgets(cmd, 20, stdin);

        len_cmd = strlen(cmd);
        // remove the '\n'
        cmd[len_cmd - 1] = '\0';
        len_cmd--;

        if (strcmp(cmd, "register") == 0) {
            char user[LINELEN];
            int len_user;

            char pass[LINELEN];
            int len_pass;

            // don't allow the user to register
            // while still being logged in
            if (curr_cookie != NULL) {
                fprintf(stderr, "[REGISTER] Cannot perform this operation while being logged in\n");
                continue;
            }

            // get the username and password
            get_credentials(user, &len_user, pass, &len_pass);

            char *res;
            res = (char *)calloc(BUFLEN, sizeof(char));

            auth_payload(user, pass, res);

            char *body[] = {res};

            message = compute_post_request(SERV_HOST, "/api/v1/tema/auth/register",
                                           "application/json", body, 1, NULL, 0, NULL);
            
            sockfd = open_connection(SERV_HOST, SERV_PORT, AF_INET, SOCK_STREAM, 0);
            send_to_server(sockfd, message);
            response = receive_from_server(sockfd);
            close_connection(sockfd);

            char *reg_check = basic_extract_json_response(response);
            if (reg_check != NULL) {
                // get the error message received
                check_log(reg_check);
            } else {
                printf("You are now registered. Welcome, %s\n", user);
            }

            free(res);
        } else if (strcmp(cmd, "login") == 0) {
            char user[LINELEN];
            int len_user;

            char pass[LINELEN];
            int len_pass;

            if (curr_cookie != NULL) {
                fprintf(stderr, "[LOGIN] Already logged in. Please log out first\n");
                continue;
            }

            get_credentials(user, &len_user, pass, &len_pass);

            char *res;
            res = (char *)calloc(BUFLEN, sizeof(char));

            auth_payload(user, pass, res);

            char *body[] = {res};

            message = compute_post_request(SERV_HOST, "/api/v1/tema/auth/login",
                                           "application/json", body, 1, NULL, 0, NULL);
            
            sockfd = open_connection(SERV_HOST, SERV_PORT, AF_INET, SOCK_STREAM, 0);
            send_to_server(sockfd, message);
            response = receive_from_server(sockfd);
            close_connection(sockfd);

            char *log_check = basic_extract_json_response(response);
            if (log_check != NULL) {
                check_log(log_check);
            } else {
                // successful login
                //     --> extract the cookie
                char *p = strstr(response, "Set-Cookie");
                char *aux = strtok(p, "\r");
                
                curr_cookie = (char *) calloc(LINELEN, sizeof(char));
                // jump over "Set-Cookie: "
                strcpy(curr_cookie, aux + 12);

                printf("You are now logged in, %s\n", user);
            }

            free(res);
        } else if (strcmp(cmd, "enter_library") == 0) {
            char *cookies[] = {curr_cookie};

            message = compute_get_request(SERV_HOST, "/api/v1/tema/library/access",
                                          NULL, cookies, 1, NULL);

            sockfd = open_connection(SERV_HOST, SERV_PORT, AF_INET, SOCK_STREAM, 0);
            send_to_server(sockfd, message);
            response = receive_from_server(sockfd);
            close_connection(sockfd);

            lib_access(response, &token);
        } else if (strcmp(cmd, "get_books") == 0) {
            char *cookies[] = {curr_cookie};

            message = compute_get_request(SERV_HOST, "/api/v1/tema/library/books",
                                          NULL, cookies, 1, token);

            sockfd = open_connection(SERV_HOST, SERV_PORT, AF_INET, SOCK_STREAM, 0);
            send_to_server(sockfd, message);
            response = receive_from_server(sockfd);
            close_connection(sockfd);

            books_check(response);
        } else if (strcmp(cmd, "get_book") == 0) {
            char id[LINELEN];
            int len_id;

            printf("id=");
            len_id = get_field(id);

            // check if <id> is not completed
            if (len_id == 0) {
                continue;
            }

            char *cookies[] = {curr_cookie};

            char book_url[LINELEN];
            sprintf(book_url, "/api/v1/tema/library/books/%s", id);

            message = compute_get_request(SERV_HOST, book_url,
                                          NULL, cookies, 1, token);

            sockfd = open_connection(SERV_HOST, SERV_PORT, AF_INET, SOCK_STREAM, 0);
            send_to_server(sockfd, message);
            response = receive_from_server(sockfd);
            close_connection(sockfd);

            id_book_check(response);
        } else if (strcmp(cmd, "add_book") == 0) {
            char title[LINELEN];
            char author[LINELEN];
            char genre[LINELEN];
            char publisher[LINELEN];
            int page_count;

            printf("title=");
            get_field(title);

            printf("author=");
            get_field(author);

            printf("genre=");
            get_field(genre);

            printf("publisher=");
            get_field(publisher);

            printf("page_count=");
            char pg[LINELEN];
            get_field(pg);

            page_count = atoi(pg);

            if (page_count <= 0) {
                fprintf(stderr, "[ADD_BOOK] No. pages not ok\n");

                continue;
            }

            char *res;
            res = (char *)calloc(BUFLEN, sizeof(char));

            book_payload(title, author, genre, page_count, publisher, res);

            char *body[] = {res};
            char *cookies[] = {curr_cookie};

            message = compute_post_request(SERV_HOST, "/api/v1/tema/library/books",
                                           "application/json", body, 1, cookies, 1, token);
            
            sockfd = open_connection(SERV_HOST, SERV_PORT, AF_INET, SOCK_STREAM, 0);
            send_to_server(sockfd, message);
            response = receive_from_server(sockfd);
            close_connection(sockfd);

            add_check(response);

            free(res);
        } else if (strcmp(cmd, "delete_book") == 0) {
            char id[LINELEN];

            printf("id=");
            get_field(id);

            char *cookies[] = {curr_cookie};

            char book_url[LINELEN];
            sprintf(book_url, "/api/v1/tema/library/books/%s", id);

            message = compute_delete_request(SERV_HOST, book_url,
                                             NULL, cookies, 1, token);

            sockfd = open_connection(SERV_HOST, SERV_PORT, AF_INET, SOCK_STREAM, 0);
            send_to_server(sockfd, message);
            response = receive_from_server(sockfd);
            close_connection(sockfd);

            delete_check(response);
        } else if (strcmp(cmd, "logout") == 0) {
            char *cookies[] = {curr_cookie};

            message = compute_get_request(SERV_HOST, "/api/v1/tema/auth/logout",
                                          NULL, cookies, 1, NULL);

            sockfd = open_connection(SERV_HOST, SERV_PORT, AF_INET, SOCK_STREAM, 0);
            send_to_server(sockfd, message);
            response = receive_from_server(sockfd);
            close_connection(sockfd);

            char *log_check = basic_extract_json_response(response);
            if (log_check != NULL) {
                check_log(log_check);
            } else {
                free(curr_cookie);
                curr_cookie = NULL;

                // remove the token to 
                // the current user's library
                if (token != NULL) {
                    free(token);
                    token = NULL;
                }

                printf("[LOGOUT] You've successfully logged out\n");
            }
        } else if (strcmp(cmd, "exit") == 0) {
            break;
        } else {
            fprintf(stderr, "Invalid Command!\n");
        }
    }

    return 0;
}
