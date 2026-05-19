/*
 * serv7.c - Servidor de noticias News7
 *
 * Uso: serv7 [-p puerto] [-d días] [-a directorio_articulos]
 *
 * El servidor escucha conexiones TCP en el puerto indicado,
 * gestiona múltiples clientes simultáneamente con select(),
 * almacena artículos en disco y los difunde a clientes conectados.
 *
 * SEÑALES:
 *   SIGINT / SIGTERM: detiene el servidor limpiamente, notificando
 *                     a todos los clientes conectados.
 *
 * ALMACENAMIENTO:
 *   Cada artículo se guarda en un fichero dentro de articles_dir con
 *   el nombre: TIMESTAMP_NICK.art
 *   Formato interno del fichero:
 *     NICK: <nick>\n
 *     DATE: <timestamp_unix>\n
 *     SUBJECT: <asunto>\n
 *     ---\n
 *     <cuerpo del artículo>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../include/protocol.h"

/* Contexto para callback de artículos */
typedef struct { int fd; } ArtCtx;

static void send_article_cb(const char *nick, const char *subject,
                            const char *body, time_t ts, void *ud) {
    (void)ts;
    ArtCtx *ctx = (ArtCtx *)ud;
    char payload_buf[MAX_MSG];
    snprintf(payload_buf, sizeof(payload_buf),
             "FROM:%s\nSUBJECT:%s\n---\n%s", nick, subject, body);
    send_message(ctx->fd, MSG_NEWS_ITEM, payload_buf);
}

/* -------------------------------------------------------------------------
 * Estructuras internas
 * ---------------------------------------------------------------------- */

typedef struct {
    int  fd;
    char nick[MAX_NICK];
    int  authenticated;
} Client;

/* -------------------------------------------------------------------------
 * Variables globales
 * ---------------------------------------------------------------------- */

static int      g_running    = 1;
static int      g_server_fd  = -1;
static Client   g_clients[MAX_CLIENTS];
static int      g_num_clients = 0;
static int      g_port       = DEFAULT_PORT;
static int      g_days       = DEFAULT_DAYS;
static char     g_articles_dir[256] = "./articles";

/* -------------------------------------------------------------------------
 * Señales
 * ---------------------------------------------------------------------- */

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* -------------------------------------------------------------------------
 * Gestión de clientes
 * ---------------------------------------------------------------------- */

static void client_add(int fd) {
    if (g_num_clients >= MAX_CLIENTS) {
        close(fd);
        return;
    }
    g_clients[g_num_clients].fd = fd;
    g_clients[g_num_clients].nick[0] = '\0';
    g_clients[g_num_clients].authenticated = 0;
    g_num_clients++;
    printf("[SERVER] Nueva conexión fd=%d (total=%d)\n", fd, g_num_clients);
}

static void client_remove(int idx) {
    printf("[SERVER] Cliente '%s' desconectado (fd=%d)\n",
           g_clients[idx].nick[0] ? g_clients[idx].nick : "?",
           g_clients[idx].fd);
    close(g_clients[idx].fd);
    /* Rellenar hueco con el último */
    g_clients[idx] = g_clients[g_num_clients - 1];
    g_num_clients--;
}

static int nick_exists(const char *nick) {
    for (int i = 0; i < g_num_clients; i++) {
        if (g_clients[i].authenticated &&
            strcmp(g_clients[i].nick, nick) == 0)
            return 1;
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * Almacenamiento de artículos
 * ---------------------------------------------------------------------- */

static int save_article(const char *nick, const char *subject,
                        const char *body, time_t ts) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%ld_%s.art",
             g_articles_dir, (long)ts, nick);

    FILE *f = fopen(path, "w");
    if (!f) {
        perror("fopen save_article");
        return -1;
    }
    fprintf(f, "NICK: %s\n", nick);
    fprintf(f, "DATE: %ld\n", (long)ts);
    fprintf(f, "SUBJECT: %s\n", subject);
    fprintf(f, "---\n");
    fprintf(f, "%s", body);
    fclose(f);
    printf("[SERVER] Artículo guardado: %s\n", path);
    return 0;
}

/*
 * load_articles_since - Carga artículos de los últimos 'days' días.
 * Para cada artículo llama a callback(nick, subject, body, ts, userdata).
 */
typedef void (*ArticleCallback)(const char *nick, const char *subject,
                                const char *body, time_t ts, void *ud);

