#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h> // Para el hilo de recepción

#define BUFFER_SIZE 1024
#define NICK_SIZE 32

int client_sockfd;
volatile int client_running = 1; // Bandera para controlar la ejecución del cliente

// Hilo para recibir mensajes del servidor
void *receive_messages(void *arg) {
    char buffer[BUFFER_SIZE];
    int read_size;

    while (client_running) {
        read_size = recv(client_sockfd, buffer, BUFFER_SIZE - 1, 0);
        if (read_size <= 0) {
            if (client_running) { // Si no estamos cerrando el cliente intencionalmente
                printf("\nDesconexión del servidor o error de lectura.\n");
                client_running = 0;
            }
            break;
        }
        buffer[read_size] = '\0';

        // Manejar mensajes especiales del servidor
        if (strncmp(buffer, "SERVER_DOWN:", 12) == 0) {
            printf("%s\n", buffer);
            client_running = 0; // Detener el cliente
            break;
        }
        if (strncmp(buffer, "ERROR: Nick ya en uso.", 22) == 0) {
            printf("%s\n", buffer);
            client_running = 0; // Detener el cliente
            break;
        }
        if (strncmp(buffer, "ERROR:", 6) == 0 || strncmp(buffer, "OK:", 3) == 0 || strncmp(buffer, "INFO:", 5) == 0) {
            printf("%s", buffer); // Mensajes de control del servidor
        } else {
            printf("%s", buffer); // Mensaje de chat de otro cliente
        }
        fflush(stdout); // Asegurar que se muestre el mensaje inmediatamente
    }
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char nick[NICK_SIZE];
    pthread_t recv_thread;

    // Renombrar el proceso (específico de Linux/Unix, no estándar de C)
    strncpy(argv[0], "cli7", strlen("cli7"));
    argv[0][strlen("cli7")] = '\0';

    if (argc != 3) {
        fprintf(stderr, "Uso: %s <direccion_servidor> <puerto_servidor>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    int server_port = atoi(argv[2]);

    if (server_port < 1024 || server_port > 65535) {
        fprintf(stderr, "Puerto inválido. Debe ser entre 1024 y 65535.\n");
        exit(EXIT_FAILURE);
    }

    // 1. Crear el socket del cliente
    client_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sockfd == -1) {
        perror("Error al crear el socket del cliente");
        exit(EXIT_FAILURE);
    }

    // 2. Configurar la dirección del servidor
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Dirección IP inválida o no soportada");
        close(client_sockfd);
        exit(EXIT_FAILURE);
    }

    // 3. Conectarse al servidor
    if (connect(client_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error al conectar con el servidor");
        close(client_sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Conectado al servidor %s:%d\n", server_ip, server_port);

    // 4. Solicitar y enviar el nick al servidor
    printf("Por favor, introduce tu nick: ");
    if (fgets(nick, sizeof(nick), stdin) == NULL) {
        perror("Error al leer el nick");
        close(client_sockfd);
        exit(EXIT_FAILURE);
    }
    // Eliminar el salto de línea al final del nick
    nick[strcspn(nick, "\n")] = 0;

    if (strlen(nick) == 0) {
        fprintf(stderr, "El nick no puede estar vacío.\n");
        close(client_sockfd);
        exit(EXIT_FAILURE);
    }

    // Enviar el nick al servidor
    snprintf(buffer, BUFFER_SIZE, "NICK %s\n", nick);
    send(client_sockfd, buffer, strlen(buffer), 0);

    // Crear un hilo para recibir mensajes del servidor de forma asíncrona
    if (pthread_create(&recv_thread, NULL, receive_messages, NULL) != 0) {
        perror("Error al crear hilo de recepción");
        close(client_sockfd);
        exit(EXIT_FAILURE);
    }

    // 5. Bucle principal para enviar mensajes al servidor
    // Leer mensajes del usuario y enviarlos al servidor
    while (client_running) {
        printf("> "); // Prompt para el usuario
        fflush(stdout); // Asegurar que el prompt se muestre

        if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) {
            perror("Error al leer entrada del usuario");
            break;
        }

        // Eliminar el salto de línea final
        buffer[strcspn(buffer, "\n")] = 0;

        if (strlen(buffer) == 0) {
            continue; // No enviar mensajes vacíos
        }

        if (strcmp(buffer, "QUIT") == 0) {
            send(client_sockfd, "QUIT\n", strlen("QUIT\n"), 0);
            client_running = 0; // Indicar que debemos salir
            break;
        } else {
            // Enviar mensaje de chat
            char formatted_msg[BUFFER_SIZE + 4]; // Para "MSG " prefijo
            snprintf(formatted_msg, sizeof(formatted_msg), "MSG %s\n", buffer);
            send(client_sockfd, formatted_msg, strlen(formatted_msg), 0);
        }
    }

    // Esperar a que el hilo de recepción termine (si aún está activo)
    pthread_cancel(recv_thread); // Intentar cancelar el hilo
    pthread_join(recv_thread, NULL); // Esperar su finalización

    close(client_sockfd);
    printf("Desconectado del servidor.\n");

    return 0;
}
