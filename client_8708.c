/*
 * NetMessenger Client - IE3010 Network Programming
 * Student ID: IT23718708 | Port: 14708 | NID: NID:7187
 */

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
#include <sys/stat.h>
#include <netdb.h>
#include <signal.h>

#define DEFAULT_PORT 14708
#define DEFAULT_IP   "127.0.0.1"
#define BUF_SIZE     4096

static int sock_fd = -1;
static volatile int running = 1;

/* Background receiver thread: handles server messages and incoming files */
void* recv_thread(void *arg) {
    (void)arg;
    char rx[BUF_SIZE];
    int rx_len = 0;
    mkdir("./downloads", 0755);

    while (running) {
        ssize_t n = recv(sock_fd, rx + rx_len, sizeof(rx) - rx_len - 1, 0);
        if (n <= 0) {
            if (running) printf("\n[Disconnected from server]\n");
            running = 0;
            break;
        }
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

            /* Check for incoming file: MSG FILE <sender> <filename> <filesize> */
            if (strncmp(line, "MSG FILE ", 9) == 0) {
                char sender[32] = {0}, fname[128] = {0};
                long long fsize = 0;
                sscanf(line + 9, "%31s %127s %lld", sender, fname, &fsize);

                printf("\n[INCOMING FILE] '%s' (%lld bytes) from '%s'\n", fname, fsize, sender);
                char path[256];
                snprintf(path, sizeof(path), "./downloads/%s", fname);
                FILE *fp = fopen(path, "wb");

                long long left = fsize;
                int buffered = rx_len;
                if (buffered > 0) {
                    int w = (buffered > left) ? (int)left : buffered;
                    if (fp) fwrite(rx, 1, w, fp);
                    left -= w;
                    int rem = buffered - w;
                    if (rem > 0) memmove(rx, rx + w, rem);
                    rx_len = rem;
                    rx[rx_len] = '\0';
                }

                char chunk[BUF_SIZE];
                while (left > 0) {
                    int r = (left > (long long)sizeof(chunk)) ? (int)sizeof(chunk) : (int)left;
                    ssize_t in = recv(sock_fd, chunk, r, 0);
                    if (in <= 0) break;
                    if (fp) fwrite(chunk, 1, in, fp);
                    left -= in;
                }
                if (fp) {
                    fclose(fp);
                    printf("[SUCCESS] File saved to %s\n", path);
                }
                printf("> ");
                fflush(stdout);
            } else {
                printf("%s\n> ", line);
                fflush(stdout);
            }
        }
    }
    return NULL;
}

/* Upload file using SENDFILE command */
void send_file(const char *target, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        printf("[ERROR] Cannot open file '%s'\n", path);
        return;
    }
    fseek(fp, 0, SEEK_END);
    long long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    const char *fname = strrchr(path, '/');
    if (!fname) fname = strrchr(path, '\\');
    fname = fname ? fname + 1 : path;

    /* Send command header */
    char hdr[256];
    snprintf(hdr, sizeof(hdr), "SENDFILE %s %s %lld\n", target, fname, fsize);
    send(sock_fd, hdr, strlen(hdr), 0);

    /* Stream raw file bytes */
    char chunk[BUF_SIZE];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        send(sock_fd, chunk, n, 0);
    }
    fclose(fp);
    printf("[SENDFILE] Sent '%s' (%lld bytes) to '%s'\n", fname, fsize, target);
}

int main(int argc, char *argv[]) {
    const char *ip = (argc > 1) ? argv[1] : DEFAULT_IP;
    int port = (argc > 2) ? atoi(argv[2]) : DEFAULT_PORT;

    /* Lecture 05 Slide 50: ignore SIGPIPE */
    signal(SIGPIPE, SIG_IGN);

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket error");
        return 1;
    }

    /* Worksheet 2 Task 6: memset */
    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port); /* Worksheet 2 Task 3: htons */

    /* Lecture 04 Slide 13: gethostbyname to resolve hostname or IP */
    struct hostent *he = gethostbyname(ip);
    if (he != NULL) {
        memcpy(&serv.sin_addr, he->h_addr_list[0], he->h_length);
    } else {
        inet_pton(AF_INET, ip, &serv.sin_addr);
    }

    if (connect(sock_fd, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
        perror("connect failed");
        return 1;
    }

    /* Worksheet 2 Task 7: getsockname & getpeername */
    struct sockaddr_in local, peer;
    socklen_t local_len = sizeof(local), peer_len = sizeof(peer);
    if (getsockname(sock_fd, (struct sockaddr *)&local, &local_len) == 0)
        printf("[getsockname] Local client port: %d\n", ntohs(local.sin_port));
    if (getpeername(sock_fd, (struct sockaddr *)&peer, &peer_len) == 0)
        printf("[getpeername] Connected to server at %s:%d\n", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));

    printf("Type commands (e.g. REGISTER <name>, BCAST <msg>, QUIT):\n\n");

    /* Start background listener thread (Worksheet 5 dual I/O solution) */
    pthread_t tid;
    pthread_create(&tid, NULL, recv_thread, NULL);
    pthread_detach(tid);

    char line[1024];
    while (running) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) == 0) continue;

        /* Check for SENDFILE command: SENDFILE <target> <filepath> [optional_size] */
        if (strncasecmp(line, "SENDFILE ", 9) == 0) {
            char target[64] = {0}, fpath[256] = {0};
            if (sscanf(line + 9, "%63s %255s", target, fpath) >= 2) {
                send_file(target, fpath);
                continue;
            }
            printf("Usage: SENDFILE <target> <filepath>\n");
            continue;
        }

        /* Send line terminated with \n */
        char out[1050];
        snprintf(out, sizeof(out), "%s\n", line);
        send(sock_fd, out, strlen(out), 0);

        if (strcasecmp(line, "QUIT") == 0) {
            sleep(1);
            break;
        }
        usleep(50000);
    }

    running = 0;
    /* Worksheet 5 Task 11: shutdown before close */
    shutdown(sock_fd, SHUT_WR);
    close(sock_fd);
    return 0;
}