static void load_articles_since(int days, ArticleCallback cb, void *ud) {
    time_t cutoff = time(NULL) - (time_t)days * 86400;
    DIR *dp = opendir(g_articles_dir);
    if (!dp) return;

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (strstr(de->d_name, ".art") == NULL) continue;

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", g_articles_dir, de->d_name);

        FILE *f = fopen(path, "r");
        if (!f) continue;

        char nick[MAX_NICK] = {0};
        char subject[MAX_SUBJECT] = {0};
        char body[MAX_BODY] = {0};
        time_t ts = 0;
        char line[512];

        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "NICK: ", 6) == 0) {
                line[strcspn(line, "\n")] = '\0';
                strncpy(nick, line + 6, MAX_NICK - 1);
            } else if (strncmp(line, "DATE: ", 6) == 0) {
                ts = (time_t)atol(line + 6);
            } else if (strncmp(line, "SUBJECT: ", 9) == 0) {
                line[strcspn(line, "\n")] = '\0';
                strncpy(subject, line + 9, MAX_SUBJECT - 1);
            } else if (strcmp(line, "---\n") == 0) {
                /* Leer el cuerpo */
                size_t off = 0;
                while (fgets(line, sizeof(line), f) && off < MAX_BODY - 1) {
                    size_t l = strlen(line);
                    if (off + l >= MAX_BODY - 1) l = MAX_BODY - 1 - off;
                    memcpy(body + off, line, l);
                    off += l;
                }
                body[off] = '\0';
                break;
            }
        }
        fclose(f);

        if (ts >= cutoff && nick[0] && subject[0])
            cb(nick, subject, body, ts, ud);
    }
    closedir(dp);
}

/* -------------------------------------------------------------------------
 * Difusión a todos los clientes autenticados
 * ---------------------------------------------------------------------- */

static void broadcast(int skip_fd, const char *type, const char *payload) {
    for (int i = 0; i < g_num_clients; i++) {
        if (g_clients[i].authenticated && g_clients[i].fd != skip_fd)
            send_message(g_clients[i].fd, type, payload);
    }
}

/* -------------------------------------------------------------------------
 * Procesado de mensajes de un cliente
 * ---------------------------------------------------------------------- */

static void handle_get_news(int client_idx, const char *payload) {
    int days = atoi(payload);
    if (days <= 0) days = g_days;

    ArtCtx ctx = { g_clients[client_idx].fd };
    load_articles_since(days, send_article_cb, &ctx);
    send_message(g_clients[client_idx].fd, MSG_NEWS_END, "");
    printf("[SERVER] Noticias de %d días enviadas a '%s'\n",
           days, g_clients[client_idx].nick);
}

static void handle_publish(int client_idx, const char *payload) {
    /* payload: "SUBJECT:<asunto>\n---\n<cuerpo>" */
    char subject[MAX_SUBJECT] = {0};
    char body[MAX_BODY] = {0};

    const char *p = payload;
    /* Parsear SUBJECT: */
    if (strncmp(p, "SUBJECT:", 8) == 0) {
        p += 8;
        const char *nl = strchr(p, '\n');
        if (nl) {
            size_t slen = (size_t)(nl - p);
            if (slen >= MAX_SUBJECT) slen = MAX_SUBJECT - 1;
            strncpy(subject, p, slen);
            p = nl + 1;
        }
    }
    /* Saltar "---\n" */
    if (strncmp(p, "---\n", 4) == 0) p += 4;
    strncpy(body, p, MAX_BODY - 1);

    time_t now = time(NULL);
    const char *nick = g_clients[client_idx].nick;

    save_article(nick, subject, body, now);

    /* Difundir a todos los demás clientes */
    char bcast[MAX_MSG];
    snprintf(bcast, sizeof(bcast),
             "FROM:%s\nSUBJECT:%s\n---\n%s", nick, subject, body);
    broadcast(g_clients[client_idx].fd, MSG_BROADCAST, bcast);
    printf("[SERVER] Artículo de '%s' difundido: %s\n", nick, subject);
}

