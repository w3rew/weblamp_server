#include <arpa/inet.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <signal.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>

const size_t QUEUE_SIZE = 5;
const size_t MSG_SIZE = 2;
sig_atomic_t flag_continue = 1;
bool init_flag = false;

typedef struct {
    uint8_t power;
    uint8_t color;
} lamp_state_t;

lamp_state_t cur_state = {0, 0};
lamp_state_t prev_state = {0, 0};
void handle_signals(int sig)
{
    if (sig == SIGTERM || sig == SIGINT)
        flag_continue = 0;
}

void make_nonblk(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        perror("Fcntl error!");
}

ssize_t read_state(int client_fd, lamp_state_t* state)
{
    uint8_t buf[MSG_SIZE];

    ssize_t ans = recv(client_fd, buf, MSG_SIZE, MSG_PEEK);
    if (ans < MSG_SIZE) //return later
        return 0;

    ans = read(client_fd, buf, MSG_SIZE);
    if (ans < MSG_SIZE) {
        fprintf(stderr, "Failed to read exactly %d bytes, read %zd\n",
                MSG_SIZE, ans);
        return 0;
    }

    state->power = buf[0];
    state->color = buf[1];
#ifdef DEBUG
    printf("Read %d %d\n", buf[0], buf[1]);
#endif

    return ans;
}

bool send_response(int client_fd, const lamp_state_t* state)
{
    uint8_t data[2] = {state->power, state->color};
    ssize_t ans = write(client_fd, data, 2);
    if (ans < 2) {
#ifdef DEBUG
        printf("ERROR\n");
#endif
        return false;
    }

    printf("Written %zd bytes\n", ans);
    return true;
}

bool states_equal(const lamp_state_t* first, const lamp_state_t* second)
{
    return ((first->power == second->power) &&
        (first->color == second->color));
}

void communicate(int fd)
{
    lamp_state_t state;
    if (read_state(fd, &state) <= 0)
        return;

    if (!init_flag) {
        prev_state = state;
        cur_state = state;
        init_flag = true;
    } else if (!states_equal(&prev_state, &state)) {
        prev_state = cur_state;
        cur_state = state;
    }

    send_response(fd, &cur_state);
}

    


int main(int argc, char *argv[])
{
    if (argc < 2)
        return 1;
    struct sigaction act = {.sa_handler = handle_signals, .sa_flags = 0};
    sigemptyset(&act.sa_mask);
    if (sigaction(SIGTERM, &act, NULL))
        return 1;
    if (sigaction(SIGINT, &act, NULL))
        return 1;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    make_nonblk(sockfd);
    uint16_t port = htons(atoi(argv[1]));
    struct sockaddr_in addr = {
        .sin_family = AF_INET, .sin_port = port, .sin_addr = {INADDR_ANY}};
#ifdef DEBUG
    int val = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val));
#endif

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
        return 1;

    if (listen(sockfd, SOMAXCONN) == -1)
        return 1;
    
    int epoll_fd = epoll_create(1);

    struct epoll_event event = {.data = {.fd = sockfd}, .events = EPOLLIN | EPOLLET};

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockfd, &event) == -1)
        perror("Epoll error!");
    for (;;) {
        int client_fd = accept(sockfd, NULL, NULL);
        if (client_fd > 0) {
            make_nonblk(client_fd);
            struct epoll_event client_event = {.data = {.fd = client_fd},
                .events = EPOLLIN | EPOLLOUT | EPOLLET};
            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event) == -1)
                perror("Client epoll error!");
        } else {
            if (errno != EWOULDBLOCK)
                perror("Accept error!");
            break;
        }
    }
    int client_fd = -1;
    struct epoll_event *events = calloc(QUEUE_SIZE, sizeof(struct epoll_event));
    while (flag_continue) {
        int read_events = epoll_wait(epoll_fd, events, QUEUE_SIZE, -1);
        for (int i = 0; i < read_events; ++i) {
            int fd = events[i].data.fd;
            if (fd == sockfd) {
                for (;;) {
                    int client_fd = accept(sockfd, NULL, NULL);
                    if (client_fd > 0) {
                        make_nonblk(client_fd);
                        struct epoll_event client_event = {
                            .data =
                            {.fd = client_fd},
                            .events = EPOLLIN | EPOLLET};
                        if (epoll_ctl(epoll_fd,
                                    EPOLL_CTL_ADD,
                                    client_fd,
                                    &client_event) == -1)
                            perror("Client epoll error!");
                    } else {
                        if (errno != EWOULDBLOCK)
                            perror("Accept error!");
                        break;
                    }
                }
            }
            else {
                printf("Fd %d\n", fd);
                communicate(fd);
            }
        }
    }

    close(sockfd);
    free(events);

    return 0;
}
