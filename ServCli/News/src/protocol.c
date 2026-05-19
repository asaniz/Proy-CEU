/*
 * protocol.c - Implementación del protocolo News7
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include "../include/protocol.h"

/*
 * write_all - Escribe exactamente 'n' bytes en fd.
 */
static int write_all(int fd, const char *buf, size_t n) {
    size_t written = 0;
    while (written < n) {
        ssize_t r = write(fd, buf + written, n - written);
        if (r <= 0) return -1;
        written += (size_t)r;
    }
    return 0;
}

/*
 * read_all - Lee exactamente 'n' bytes de fd.
 */
static int read_all(int fd, char *buf, size_t n) {
    size_t done = 0;
    while (done < n) {
        ssize_t r = read(fd, buf + done, n - done);
        if (r <= 0) return -1;
        done += (size_t)r;
    }
    return 0;
}

/*
 * send_message - Envía un mensaje al file descriptor fd.
 *
 * Formato en red (binario-seguro):
 *   CABECERA: "TYPE LLLLLLLLLL\n"   (14 bytes fijos: 2+1+10+1)
 *   PAYLOAD:  <length bytes>         (puede contener '\n' y cualquier byte)
 *
 * Ejemplo de cabecera para publicar: "PB 0000000051\n"
 * El payload se envía a continuación, sin terminador adicional.
 */
int send_message(int fd, const char *type, const char *payload) {
    uint32_t len = payload ? (uint32_t)strlen(payload) : 0;
    char header[16];
    int n = snprintf(header, sizeof(header), "%s %010u\n", type, len);
    if (n != 14) return -1;

    if (write_all(fd, header, 14) < 0) return -1;
    if (len > 0 && write_all(fd, payload, len) < 0) return -1;
    return 0;
}

/*
 * recv_message - Recibe un mensaje del file descriptor fd.
 *
 * Lee primero los 14 bytes de cabecera, extrae tipo y longitud,
 * luego lee exactamente 'length' bytes de payload.
 * Devuelve 0 en éxito, -1 en error o conexión cerrada.
 */
int recv_message(int fd, Message *msg) {
    char header[15];

    /* Leer cabecera de 14 bytes exactos */
    if (read_all(fd, header, 14) < 0) return -1;
    header[14] = '\0';

    /* Validar formato: "XX NNNNNNNNNN\n" */
    if (header[2] != ' ' || header[13] != '\n') return -1;

    /* Tipo */
    msg->type[0] = header[0];
    msg->type[1] = header[1];
    msg->type[2] = '\0';

    /* Longitud */
    char len_str[11];
    memcpy(len_str, header + 3, 10);
    len_str[10] = '\0';
    msg->length = (uint32_t)atol(len_str);

    /* Payload */
    if (msg->length == 0) {
        msg->payload[0] = '\0';
        return 0;
    }
    if (msg->length >= sizeof(msg->payload)) return -1;  /* Demasiado grande */
    if (read_all(fd, msg->payload, msg->length) < 0) return -1;
    msg->payload[msg->length] = '\0';

    return 0;
}