static int handle_client(int idx) {
    Message msg;
    if (recv_message(g_clients[idx].fd, &msg) < 0) {
        return -1;  /* Cliente cerró conexión o error */
    }

    /* Autenticación */
    if (!g_clients[idx].authenticated) {
        if (strcmp(msg.type, MSG_AUTH) != 0) {
            send_message(g_clients[idx].fd, MSG_ERROR, "AUTH_REQUIRED");
            return 0;
        }
        char nick[MAX_NICK];
        strncpy(nick, msg.payload, MAX_NICK - 1);
        nick[MAX_NICK - 1] = '\0';
        /* Eliminar espacios/newlines */
        for (int i = 0; nick[i]; i++)
            if (nick[i] == '\n' || nick[i] == '\r') { nick[i] = '\0'; break; }

        if (nick[0] == '\0') {
            send_message(g_clients[idx].fd, MSG_AUTH_ERR, "EMPTY_NICK");
            return 0;
        }
        if (nick_exists(nick)) {
            send_message(g_clients[idx].fd, MSG_AUTH_ERR, "DUPLICATE_NICK");
            printf("[SERVER] Nick duplicado rechazado: '%s'\n", nick);
            return 0;
        }
        strncpy(g_clients[idx].nick, nick, MAX_NICK - 1);
        g_clients[idx].authenticated = 1;
        send_message(g_clients[idx].fd, MSG_AUTH_OK, nick);
        printf("[SERVER] Cliente autenticado: '%s'\n", nick);
        return 0;
    }

    /* Mensajes de cliente autenticado */
    if (strcmp(msg.type, MSG_GET_NEWS) == 0) {
        handle_get_news(idx, msg.payload);
    } else if (strcmp(msg.type, MSG_PUBLISH) == 0) {
        handle_publish(idx, msg.payload);
    } else if (strcmp(msg.type, MSG_QUIT) == 0) {
        return -1;  /* Cliente se despide */
    } else {
        send_message(g_clients[idx].fd, MSG_ERROR, "UNKNOWN_CMD");
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Uso
 * ---------------------------------------------------------------------- */

static void usage(const char *prog) {
    fprintf(stderr,
        "Uso: %s [-p puerto] [-d dias] [-a directorio_articulos]\n"
        "  -p puerto   Puerto TCP a escuchar (default: %d)\n"
        "  -d dias     Dias minimos de retención de artículos (default: %d)\n"
        "  -a dir      Directorio donde guardar artículos (default: ./articles)\n",
        prog, DEFAULT_PORT, DEFAULT_DAYS);
    exit(EXIT_FAILURE);
}

/* -------------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "p:d:a:")) != -1) {
        switch (opt) {
            case 'p': g_port = atoi(optarg); break;
            case 'd': g_days = atoi(optarg); break;
            case 'a': strncpy(g_articles_dir, optarg, sizeof(g_articles_dir)-1); break;
            default:  usage(argv[0]);
        }
    }

    /* Crear directorio de artículos si no existe */
    mkdir(g_articles_dir, 0755);

    /* Señales */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);   /* Ignorar SIGPIPE en escrituras a socket cerrado */

    /* Crear socket servidor */
    g_server_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    int reuse = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)g_port);

    if (bind(g_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(EXIT_FAILURE);
    }
    if (listen(g_server_fd, 8) < 0) {
        perror("listen"); exit(EXIT_FAILURE);
    }

    printf("[SERVER] serv7 arrancado en puerto %d (días=%d, dir=%s)\n",
           g_port, g_days, g_articles_dir);

    /* Bucle principal con select() */
    while (g_running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(g_server_fd, &readfds);
        int maxfd = g_server_fd;

        for (int i = 0; i < g_num_clients; i++) {
            FD_SET(g_clients[i].fd, &readfds);
            if (g_clients[i].fd > maxfd) maxfd = g_clients[i].fd;
        }

        struct timeval tv = {1, 0};  /* timeout 1s para comprobar g_running */
        int ret = select(maxfd + 1, &readfds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        /* Nueva conexión */
        if (FD_ISSET(g_server_fd, &readfds)) {
            struct sockaddr_in caddr;
            socklen_t clen = sizeof(caddr);
            int cfd = accept(g_server_fd, (struct sockaddr *)&caddr, &clen);
            if (cfd >= 0) client_add(cfd);
        }

        /* Mensajes de clientes existentes */
        for (int i = 0; i < g_num_clients; i++) {
            if (FD_ISSET(g_clients[i].fd, &readfds)) {
                if (handle_client(i) < 0) {
                    client_remove(i);
                    i--;  /* Ajustar índice tras eliminación */
                }
            }
        }
    }

    /* Apagado limpio: notificar a todos los clientes */
    printf("[SERVER] Apagando servidor...\n");
    for (int i = 0; i < g_num_clients; i++) {
        send_message(g_clients[i].fd, MSG_SHUTDOWN,
                     "El servidor se está deteniendo");
        close(g_clients[i].fd);
    }
    close(g_server_fd);
    printf("[SERVER] Servidor detenido.\n");
    return 0;
}
