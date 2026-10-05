/*
 * NetMessenger Server - IE3010 Network Programming
 * Student ID: IT23718708 | Port: 14708 | NID: NID:7187
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <signal.h>

#define PORT         14708
#define NID_TAG      "NID:7187"
#define LOG_FILE     "netmsg_IT23718708.log"
#define STORAGE_DIR  "./storage/IT23718708"
#define MAX_CLIENTS  64
#define MAX_ROOMS    32
#define BUF_SIZE     4096

/* Client information */
typedef struct {
    int fd;
    char name[32];
    int registered;
    int active;
} client_t;

/* Chat room information */
typedef struct {
    char name[32];
    int fds[MAX_CLIENTS];
    int count;
    int active;
} room_t;

static client_t clients[MAX_CLIENTS];
static room_t   rooms[MAX_ROOMS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* Log event with timestamp to console and netmsg_IT23718708.log */
void log_msg(const char *tag, const char *msg) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);

    printf("[%s] [%s] %s\n", buf, tag, msg);
    FILE *fp = fopen(LOG_FILE, "a");
    if (fp) {
        fprintf(fp, "[%s] [%s] %s\n", buf, tag, msg);
        fclose(fp);
    }
}

/* Send protocol response ending with personalized NID tag */
void reply(int fd, const char *status, const char *msg) {
    char out[2048];
    snprintf(out, sizeof(out), "%s %s %s\n", status, msg, NID_TAG);
    send(fd, out, strlen(out), 0);
}

/* Broadcast message to all registered clients except exclude_fd */
void broadcast(const char *msg, int exclude_fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].registered && clients[i].fd != exclude_fd) {
            send(clients[i].fd, msg, strlen(msg), 0);
        }
    }
}

/* Find client by username */
client_t* find_client(const char *name) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].registered && strcmp(clients[i].name, name) == 0)
            return &clients[i];
    }
    return NULL;
}

/* Disconnect client, remove from rooms, and release resources */
void disconnect_client(client_t *c) {
    char name[32] = {0};
    int was_reg = 0, fd = c->fd;

    pthread_mutex_lock(&lock);
    if (c->registered) {
        snprintf(name, sizeof(name), "%s", c->name);
        was_reg = 1;
    }
    c->active = 0;
    c->registered = 0;
    c->fd = -1;

    /* Remove from all rooms */
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].active) {
            for (int j = 0; j < rooms[i].count; j++) {
                if (rooms[i].fds[j] == fd) {
                    rooms[i].fds[j] = rooms[i].fds[--rooms[i].count];
                    break;
                }
            }
            if (rooms[i].count == 0) rooms[i].active = 0;
        }
    }
    pthread_mutex_unlock(&lock);

    /* Worksheet 5 Task 11: shutdown before close */
    shutdown(fd, SHUT_RDWR);
    close(fd);

    if (was_reg) {
        char msg[128];
        snprintf(msg, sizeof(msg), "User '%s' disconnected", name);
        log_msg("DISCONNECT", msg);

        snprintf(msg, sizeof(msg), "MSG BCAST server %s has left the chat\n", name);
        pthread_mutex_lock(&lock);
        broadcast(msg, -1);
        pthread_mutex_unlock(&lock);
    }
}

