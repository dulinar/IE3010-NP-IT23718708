#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <netdb.h>
#include <signal.h>

#define DEFAULT_PORT 14708
#define DEFAULT_IP   "127.0.0.1"
#define BUF_SIZE     4096

static int sock_fd = -1;
static volatile int running = 1;

ssize_t send_all(int fd, const void *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(fd, (const char *)buf + total, len - total, 0);
        if (n <= 0) return n;
        total += n;
    }
    return total;
}

void* recv_thread(void *arg) {
    (void)arg;
    char rx[BUF_SIZE];
    int rx_len = 0;

    while (running) {
        ssize_t n = recv(sock_fd, rx + rx_len, sizeof(rx) - rx_len - 1, 0);
        if (n <= 0) { running = 0; break; }
        rx_len += n;
        rx[rx_len] = '\0';

        while (1) {
            char *nl = strchr(rx, '\n');
            if (!nl) break;
            *nl = '\0';
            char line[BUF_SIZE];
            snprintf(line, sizeof(line), "%s", rx);
            if (strlen(line) > 0 && line[strlen(line) - 1] == '\r') line[strlen(line) - 1] = '\0';
            int consumed = (nl - rx) + 1;
            memmove(rx, rx + consumed, rx_len - consumed);
            rx_len -= consumed;
            rx[rx_len] = '\0';

            if (strncmp(line, "MSG FILE ", 9) == 0) {
                char sender[32] = {0}, fname[128] = {0};
                long long fsize = 0;
                sscanf(line + 9, "%31s %127s %lld", sender, fname, &fsize);
                long long left = fsize;
                if (rx_len > 0) {
                    int w = (rx_len > left) ? (int)left : rx_len;
                    left -= w;
                    int rem = rx_len - w;
                    if (rem > 0) memmove(rx, rx + w, rem);
                    rx_len = rem;
                    rx[rx_len] = '\0';
                }
                char chunk[BUF_SIZE];
                while (left > 0) {
                    int r = (left > (long long)sizeof(chunk)) ? (int)sizeof(chunk) : (int)left;
                    ssize_t in = recv(sock_fd, chunk, r, 0);
                    if (in <= 0) break;
                    left -= in;
                }
                printf("%s\n> ", line);
                fflush(stdout);
            } else {
                printf("%s\n> ", line);
                fflush(stdout);
            }
        }
    }
    return NULL;
}

void send_file(const char *target, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { printf("[ERROR] Cannot open file '%s'\n", path); return; }
    fseek(fp, 0, SEEK_END);
    long long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    const char *b = strrchr(path, '/');
    if (!b) b = strrchr(path, '\\');
    const char *fname = b ? b + 1 : path;

    char hdr[256];
    snprintf(hdr, sizeof(hdr), "SENDFILE %s %s %lld\n", target, fname, fsize);
    if (send_all(sock_fd, hdr, strlen(hdr)) <= 0) { fclose(fp); return; }

    char chunk[BUF_SIZE];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        if (send_all(sock_fd, chunk, n) <= 0) break;
    }
    fclose(fp);
}

int main(int argc, char *argv[]) {
    const char *ip = (argc > 1) ? argv[1] : DEFAULT_IP;
    int port = (argc > 2) ? atoi(argv[2]) : DEFAULT_PORT;
    signal(SIGPIPE, SIG_IGN);

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) { perror("socket error"); return 1; }

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);

    struct hostent *he = gethostbyname(ip);
    if (he) memcpy(&serv.sin_addr, he->h_addr_list[0], he->h_length);
    else inet_pton(AF_INET, ip, &serv.sin_addr);

    if (connect(sock_fd, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
        perror("connect failed");
        return 1;
    }
    printf("Connected to NetMessenger server at %s:%d\n", ip, port);

    pthread_t tid;
    pthread_create(&tid, NULL, recv_thread, NULL);

    char line[1024];
    while (running) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\r\n")] = '\0';
        if (!*line) continue;

        if (strncasecmp(line, "SENDFILE ", 9) == 0) {
            char target[64] = {0}, fpath[256] = {0};
            if (sscanf(line + 9, "%63s %255s", target, fpath) >= 2) {
                send_file(target, fpath);
                continue;
            }
            printf("Usage: SENDFILE <target> <filepath>\n");
            continue;
        }

        char out[1050];
        snprintf(out, sizeof(out), "%s\n", line);
        if (send_all(sock_fd, out, strlen(out)) <= 0) break;

        if (strcasecmp(line, "QUIT") == 0) {
            pthread_join(tid, NULL);
            break;
        }
    }

    running = 0;
    if (sock_fd != -1) close(sock_fd);
    return 0;
}
