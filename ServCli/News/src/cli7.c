/*
 * cli7.c - Cliente de noticias News7
 *
 * Uso: cli7 [-h host] [-p puerto] -n nick
 *
 * El cliente se conecta al servidor, se autentica con un nick único,
 * y presenta un menú interactivo para:
 *   1. Pedir noticias de los últimos N días
 *   2. Publicar un nuevo artículo
 *   3. Salir
 *
 * Usa select() para atender simultáneamente la entrada del usuario
 * y los mensajes entrantes del servidor (artículos difundidos en tiempo real).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "../include/protocol.h"

/* -------------------------------------------------------------------------
 * Variables globales
 * ---------------------------------------------------------------------- */

static int  g_sockfd  = -1;
static int  g_running = 1;
static char g_nick[MAX_NICK] = {0};

/* -------------------------------------------------------------------------
 * Señales
 * ---------------------------------------------------------------------- */

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* -------------------------------------------------------------------------
 * Utilidades de visualización
 * ---------------------------------------------------------------------- */

static void print_separator(void) {
    printf("─────────────────────────────────────────────────────────\n");
}

static void print_article(const char *payload, int is_live) {
    /* payload: "FROM:<nick>\nSUBJECT:<asunto>\n---\n<cuerpo>" */
    char nick[MAX_NICK] = {0};
    char subject[MAX_SUBJECT] = {0};
    char body[MAX_BODY] = {0};

    const char *p = payload;
    if (strncmp(p, "FROM:", 5) == 0) {
        p += 5;
        const char *nl = strchr(p, '\n');
        if (nl) {
            size_t l = (size_t)(nl - p);
            if (l >= MAX_NICK) l = MAX_NICK - 1;
            strncpy(nick, p, l);
            p = nl + 1;
        }
    }
    if (strncmp(p, "SUBJECT:", 8) == 0) {
        p += 8;
        const char *nl = strchr(p, '\n');
        if (nl) {
            size_t l = (size_t)(nl - p);
            if (l >= MAX_SUBJECT) l = MAX_SUBJECT - 1;
            strncpy(subject, p, l);
            p = nl + 1;
        }
    }
    if (strncmp(p, "---\n", 4) == 0) p += 4;
    strncpy(body, p, MAX_BODY - 1);

    print_separator();
    if (is_live)
        printf("*** ARTÍCULO EN DIRECTO ***\n");
    printf("De:      %s\n", nick);
    printf("Asunto:  %s\n", subject);
    printf("---\n%s\n", body);
    print_separator();
}

/* -------------------------------------------------------------------------
 * Leer noticias interactivamente
 * ---------------------------------------------------------------------- */

static void cmd_get_news(void) {
    int days;
    printf("¿Noticias de cuántos días atrás? [%d]: ", DEFAULT_DAYS);
    fflush(stdout);

    char buf[32];
    if (!fgets(buf, sizeof(buf), stdin)) return;
    days = atoi(buf);
    if (days <= 0) days = DEFAULT_DAYS;

    char payload[16];
    snprintf(payload, sizeof(payload), "%d", days);
    send_message(g_sockfd, MSG_GET_NEWS, payload);

    printf("\n[Noticias de los últimos %d días]\n", days);

    /* Recibir artículos hasta MSG_NEWS_END */
    int count = 0;
    Message msg;
    while (1) {
        if (recv_message(g_sockfd, &msg) < 0) {
            printf("[!] Conexión perdida\n");
            g_running = 0;
            return;
        }
        if (strcmp(msg.type, MSG_NEWS_END) == 0) break;
        if (strcmp(msg.type, MSG_SHUTDOWN) == 0) {
            printf("[!] Servidor detenido: %s\n", msg.payload);
            g_running = 0;
            return;
        }
        if (strcmp(msg.type, MSG_NEWS_ITEM) == 0) {
            print_article(msg.payload, 0);
            count++;
        }
    }
    if (count == 0)
        printf("(No hay artículos en ese período)\n");
    else
        printf("[Total: %d artículo(s)]\n", count);
}

/* -------------------------------------------------------------------------
 * Publicar artículo
 * ---------------------------------------------------------------------- */

static void cmd_publish(void) {
    char subject[MAX_SUBJECT];
    char body[MAX_BODY];
    char line[512];

    printf("Asunto: ");
    fflush(stdout);
    if (!fgets(subject, sizeof(subject), stdin)) return;
    subject[strcspn(subject, "\n")] = '\0';

    if (subject[0] == '\0') {
        printf("[!] El asunto no puede estar vacío.\n");
        return;
    }

    printf("Texto del artículo (termina con una línea que contenga solo '.'):\n");
    body[0] = '\0';
    size_t blen = 0;

    while (blen < MAX_BODY - 1) {
        if (!fgets(line, sizeof(line), stdin)) break;
        if (strcmp(line, ".\n") == 0 || strcmp(line, ".") == 0) break;
        size_t ll = strlen(line);
        if (blen + ll >= MAX_BODY - 1) {
            printf("[!] Cuerpo demasiado largo, truncando.\n");
            break;
        }
        strcat(body, line);
        blen += ll;
    }

    char payload[MAX_MSG];
    snprintf(payload, sizeof(payload), "SUBJECT:%s\n---\n%s", subject, body);
    send_message(g_sockfd, MSG_PUBLISH, payload);
    printf("[OK] Artículo publicado: '%s'\n", subject);
}

