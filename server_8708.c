#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <signal.h>

#define PORT          14708
#define NID_TAG       "NID:7187"
#define LOG_FILE      "netmsg_IT23718708.log"
#define STORAGE_DIR   "./storage/IT23718708"
#define MAX_CLIENTS   64
#define MAX_ROOMS     32
#define BUF_SIZE      4096
#define MAX_FILE_SIZE (50 * 1024 * 1024)

typedef struct {
    int fd;
    char name[32];
    char ip[48];
    int port;
    int registered;
    int active;
} client_t;

typedef struct {
    char name[32];
    int fds[MAX_CLIENTS];
    int count;
    int active;
} room_t;

static client_t clients[MAX_CLIENTS];
static room_t   rooms[MAX_ROOMS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

void log_msg(const char *tag, const char *msg) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    pthread_mutex_lock(&log_lock);
    printf("[%s] [%s] %s\n", buf, tag, msg);
    fflush(stdout);
    FILE *fp = fopen(LOG_FILE, "a");
    if (fp) {
        fprintf(fp, "[%s] [%s] %s\n", buf, tag, msg);
        fclose(fp);
    }
    pthread_mutex_unlock(&log_lock);
}

ssize_t send_all(int fd, const void *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(fd, (const char *)buf + total, len - total, 0);
        if (n <= 0) return n;
        total += n;
    }
    return total;
}

void reply(int fd, const char *status, const char *msg) {
    char out[1024];
    snprintf(out, sizeof(out), "%s %s %s\n", status, msg, NID_TAG);
    send_all(fd, out, strlen(out));
}

void broadcast(const char *msg, int exclude_fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].registered && clients[i].fd != exclude_fd)
            send_all(clients[i].fd, msg, strlen(msg));
    }
}

client_t* find_client(const char *name) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].registered && strcasecmp(clients[i].name, name) == 0)
            return &clients[i];
    }
    return NULL;
}

room_t* find_room(const char *name) {
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].active && strcmp(rooms[i].name, name) == 0) return &rooms[i];
    }
    return NULL;
}

void disconnect_client(client_t *c) {
    char name[32] = {0};
    int was_reg = 0, fd = -1;
    pthread_mutex_lock(&lock);
    if (!c->active) { pthread_mutex_unlock(&lock); return; }
    fd = c->fd;
    if (c->registered) { snprintf(name, sizeof(name), "%s", c->name); was_reg = 1; }
    c->active = c->registered = 0;
    c->fd = -1;
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
    if (fd != -1) { shutdown(fd, SHUT_WR); close(fd); }
    char msg[128];
    if (was_reg) {
        snprintf(msg, sizeof(msg), "User '%s' disconnected", name);
        log_msg("DISCONNECT", msg);
        snprintf(msg, sizeof(msg), "MSG BCAST server %s has left the chat\n", name);
        pthread_mutex_lock(&lock);
        broadcast(msg, -1);
        pthread_mutex_unlock(&lock);
    } else {
        snprintf(msg, sizeof(msg), "Unregistered client disconnected (fd=%d)", fd);
        log_msg("DISCONNECT", msg);
    }
}

void drain_bytes(int fd, char *rx, int *rx_len, long long bytes) {
    long long left = bytes;
    if (*rx_len > 0) {
        int w = (*rx_len > left) ? (int)left : *rx_len;
        left -= w;
        int rem = *rx_len - w;
        if (rem > 0) memmove(rx, rx + w, rem);
        *rx_len = rem;
        rx[*rx_len] = '\0';
    }
    char buf[BUF_SIZE];
    while (left > 0) {
        int rd = (left > (long long)sizeof(buf)) ? (int)sizeof(buf) : (int)left;
        ssize_t n = recv(fd, buf, rd, 0);
        if (n <= 0) break;
        left -= n;
    }
}