/* Handle SENDFILE: store under storage/IT23718708/<sender>/ and relay */
void handle_sendfile(client_t *c, char *args, char *rx, int *rx_len, int offset) {
    char target[32] = {0}, fname[128] = {0};
    long long fsize = 0;

    if (sscanf(args, "%31s %127s %lld", target, fname, &fsize) != 3 || fsize <= 0) {
        reply(c->fd, "ERR", "007 BAD_ARGUMENTS");
        return;
    }

    pthread_mutex_lock(&lock);
    client_t *t = find_client(target);
    int target_fd = t ? t->fd : -1;
    room_t *r = NULL;
    if (target_fd == -1) {
        for (int i = 0; i < MAX_ROOMS; i++) {
            if (rooms[i].active && strcmp(rooms[i].name, target) == 0) { r = &rooms[i]; break; }
        }
    }
    pthread_mutex_unlock(&lock);

    if (target_fd == -1 && !r) {
        reply(c->fd, "ERR", "002 USER_OR_ROOM_NOT_FOUND");
        return;
    }

    /* Create personalized directory: ./storage/IT23718708/<sender>/ */
    mkdir("./storage", 0755);
    mkdir(STORAGE_DIR, 0755);
    char dir[512], path[512];
    snprintf(dir, sizeof(dir), "%s/%s", STORAGE_DIR, c->name);
    mkdir(dir, 0755);
    snprintf(path, sizeof(path), "%s/%s", dir, fname);

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        reply(c->fd, "ERR", "500 STORAGE_ERROR");
        return;
    }

    /* 1. Write bytes already present in receive buffer */
    long long left = fsize;
    int buffered = *rx_len - offset;
    if (buffered > 0) {
        int w = (buffered > left) ? (int)left : buffered;
        fwrite(rx + offset, 1, w, fp);
        left -= w;
        int rem = buffered - w;
        if (rem > 0) memmove(rx, rx + offset + w, rem);
        *rx_len = rem;
    } else {
        *rx_len = 0;
    }

    /* 2. Read remaining file bytes directly from socket */
    char chunk[BUF_SIZE];
    while (left > 0) {
        int rd = (left > (long long)sizeof(chunk)) ? (int)sizeof(chunk) : (int)left;
        ssize_t n = recv(c->fd, chunk, rd, 0);
        if (n <= 0) { fclose(fp); return; }
        fwrite(chunk, 1, n, fp);
        left -= n;
    }
    fclose(fp);

    /* Relay file to target client or room */
    char hdr[256];
    snprintf(hdr, sizeof(hdr), "MSG FILE %s %s %lld\n", c->name, fname, fsize);

    FILE *rfp = fopen(path, "rb");
    if (rfp) {
        if (target_fd != -1) {
            send(target_fd, hdr, strlen(hdr), 0);
            size_t n;
            while ((n = fread(chunk, 1, sizeof(chunk), rfp)) > 0) {
                send(target_fd, chunk, n, 0);
            }
        } else if (r) {
            pthread_mutex_lock(&lock);
            for (int i = 0; i < r->count; i++) {
                if (r->fds[i] != c->fd) {
                    send(r->fds[i], hdr, strlen(hdr), 0);
                    fseek(rfp, 0, SEEK_SET);
                    size_t n;
                    while ((n = fread(chunk, 1, sizeof(chunk), rfp)) > 0) {
                        send(r->fds[i], chunk, n, 0);
                    }
                }
            }
            pthread_mutex_unlock(&lock);
        }
        fclose(rfp);
    }

    char ok_msg[160];
    snprintf(ok_msg, sizeof(ok_msg), "FILE_RECEIVED %s", fname);
    reply(c->fd, "OK", ok_msg);

    char log_buf[256];
    snprintf(log_buf, sizeof(log_buf), "File '%s' (%lld bytes) from '%s' to '%s'", fname, fsize, c->name, target);
    log_msg("FILE_TRANSFER", log_buf);
}

