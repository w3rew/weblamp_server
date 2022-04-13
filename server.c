#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define TIMEOUT 1000 //seconds

typedef struct {
    uint8_t power;
    uint8_t color;
} lamp_state_t;

sig_atomic_t flag_continue = 1;
void handle_signals(int sig)
{
    if (sig == SIGTERM || sig == SIGINT)
        flag_continue = 0;
}

ssize_t read_query(int client_fd, lamp_state_t* state)
{
    uint8_t query[2];

    ssize_t ret = 0;
    ret += read(client_fd, query, 1);
    ret += read(client_fd, query + 1, 1);
    state->power = query[0];
    state->color = query[1];
    return ret;
}

bool send_response(int client_fd, const lamp_state_t* state)
{
    uint8_t data[2] = {state->power, state->color};
    if (write(client_fd, data, 2) < 2) {
        return false;
    }

    return true;
}

bool states_equal(const lamp_state_t* first, const lamp_state_t* second)
{
    return ((first->power == second->power) &&
        (first->color == second->color));
}


int main(int argc, char* argv[])
{
    if (argc < 2)
        return 1;

    struct sigaction act = {.sa_handler = handle_signals, .sa_flags = 0};
    sigemptyset(&act.sa_mask);
    /*if (sigaction(SIGINT, &act, NULL))
        return 1;
    if (sigaction(SIGTERM, &act, NULL))
        return 1;
        */

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    uint16_t port = htons(atoi(argv[1]));
    struct sockaddr_in addr = {
        .sin_family = AF_INET, .sin_port = port, .sin_addr = {INADDR_ANY}};
#ifdef DEBUG
    int val = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val));
#endif
    /*
    struct timeval tv;
    tv.tv_sec = TIMEOUT;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
    */

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
        return 1;

    if (listen(sockfd, SOMAXCONN) == -1)
        return 1;

    int client_fd = -1;
    lamp_state_t cur_state = {0, 0};
    lamp_state_t prev_state = {0, 0};
    bool init_flag = false;

    while (flag_continue && ((client_fd = accept(sockfd, NULL, NULL)) != -1)) {
        lamp_state_t state;
        if (read_query(client_fd, &state) < 2)
            continue;

        if (!init_flag) {
            init_flag = true;
            prev_state = state;
            cur_state = state;
        } else {
            if (!states_equal(&prev_state, &state)) {
                prev_state = cur_state;
                cur_state = state;
            }
        }
        send_response(client_fd, &cur_state);

        shutdown(client_fd, SHUT_RDWR);
        close(client_fd);
    }

    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
    return 0;
}
