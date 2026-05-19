#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h> // Para manejar señales como SIGINT

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define NICK_SIZE 32

// Estructura para almacenar información de cada cliente
typedef struct {
    int sockfd;
    char nick[NICK_SIZE];
    struct sockaddr_in address;
    pthread_t thread_id;
    int active; // 1 si está activo, 0 si no
} client_t;

client_t clients[MAX_CLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
int server_sockfd;
volatile sig_atomic_t server_running = 1; // Bandera para controlar la ejecución del servidor

// Función para enviar un mensaje a un cliente específico
void send_message_to_client(int sockfd, const char *message) {
    send(sockfd, message, strlen(message), 0);
}

// Función para enviar un mensaje a todos los clientes conectados
void broadcast_message(const char *message, int sender_sockfd) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].sockfd != sender_sockfd) { // Enviar a todos excepto al remitente (opcional)
            send_message_to_client(clients[i].sockfd, message);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Función para verificar si un nick ya está en uso
int is_nick_unique(const char *nick) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && strcmp(clients[i].nick, nick) == 0) {
            pthread_mutex_unlock(&clients_mutex);
            return 0; // Nick no único
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return 1; // Nick único
}

// Función para manejar la comunicación con un cliente específico
void *handle_client(void *arg) {
    client_t *cli = (client_t *)arg;
    char buffer[BUFFER_SIZE];
    int read_size;
    char formatted_message[BUFFER_SIZE + NICK_SIZE + 5]; // Para "[Nick] Mensaje"

    printf("Cliente conectado desde %s:%d (sockfd %d)\n",
           inet_ntoa(cli->address.sin_addr), ntohs(cli->address.sin_port), cli->sockfd);

    // 1. Recepción y validación del Nick
    read_size = recv(cli->sockfd, buffer, BUFFER_SIZE - 1, 0);
    if (read_size <= 0) {
        printf("Error o desconexión al recibir nick del cliente %d\n", cli->sockfd);
        goto disconnect_client;
    }
    buffer[read_size] = '\0';

    if (strncmp(buffer, "NICK ", 5) == 0) {
        char client_nick[NICK_SIZE];
        strncpy(client_nick, buffer + 5, NICK_SIZE - 1);
        client_nick[NICK_SIZE - 1] = '\0'; // Asegurar terminación nula

        // Eliminar salto de línea si existe
        char *newline = strchr(client_nick, '\n');
        if (newline) *newline = '\0';

        if (strlen(client_nick) == 0) {
            send_message_to_client(cli->sockfd, "ERROR: Nick no puede estar vacío.\n");
            goto disconnect_client;
        }

        if (!is_nick_unique(client_nick)) {
            send_message_to_client(cli->sockfd, "ERROR: Nick ya en uso. Desconectando.\n");
            printf("Cliente con nick '%s' rechazado por duplicado (sockfd %d).\n", client_nick, cli->sockfd);
            goto disconnect_client;
        }

        pthread_mutex_lock(&clients_mutex);
        strncpy(cli->nick, client_nick, NICK_SIZE - 1);
        cli->nick[NICK_SIZE - 1] = '\0';
        pthread_mutex_unlock(&clients_mutex);

        send_message_to_client(cli->sockfd, "OK: Nick aceptado. Bienvenido al chat!\n");
        printf("Cliente %s (%s:%d) se ha unido al chat.\n", cli->nick,
               inet_ntoa(cli->address.sin_addr), ntohs(cli->address.sin_port));

        snprintf(formatted_message, sizeof(formatted_message), "INFO: %s se ha unido al chat.\n", cli->nick);
        broadcast_message(formatted_message, cli->sockfd);

    } else {
        send_message_to_client(cli->sockfd, "ERROR: Formato de nick incorrecto. Desconectando.\n");
        goto disconnect_client;
    }

    // 2. Bucle principal de recepción de mensajes del cliente
    while ((read_size = recv(cli->sockfd, buffer, BUFFER_SIZE - 1, 0)) > 0 && server_running) {
        buffer[read_size] = '\0';

        // Eliminar salto de línea
        char *newline = strchr(buffer, '\n');
        if (newline) *newline = '\0';

        printf("Recibido de %s: %s\n", cli->nick, buffer);

        if (strcmp(buffer, "QUIT") == 0) {
            printf("%s ha solicitado desconectarse.\n", cli->nick);
            break; // Salir del bucle y desconectar
        } else if (strncmp(buffer, "MSG ", 4) == 0) {
            char *message_content = buffer + 4;
            if (strlen(message_content) > 0) {
                snprintf(formatted_message, sizeof(formatted_message), "[%s] %s\n", cli->nick, message_content);
                broadcast_message(formatted_message, 0); // Enviar a todos, incluyendo al remitente si se desea
            }
        } else {
            send_message_to_client(cli->sockfd, "ERROR: Comando desconocido o formato incorrecto.\n");
        }
    }

disconnect_client:
    // Limpiar recursos del cliente
    close(cli->sockfd);
    pthread_mutex_lock(&clients_mutex);
    cli->active = 0; // Marcar como inactivo
    snprintf(formatted_message, sizeof(formatted_message), "INFO: %s se ha desconectado.\n", cli->nick);
    printf("Cliente %s (%s:%d, sockfd %d) se ha desconectado.\n", cli->nick,
           inet_ntoa(cli->address.sin_addr), ntohs(cli->address.sin_port), cli->sockfd);
    pthread_mutex_unlock(&clients_mutex);

    if (server_running) { // Solo si el servidor sigue corriendo, notificar a los demás
        broadcast_message(formatted_message, 0);
    }
    pthread_exit(NULL);
}