void handle_sendfile(client_t *c, const char *args, char *rx, int *rx_len) {
    char target[32] = {0}, raw_fname[128] = {0};
    long long fsize = 0;
    if (sscanf(args, "%31s %127s %lld", target, raw_fname, &fsize) != 3 || fsize <= 0) {
        reply(c->fd, "ERR", "007 BAD_ARGUMENTS");
        log_msg("ERROR", "Invalid SENDFILE arguments");
        return;
    }
    const char *b = strrchr(raw_fname, '/');
    if (!b) b = strrchr(raw_fname, '\\');
    const char *fname = b ? b + 1 : raw_fname;
    if (!*fname) fname = "file.bin";

    if (fsize > MAX_FILE_SIZE) {
        reply(c->fd, "ERR", "004 FILE_TOO_LARGE");
        drain_bytes(c->fd, rx, rx_len, fsize);
        log_msg("FILE_ERR", "File rejected: FILE_TOO_LARGE");
        return;
    }
    pthread_mutex_lock(&lock);
    client_t *t = find_client(target);
    int target_fd = t ? t->fd : -1;
    room_t *r = (target_fd == -1) ? find_room(target) : NULL;
    pthread_mutex_unlock(&lock);

    if (target_fd == -1 && !r) {
        reply(c->fd, "ERR", "002 USER_NOT_FOUND");
        drain_bytes(c->fd, rx, rx_len, fsize);
        log_msg("FILE_ERR", "File target not found");
        return;
    }

    mkdir("./storage", 0755);
    mkdir(STORAGE_DIR, 0755);
    char dir[256], path[384];
    snprintf(dir, sizeof(dir), "%s/%s", STORAGE_DIR, c->name);
    mkdir(dir, 0755);
    snprintf(path, sizeof(path), "%s/%s", dir, fname);

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        reply(c->fd, "ERR", "500 STORAGE_ERROR");
        drain_bytes(c->fd, rx, rx_len, fsize);
        return;
    }

    long long left = fsize;
    if (*rx_len > 0) {
        int w = (*rx_len > left) ? (int)left : *rx_len;
        fwrite(rx, 1, w, fp);
        left -= w;
        int rem = *rx_len - w;
        if (rem > 0) memmove(rx, rx + w, rem);
        *rx_len = rem;
        rx[*rx_len] = '\0';
    }
    char chunk[BUF_SIZE];
    while (left > 0) {
        int rd = (left > (long long)sizeof(chunk)) ? (int)sizeof(chunk) : (int)left;
        ssize_t n = recv(c->fd, chunk, rd, 0);
        if (n <= 0) { fclose(fp); remove(path); return; }
        fwrite(chunk, 1, n, fp);
        left -= n;
    }
    fclose(fp);

    char hdr[256];
    snprintf(hdr, sizeof(hdr), "MSG FILE %s %s %lld\n", c->name, fname, fsize);
    FILE *rfp = fopen(path, "rb");
    if (rfp) {
        pthread_mutex_lock(&lock);
        t = find_client(target);
        target_fd = t ? t->fd : -1;
        r = (target_fd == -1) ? find_room(target) : NULL;
        if (target_fd != -1) {
            send_all(target_fd, hdr, strlen(hdr));
            size_t n;
            while ((n = fread(chunk, 1, sizeof(chunk), rfp)) > 0) send_all(target_fd, chunk, n);
        } else if (r) {
            for (int i = 0; i < r->count; i++) {
                if (r->fds[i] != c->fd) {
                    send_all(r->fds[i], hdr, strlen(hdr));
                    fseek(rfp, 0, SEEK_SET);
                    size_t n;
                    while ((n = fread(chunk, 1, sizeof(chunk), rfp)) > 0) send_all(r->fds[i], chunk, n);
                }
            }
        }
        pthread_mutex_unlock(&lock);
        fclose(rfp);
    }
    char ok[160];
    snprintf(ok, sizeof(ok), "FILE_RECEIVED %s", fname);
    reply(c->fd, "OK", ok);
    char lbuf[256];
    snprintf(lbuf, sizeof(lbuf), "File '%s' (%lld bytes) from '%s' to '%s'", fname, fsize, c->name, target);
    log_msg("FILE_TRANSFER", lbuf);
}