/* Worker thread servicing a connected client */
void* client_thread(void *arg) {
    client_t *c = (client_t *)arg;
    char rx[BUF_SIZE];
    int rx_len = 0;

    /* Worksheet 2 Task 7: getpeername, Task 4: inet_ntoa, Task 3: ntohs */
    struct sockaddr_in peer;
    socklen_t plen = sizeof(peer);
    if (getpeername(c->fd, (struct sockaddr *)&peer, &plen) == 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Client connected from %s:%d", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
        log_msg("CONNECT", msg);
    }

    while (1) {
        ssize_t n = recv(c->fd, rx + rx_len, sizeof(rx) - rx_len - 1, 0);
        if (n <= 0) break;
        rx_len += n;
        rx[rx_len] = '\0';

        /* Parse lines ending with newline */
        while (1) {
            char *nl = strchr(rx, '\n');
            if (!nl) break;
            *nl = '\0';

            char line[BUF_SIZE];
            snprintf(line, sizeof(line), "%s", rx);
            if (strlen(line) > 0 && line[strlen(line) - 1] == '\r') line[strlen(line) - 1] = '\0';

            int consumed = (nl - rx) + 1;

            /* Extract command and arguments */
            char cmd[32] = {0}, *args = "";
            char *sp = strchr(line, ' ');
            if (sp) {
                *sp = '\0';
                snprintf(cmd, sizeof(cmd), "%s", line);
                args = sp + 1;
                while (*args == ' ') args++;
            } else {
                snprintf(cmd, sizeof(cmd), "%s", line);
            }

            /* 1. QUIT */
            if (strcasecmp(cmd, "QUIT") == 0) {
                reply(c->fd, "OK", "BYE");
                disconnect_client(c);
                return NULL;
            }

            /* 2. REGISTER <username> */
            if (strcasecmp(cmd, "REGISTER") == 0) {
                if (strlen(args) == 0) {
                    reply(c->fd, "ERR", "007 BAD_ARGUMENTS");
                } else {
                    pthread_mutex_lock(&lock);
                    if (c->registered) {
                        pthread_mutex_unlock(&lock);
                        reply(c->fd, "ERR", "001 ALREADY_REGISTERED");
                    } else if (find_client(args)) {
                        pthread_mutex_unlock(&lock);
                        reply(c->fd, "ERR", "001 USERNAME_TAKEN");
                    } else {
                        snprintf(c->name, sizeof(c->name), "%s", args);
                        c->registered = 1;
                        pthread_mutex_unlock(&lock);

                        char ok_msg[64];
                        snprintf(ok_msg, sizeof(ok_msg), "REGISTERED %s", c->name);
                        reply(c->fd, "OK", ok_msg);

                        char log_buf[64], bcast[128];
                        snprintf(log_buf, sizeof(log_buf), "User '%s' registered", c->name);
                        log_msg("REGISTER", log_buf);

                        snprintf(bcast, sizeof(bcast), "MSG BCAST server %s has joined the chat\n", c->name);
                        pthread_mutex_lock(&lock);
                        broadcast(bcast, c->fd);
                        pthread_mutex_unlock(&lock);
                    }
                }
            }
            /* Remaining commands require registration */
            else if (!c->registered) {
                reply(c->fd, "ERR", "005 UNREGISTERED");
            }
            /* 3. LIST */
            else if (strcasecmp(cmd, "LIST") == 0) {
                char list[1024] = {0};
                int first = 1;
                pthread_mutex_lock(&lock);
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].active && clients[i].registered) {
                        if (!first) strncat(list, ",", sizeof(list) - strlen(list) - 1);
                        strncat(list, clients[i].name, sizeof(list) - strlen(list) - 1);
                        first = 0;
                    }
                }
                pthread_mutex_unlock(&lock);
                char res[1100];
                snprintf(res, sizeof(res), "USERS %s", list);
                reply(c->fd, "OK", res);
            }
            /* 4. BCAST <message> */
            else if (strcasecmp(cmd, "BCAST") == 0) {
                if (strlen(args) == 0) {
                    reply(c->fd, "ERR", "007 BAD_ARGUMENTS");
                } else {
                    reply(c->fd, "OK", "SENT");
                    char fwd[BUF_SIZE + 64];
                    snprintf(fwd, sizeof(fwd), "MSG BCAST %s %s\n", c->name, args);
                    pthread_mutex_lock(&lock);
                    broadcast(fwd, c->fd);
                    pthread_mutex_unlock(&lock);
                }
            }
            /* 5. PMSG <target> <message> */
            else if (strcasecmp(cmd, "PMSG") == 0) {
                char *msg = strchr(args, ' ');
                if (!msg) {
                    reply(c->fd, "ERR", "007 BAD_ARGUMENTS");
                } else {
                    *msg = '\0';
                    msg++;
                    while (*msg == ' ') msg++;
                    pthread_mutex_lock(&lock);
                    client_t *t = find_client(args);
                    if (t) {
                        char priv[BUF_SIZE + 64];
                        snprintf(priv, sizeof(priv), "MSG PRIV %s %s\n", c->name, msg);
                        send(t->fd, priv, strlen(priv), 0);
                        pthread_mutex_unlock(&lock);
                        reply(c->fd, "OK", "SENT");
                    } else {
                        pthread_mutex_unlock(&lock);
                        reply(c->fd, "ERR", "002 USER_NOT_FOUND");
                    }
                }
            }
            /* 6. JOIN <room> */
            else if (strcasecmp(cmd, "JOIN") == 0) {
                if (strlen(args) == 0) {
                    reply(c->fd, "ERR", "007 BAD_ARGUMENTS");
                } else {
                    pthread_mutex_lock(&lock);
                    room_t *r = NULL;
                    for (int i = 0; i < MAX_ROOMS; i++) {
                        if (rooms[i].active && strcmp(rooms[i].name, args) == 0) { r = &rooms[i]; break; }
                    }
                    if (!r) {
                        for (int i = 0; i < MAX_ROOMS; i++) {
                            if (!rooms[i].active) {
                                r = &rooms[i]; r->active = 1; snprintf(r->name, sizeof(r->name), "%s", args); r->count = 0;
                                break;
                            }
                        }
                    }
                    if (r && r->count < MAX_CLIENTS) {
                        r->fds[r->count++] = c->fd;
                        pthread_mutex_unlock(&lock);
                        char ok[64];
                        snprintf(ok, sizeof(ok), "JOINED %s", args);
                        reply(c->fd, "OK", ok);
                    } else {
                        pthread_mutex_unlock(&lock);
                        reply(c->fd, "ERR", "500 ROOM_FULL");
                    }
                }
            }
            /* 7. LEAVE <room> */
            else if (strcasecmp(cmd, "LEAVE") == 0) {
                int found = 0;
                pthread_mutex_lock(&lock);
                for (int i = 0; i < MAX_ROOMS; i++) {
                    if (rooms[i].active && strcmp(rooms[i].name, args) == 0) {
                        for (int j = 0; j < rooms[i].count; j++) {
                            if (rooms[i].fds[j] == c->fd) {
                                rooms[i].fds[j] = rooms[i].fds[--rooms[i].count];
                                found = 1; break;
                            }
                        }
                        if (rooms[i].count == 0) rooms[i].active = 0;
                        break;
                    }
                }
                pthread_mutex_unlock(&lock);
                if (found) {
                    char ok[64];
                    snprintf(ok, sizeof(ok), "LEFT %s", args);
                    reply(c->fd, "OK", ok);
                } else {
                    reply(c->fd, "ERR", "003 ROOM_NOT_FOUND");
                }
            }
            /* 8. ROOMS */
            else if (strcasecmp(cmd, "ROOMS") == 0) {
                char list[1024] = {0};
                int first = 1;
                pthread_mutex_lock(&lock);
                for (int i = 0; i < MAX_ROOMS; i++) {
                    if (rooms[i].active) {
                        if (!first) strncat(list, ",", sizeof(list) - strlen(list) - 1);
                        strncat(list, rooms[i].name, sizeof(list) - strlen(list) - 1);
                        first = 0;
                    }
                }
                pthread_mutex_unlock(&lock);
                char res[1100];
                snprintf(res, sizeof(res), "ROOMS %s", list);
                reply(c->fd, "OK", res);
            }
            /* 9. RMSG <room> <message> */
            else if (strcasecmp(cmd, "RMSG") == 0) {
                char *msg = strchr(args, ' ');
                if (!msg) {
                    reply(c->fd, "ERR", "007 BAD_ARGUMENTS");
                } else {
                    *msg = '\0';
                    msg++;
                    while (*msg == ' ') msg++;
                    int sent = 0;
                    pthread_mutex_lock(&lock);
                    for (int i = 0; i < MAX_ROOMS; i++) {
                        if (rooms[i].active && strcmp(rooms[i].name, args) == 0) {
                            char fwd[BUF_SIZE + 64];
                            snprintf(fwd, sizeof(fwd), "MSG ROOM %s %s %s\n", args, c->name, msg);
                            for (int j = 0; j < rooms[i].count; j++) {
                                if (rooms[i].fds[j] != c->fd) send(rooms[i].fds[j], fwd, strlen(fwd), 0);
                            }
                            sent = 1; break;
                        }
                    }
                    pthread_mutex_unlock(&lock);
                    if (sent) reply(c->fd, "OK", "SENT");
                    else reply(c->fd, "ERR", "003 ROOM_NOT_FOUND");
                }
            }
            /* 10. SENDFILE <target> <filename> <filesize> */
            else if (strcasecmp(cmd, "SENDFILE") == 0) {
                handle_sendfile(c, args, rx, &rx_len, consumed);
                break;
            }
            else {
                reply(c->fd, "ERR", "006 UNKNOWN_COMMAND");
            }

            memmove(rx, rx + consumed, rx_len - consumed);
            rx_len -= consumed;
        }
    }

    disconnect_client(c);
    return NULL;
}