// Handler para la señal SIGINT (Ctrl+C)
void sigint_handler(int signum) {
    printf("\nSeñal SIGINT recibida. Deteniendo el servidor...\n");
    server_running = 0; // Indicar que el servidor debe detenerse

    // Enviar mensaje de cierre a todos los clientes
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            send_message_to_client(clients[i].sockfd, "SERVER_DOWN: El servidor se está deteniendo. Desconectando...\n");
            // No cerramos el socket aquí, se encargará el hilo del cliente o el cierre global
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    // Cierre del socket del servidor para liberar el puerto
    // Esto provocará un error en accept() si está bloqueado, lo cual es lo deseable.
    close(server_sockfd);
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    int port;

    // Renombrar el proceso (específico de Linux/Unix, no estándar de C)
    // Se usa argv[0] para asegurar que "serv7" aparezca en 'ps'
    strncpy(argv[0], "serv7", strlen("serv7"));
    argv[0][strlen("serv7")] = '\0'; // Asegurar terminación nula

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    port = atoi(argv[1]);
    if (port < 1024 || port > 65535) {
        fprintf(stderr, "Puerto inválido. Debe ser entre 1024 y 65535.\n");
        exit(EXIT_FAILURE);
    }

    // Inicializar la estructura de clientes
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].sockfd = -1;
        clients[i].active = 0;
        memset(clients[i].nick, 0, sizeof(clients[i].nick));
    }

    // Configurar el manejador de señales para SIGINT
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    // 1. Crear el socket del servidor
    server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sockfd == -1) {
        perror("Error al crear el socket del servidor");
        exit(EXIT_FAILURE);
    }

    // Permitir reutilizar el puerto inmediatamente después de cerrar el servidor
    int enable = 1;
    if (setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        // No es fatal, pero puede causar problemas de "Address already in use"
    }

    // 2. Configurar la dirección del servidor
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // Escuchar en todas las interfaces
    server_addr.sin_port = htons(port);

    // 3. Vincular el socket a la dirección y puerto
    if (bind(server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error al vincular el socket");
        close(server_sockfd);
        exit(EXIT_FAILURE);
    }

    // 4. Poner el socket en modo de escucha
    if (listen(server_sockfd, 5) < 0) { // 5 es el tamaño de la cola de conexiones pendientes
        perror("Error al escuchar en el socket");
        close(server_sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Servidor de chat iniciado en el puerto %d. Esperando conexiones...\n", port);

    // 5. Bucle principal para aceptar conexiones de clientes
    while (server_running) {
        client_len = sizeof(client_addr);
        int client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_addr, &client_len);

        if (client_sockfd < 0) {
            if (server_running) { // Si el servidor no se está deteniendo intencionalmente
                perror("Error al aceptar conexión");
            }
            continue; // Intentar aceptar la próxima conexión
        }

        // Buscar un slot libre para el nuevo cliente
        int i;
        pthread_mutex_lock(&clients_mutex);
        for (i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active) {
                clients[i].sockfd = client_sockfd;
                clients[i].address = client_addr;
                clients[i].active = 1;
                memset(clients[i].nick, 0, sizeof(clients[i].nick)); // Limpiar nick
                break;
            }
        }
        pthread_mutex_unlock(&clients_mutex);

        if (i == MAX_CLIENTS) {
            // Demasiados clientes conectados
            printf("Demasiados clientes. Conexión rechazada para %s:%d\n",
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            send_message_to_client(client_sockfd, "ERROR: Demasiados clientes conectados. Inténtelo más tarde.\n");
            close(client_sockfd);
        } else {
            // Crear un nuevo hilo para manejar al cliente
            if (pthread_create(&clients[i].thread_id, NULL, handle_client, (void *)&clients[i]) != 0) {
                perror("Error al crear hilo para el cliente");
                close(client_sockfd);
                pthread_mutex_lock(&clients_mutex);
                clients[i].active = 0;
                pthread_mutex_unlock(&clients_mutex);
            }
            pthread_detach(clients[i].thread_id); // Desvincular el hilo
        }
    }

    printf("Esperando a que los hilos de los clientes terminen...\n");
    // No necesitamos pthread_join aquí si usamos pthread_detach y la lógica de server_running
    // Los hilos se encargarán de sus propios sockets.

    // El socket del servidor ya fue cerrado en el handler de SIGINT
    // Si el servidor termina por otra razón, asegurar el cierre aquí
    if (server_sockfd != -1) {
        close(server_sockfd);
    }

    pthread_mutex_destroy(&clients_mutex);
    printf("Servidor apagado.\n");
    return 0;
}