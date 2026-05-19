/*
 * protocol.h - Protocolo de comunicación News7
 *
 * Define los tipos de mensajes y estructuras compartidas
 * entre cliente y servidor.
 *
 * PROTOCOLO DE DELIMITACIÓN:
 *   Cada mensaje tiene la forma:
 *     [TIPO|LONGITUD|PAYLOAD\n]
 *   donde TIPO es un código de 2 caracteres, LONGITUD es un entero
 *   de 5 dígitos (bytes del payload) y PAYLOAD es el contenido.
 *
 *   Para artículos, el payload tiene la forma:
 *     ASUNTO\nCUERPO
 *   donde ASUNTO es la primera línea y CUERPO el resto.
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/* Tamaños máximos */
#define MAX_NICK       32
#define MAX_SUBJECT    128
#define MAX_BODY       4096
#define MAX_MSG        (MAX_BODY + MAX_SUBJECT + 64)
#define MAX_CLIENTS    64

/* Puerto y host por defecto */
#define DEFAULT_PORT   7777
#define DEFAULT_HOST   "127.0.0.1"
#define DEFAULT_DAYS   4

/* Tipos de mensaje (2 chars + '\0') */
#define MSG_AUTH       "AU"   /* Cliente -> Servidor: autenticación (nick) */
#define MSG_AUTH_OK    "AK"   /* Servidor -> Cliente: auth aceptada */
#define MSG_AUTH_ERR   "AE"   /* Servidor -> Cliente: nick duplicado */
#define MSG_GET_NEWS   "GN"   /* Cliente -> Servidor: pedir noticias de N días */
#define MSG_NEWS_ITEM  "NI"   /* Servidor -> Cliente: un artículo */
#define MSG_NEWS_END   "NE"   /* Servidor -> Cliente: fin de artículos */
#define MSG_PUBLISH    "PB"   /* Cliente -> Servidor: publicar artículo */
#define MSG_BROADCAST  "BC"   /* Servidor -> Cliente: artículo difundido */
#define MSG_QUIT       "QT"   /* Cliente -> Servidor: desconexión */
#define MSG_SHUTDOWN   "SD"   /* Servidor -> Clientes: servidor se detiene */
#define MSG_ERROR      "ER"   /* Error genérico */

/* Estructura de un artículo */
typedef struct {
    char nick[MAX_NICK];
    char subject[MAX_SUBJECT];
    char body[MAX_BODY];
    time_t timestamp;
} Article;

/* Estructura de un mensaje de protocolo */
typedef struct {
    char type[3];      /* Tipo de mensaje */
    uint32_t length;   /* Longitud del payload */
    char payload[MAX_MSG];
} Message;

/* Funciones de protocolo */
int  send_message(int fd, const char *type, const char *payload);
int  recv_message(int fd, Message *msg);

#endif /* PROTOCOL_H */