int main(int argc, char *argv[]) {
    int port = (argc > 1) ? atoi(argv[1]) : PORT;

    printf("NetMessenger Server (IT23718708) listening on port %d [%s]\n", port, NID_TAG);

    /* Lecture 05 Slide 50: ignore SIGPIPE to prevent crash on unexpected disconnects */
    signal(SIGPIPE, SIG_IGN);

    int sfd = socket(AF_INET, SOCK_STREAM, 0);

    /* Practical 7 Task 5: setsockopt with SO_REUSEADDR */
    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Worksheet 2 Task 6: memset instead of bzero */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port); /* Worksheet 2 Task 3: htons */

    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        return 1;
    }
    listen(sfd, 16);

    /* Worksheet 2 Task 7: getsockname to verify local bound port */
    struct sockaddr_in local;
    socklen_t local_len = sizeof(local);
    if (getsockname(sfd, (struct sockaddr *)&local, &local_len) == 0) {
        printf("[getsockname] Bound to port: %d\n", ntohs(local.sin_port));
    }

    /* Initialize storage base directories */
    mkdir("./storage", 0755);
    mkdir(STORAGE_DIR, 0755);

    log_msg("STARTUP", "Server started");

    while (1) {
        struct sockaddr_in caddr;
        socklen_t len = sizeof(caddr);
        int cfd = accept(sfd, (struct sockaddr *)&caddr, &len);
        /* Worksheet 4 Task 13: Handle interrupted slow system call EINTR */
        if (cfd < 0) {
            if (errno == EINTR) continue;
            continue;
        }

        pthread_mutex_lock(&lock);
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active) { slot = i; break; }
        }

        if (slot != -1) {
            clients[slot].fd = cfd;
            clients[slot].active = 1;
            clients[slot].registered = 0;
            pthread_t tid;
            pthread_create(&tid, NULL, client_thread, &clients[slot]);
            pthread_detach(tid);
        } else {
            close(cfd);
        }
        pthread_mutex_unlock(&lock);
    }

    close(sfd);
    return 0;
}