void* client_thread(void *arg) {
    client_t *c = (client_t *)arg;
    char rx[BUF_SIZE];
    int rx_len = 0;

    struct sockaddr_in peer;
    socklen_t plen = sizeof(peer);
    if (getpeername(c->fd, (struct sockaddr *)&peer, &plen) == 0) {
        snprintf(c->ip, sizeof(c->ip), "%s", inet_ntoa(peer.sin_addr));
        c->port = ntohs(peer.sin_port);
        char msg[128];
        snprintf(msg, sizeof(msg), "Client connected from %s:%d (fd=%d)", c->ip, c->port, c->fd);
        log_msg("CONNECT", msg);
    }

    while (1) {
        ssize_t n = recv(c->fd, rx + rx_len, sizeof(rx) - rx_len - 1, 0);
        if (n <= 0) break;
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

            if (strcasecmp(cmd, "QUIT") == 0) {
                log_msg("QUIT", c->name[0] ? c->name : "Unregistered");
                reply(c->fd, "OK", "BYE");
                disconnect_client(c);
                return NULL;
            }
            if (strcasecmp(cmd, "REGISTER") == 0) {
                if (!*args) {
                    reply(c->fd, "ERR", "007 BAD_ARGUMENTS");
                } else {
                    pthread_mutex_lock(&lock);
                    if (c->registered) {
                        pthread_mutex_unlock(&lock);
                        reply(c->fd, "ERR", "001 ALREADY_REGISTERED");
                    } else if (find_client(args)) {
                        pthread_mutex_unlock(&lock);
                        reply(c->fd, "ERR", "001 USERNAME_TAKEN");
                        log_msg("REGISTER_ERR", "Username taken");
                    } else {
                        snprintf(c->name, sizeof(c->name), "%s", args);
                        c->registered = 1;
                        pthread_mutex_unlock(&lock);

                        char ok_msg[64], lbuf[64], bcast[128];
                        snprintf(ok_msg, sizeof(ok_msg), "REGISTERED %s", c->name);
                        reply(c->fd, "OK", ok_msg);
                        snprintf(lbuf, sizeof(lbuf), "User '%s' registered", c->name);
                        log_msg("REGISTER", lbuf);

                        snprintf(bcast, sizeof(bcast), "MSG BCAST server %s has joined the chat\n", c->name);
                        pthread_mutex_lock(&lock);
                        broadcast(bcast, c->fd);
                        pthread_mutex_unlock(&lock);
                    }
                }
            }
            else if (!c->registered) {
                if (strcasecmp(cmd, "SENDFILE") == 0) {
                    char t[32], f[128]; long long sz = 0;
                    if (sscanf(args, "%31s %127s %lld", t, f, &sz) == 3 && sz > 0)
                        drain_bytes(c->fd, rx, &rx_len, sz);
                }
                reply(c->fd, "ERR", "005 UNREGISTERED");
            }
            else if (strcasecmp(cmd, "LIST") == 0) {
                char list[1024] = {0}; int first = 1;
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
                char lbuf[64];
                snprintf(lbuf, sizeof(lbuf), "User '%s' requested user list", c->name);
                log_msg("LIST", lbuf);
            }
            else if (strcasecmp(cmd, "BCAST") == 0) {
                if (!*args) {
                    reply(c->fd, "ERR", "007 BAD_ARGUMENTS");
                } else {
                    reply(c->fd, "OK", "SENT");
                    char lbuf[BUF_SIZE + 64], fwd[BUF_SIZE + 64];
                    snprintf(lbuf, sizeof(lbuf), "User '%s' broadcast: %s", c->name, args);
                    log_msg("BCAST", lbuf);
                    snprintf(fwd, sizeof(fwd), "MSG BCAST %s %s\n", c->name, args);
                    pthread_mutex_lock(&lock);
                    broadcast(fwd, c->fd);
                    pthread_mutex_unlock(&lock);
                }
            }
            else if (strcasecmp(cmd, "PMSG") == 0) {
                char target[32] = {0};
                char *sp_t = strchr(args, ' ');
                if (!sp_t) {
                    reply(c->fd, "ERR", "007 BAD_ARGUMENTS");
                } else {
                    size_t tlen = sp_t - args;
                    if (tlen >= sizeof(target)) tlen = sizeof(target) - 1;
                    memcpy(target, args, tlen);
                    target[tlen] = '\0';
                    char *msg = sp_t + 1;
                    while (*msg == ' ') msg++;
                    if (!*msg) {
                        reply(c->fd, "ERR", "007 BAD_ARGUMENTS");
                    } else {
                        pthread_mutex_lock(&lock);
                        client_t *t = find_client(target);
                        if (t) {
                            char priv[BUF_SIZE + 64];
                            snprintf(priv, sizeof(priv), "MSG PRIV %s %s\n", c->name, msg);
                            send_all(t->fd, priv, strlen(priv));
                            pthread_mutex_unlock(&lock);
                            reply(c->fd, "OK", "SENT");
                            char lbuf[BUF_SIZE + 64];
                            snprintf(lbuf, sizeof(lbuf), "%s -> %s: %s", c->name, target, msg);
                            log_msg("PMSG", lbuf);
                        } else {
                            pthread_mutex_unlock(&lock);
                            reply(c->fd, "ERR", "002 USER_NOT_FOUND");
                        }
                    }
                }
            }
            else if (strcasecmp(cmd, "JOIN") == 0) {
                if (!*args) {
                    reply(c->fd, "ERR", "007 BAD_ARGUMENTS");
                } else {
                    pthread_mutex_lock(&lock);
                    room_t *r = find_room(args);
                    if (!r) {
                        for (int i = 0; i < MAX_ROOMS; i++) {
                            if (!rooms[i].active) {
                                r = &rooms[i]; r->active = 1;
                                snprintf(r->name, sizeof(r->name), "%s", args);
                                r->count = 0; break;
                            }
                        }
                    }
                    int joined = 0;
                    if (r) {
                        int already = 0;
                        for (int j = 0; j < r->count; j++) {
                            if (r->fds[j] == c->fd) { already = 1; break; }
                        }
                        if (already) joined = 1;
                        else if (r->count < MAX_CLIENTS) { r->fds[r->count++] = c->fd; joined = 1; }
                    }
                    pthread_mutex_unlock(&lock);
                    if (joined) {
                        char ok[64], lbuf[64];
                        snprintf(ok, sizeof(ok), "JOINED %s", args);
                        reply(c->fd, "OK", ok);
                        snprintf(lbuf, sizeof(lbuf), "User '%s' joined room '%s'", c->name, args);
                        log_msg("JOIN", lbuf);
                    } else reply(c->fd, "ERR", "500 ROOM_FULL");
                }
            }
            else if (strcasecmp(cmd, "LEAVE") == 0) {
                int found = 0;
                pthread_mutex_lock(&lock);
                room_t *r = find_room(args);
                if (r) {
                    for (int j = 0; j < r->count; j++) {
                        if (r->fds[j] == c->fd) {
                            r->fds[j] = r->fds[--r->count];
                            found = 1; break;
                        }
                    }
                    if (r->count == 0) r->active = 0;
                }
                pthread_mutex_unlock(&lock);
                if (found) {
                    char ok[64], lbuf[64];
                    snprintf(ok, sizeof(ok), "LEFT %s", args);
                    reply(c->fd, "OK", ok);
                    snprintf(lbuf, sizeof(lbuf), "User '%s' left room '%s'", c->name, args);
                    log_msg("LEAVE", lbuf);
                } else reply(c->fd, "ERR", "003 ROOM_NOT_FOUND");
            }
            else if (strcasecmp(cmd, "ROOMS") == 0) {
                char list[1024] = {0}; int first = 1;
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
                char lbuf[64];
                snprintf(lbuf, sizeof(lbuf), "User '%s' requested room list", c->name);
                log_msg("ROOMS", lbuf);
            }
            else if (strcasecmp(cmd, "RMSG") == 0) {
                char *sp_r = strchr(args, ' ');
                if (!sp_r) {
                    reply(c->fd, "ERR", "007 BAD_ARGUMENTS");
                } else {
                    *sp_r = '\0';
                    char *msg = sp_r + 1;
                    while (*msg == ' ') msg++;
                    if (!*msg) {
                        reply(c->fd, "ERR", "007 BAD_ARGUMENTS");
                    } else {
                        int sent = 0;
                        pthread_mutex_lock(&lock);
                        room_t *r = find_room(args);
                        if (r) {
                            char fwd[BUF_SIZE + 64];
                            snprintf(fwd, sizeof(fwd), "MSG ROOM %s %s %s\n", args, c->name, msg);
                            for (int j = 0; j < r->count; j++) {
                                if (r->fds[j] != c->fd) send_all(r->fds[j], fwd, strlen(fwd));
                            }
                            sent = 1;
                        }
                        pthread_mutex_unlock(&lock);
                        if (sent) {
                            reply(c->fd, "OK", "SENT");
                            char lbuf[BUF_SIZE + 64];
                            snprintf(lbuf, sizeof(lbuf), "User '%s' in room '%s': %s", c->name, args, msg);
                            log_msg("RMSG", lbuf);
                        } else reply(c->fd, "ERR", "003 ROOM_NOT_FOUND");
                    }
                }
            }
            else if (strcasecmp(cmd, "SENDFILE") == 0) {
                handle_sendfile(c, args, rx, &rx_len);
            }
            else {
                reply(c->fd, "ERR", "006 UNKNOWN_COMMAND");
            }
        }
    }
    disconnect_client(c);
    return NULL;
}

int main(int argc, char *argv[]) {
    int port = (argc > 1) ? atoi(argv[1]) : PORT;
    printf("NetMessenger Server listening on port %d [%s]\n", port, NID_TAG);
    signal(SIGPIPE, SIG_IGN);

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket error"); return 1; }

    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        return 1;
    }
    listen(sfd, 16);

    mkdir("./storage", 0755);
    mkdir(STORAGE_DIR, 0755);
    log_msg("STARTUP", "Server started");

    while (1) {
        struct sockaddr_in caddr;
        socklen_t len = sizeof(caddr);
        int cfd = accept(sfd, (struct sockaddr *)&caddr, &len);
        if (cfd < 0) continue;

        pthread_mutex_lock(&lock);
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active) { slot = i; break; }
        }
        if (slot != -1) {
            clients[slot].fd = cfd;
            clients[slot].active = 1;
            clients[slot].registered = 0;
            clients[slot].name[0] = clients[slot].ip[0] = '\0';
            clients[slot].port = 0;
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