/* -------------------------------------------------------------------------
 * Menú principal
 * ---------------------------------------------------------------------- */

static void print_menu(void) {
    printf("\n══════════ NEWS7 ══════════\n");
    printf("  1. Ver noticias\n");
    printf("  2. Publicar artículo\n");
    printf("  3. Salir\n");
    printf("Opción: ");
    fflush(stdout);
}

/* -------------------------------------------------------------------------
 * Uso
 * ---------------------------------------------------------------------- */

static void usage(const char *prog) {
    fprintf(stderr,
        "Uso: %s -n nick [-h host] [-p puerto]\n"
        "  -n nick    Nombre de usuario (obligatorio)\n"
        "  -h host    Host del servidor (default: %s)\n"
        "  -p puerto  Puerto del servidor (default: %d)\n",
        prog, DEFAULT_HOST, DEFAULT_PORT);
    exit(EXIT_FAILURE);
}

/* -------------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    char host[128] = DEFAULT_HOST;
    int  port      = DEFAULT_PORT;

    int opt;
    while ((opt = getopt(argc, argv, "h:p:n:")) != -1) {
        switch (opt) {
            case 'h': strncpy(host, optarg, sizeof(host) - 1); break;
            case 'p': port = atoi(optarg); break;
            case 'n': strncpy(g_nick, optarg, MAX_NICK - 1); break;
            default:  usage(argv[0]);
        }
    }

    if (g_nick[0] == '\0') {
        fprintf(stderr, "Error: debe especificar un nick con -n\n");
        usage(argv[0]);
    }

    /* Señales */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    /* Resolver host */
    struct hostent *he = gethostbyname(host);
    if (!he) {
        herror("gethostbyname");
        exit(EXIT_FAILURE);
    }

    /* Conectar */
    g_sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if (g_sockfd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port   = htons((uint16_t)port);
    memcpy(&saddr.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(g_sockfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        perror("connect");
        exit(EXIT_FAILURE);
    }
    printf("[cli7] Conectado a %s:%d\n", host, port);

    /* Autenticación */
    send_message(g_sockfd, MSG_AUTH, g_nick);

    Message msg;
    if (recv_message(g_sockfd, &msg) < 0) {
        fprintf(stderr, "Error al recibir respuesta de autenticación\n");
        close(g_sockfd);
        exit(EXIT_FAILURE);
    }
    if (strcmp(msg.type, MSG_AUTH_OK) == 0) {
        printf("[cli7] Autenticado como '%s'\n", g_nick);
    } else if (strcmp(msg.type, MSG_AUTH_ERR) == 0) {
        fprintf(stderr, "[!] Error de autenticación: %s\n", msg.payload);
        close(g_sockfd);
        exit(EXIT_FAILURE);
    } else {
        fprintf(stderr, "[!] Respuesta inesperada: %s\n", msg.type);
        close(g_sockfd);
        exit(EXIT_FAILURE);
    }

    /* Bucle principal con select() */
    while (g_running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(g_sockfd, &readfds);
        int maxfd = g_sockfd;

        /* Mostar menú antes de select */
        print_menu();

        int ret = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ret < 0) {
            if (errno == EINTR) {
                /* Si se pulsó Ctrl+C */
                if (!g_running) break;
                continue;
            }
            perror("select");
            break;
        }

        /* Mensaje del servidor (artículo en tiempo real o shutdown) */
        if (FD_ISSET(g_sockfd, &readfds)) {
            if (recv_message(g_sockfd, &msg) < 0) {
                printf("\n[!] Conexión con el servidor perdida\n");
                g_running = 0;
                break;
            }
            if (strcmp(msg.type, MSG_BROADCAST) == 0) {
                printf("\n");
                print_article(msg.payload, 1);
            } else if (strcmp(msg.type, MSG_SHUTDOWN) == 0) {
                printf("\n[!] El servidor se ha detenido: %s\n", msg.payload);
                g_running = 0;
                break;
            }
            continue;  /* Volver a mostrar menú */
        }

        /* Entrada del usuario */
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char input[8];
            if (!fgets(input, sizeof(input), stdin)) break;
            int choice = atoi(input);

            switch (choice) {
                case 1: cmd_get_news(); break;
                case 2: cmd_publish();  break;
                case 3: g_running = 0;  break;
                default:
                    printf("[!] Opción inválida\n");
            }
        }
    }

    /* Despedirse del servidor */
    if (g_sockfd >= 0) {
        send_message(g_sockfd, MSG_QUIT, "bye");
        close(g_sockfd);
    }
    printf("[cli7] Desconectado. Hasta luego.\n");
    return 0;
}
