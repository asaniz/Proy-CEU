# Documento de Arquitectura — News7

## 1. Visión general

News7 es una aplicación cliente-servidor de publicación de noticias que usa **sockets TCP del dominio `PF_INET`** (IPv4). El servidor es el único punto central; los clientes se conectan a él para consultar y publicar artículos.

```
  ┌────────┐          TCP          ┌─────────────────────┐
  │  cli7  │ ◄──────────────────► │                     │
  └────────┘                      │       serv7         │
  ┌────────┐          TCP          │                     │
  │  cli7  │ ◄──────────────────► │  (multiplexado con  │
  └────────┘                      │     select())        │
  ┌────────┐          TCP          │                     │
  │  cli7  │ ◄──────────────────► │                     │
  └────────┘                      └─────────────────────┘
                                          │
                                    articles/
                                  *.art (ficheros)
```

---

## 2. Arquitectura del servidor (`serv7`)

### 2.1 Modelo de concurrencia

El servidor atiende múltiples clientes **sin crear procesos ni hilos adicionales**, usando la llamada `select()` sobre todos los file descriptors activos (socket servidor + sockets de clientes).

```
main()
 │
 ├─ socket() / bind() / listen()
 │
 └─ bucle select()
       ├─ servidor_fd listo → accept() → client_add()
       └─ client_fd[i] listo → handle_client(i)
                                    ├─ handle_get_news()
                                    ├─ handle_publish() → save_article()
                                    │                  → broadcast()
                                    └─ MSG_QUIT → client_remove()
```

El timeout de `select()` es de 1 segundo, lo que permite comprobar la variable `g_running` tras recibir SIGINT/SIGTERM.

### 2.2 Gestión de clientes

Los clientes se almacenan en un array estático de tamaño `MAX_CLIENTS` (64). Cada entrada contiene:

```c
typedef struct {
    int  fd;               // File descriptor del socket
    char nick[MAX_NICK];   // Nombre del cliente
    int  authenticated;    // 0: pendiente, 1: autenticado
} Client;
```

Cuando un cliente se desconecta, su entrada se reemplaza con la del último elemento (compactación O(1)).

### 2.3 Almacenamiento de artículos

Cada artículo se persiste en un fichero independiente dentro del directorio de artículos (por defecto `./articles/`). El nombre del fichero es:

```
<timestamp_unix>_<nick>.art
```

Ejemplo: `1716000000_alice.art`

**Formato interno del fichero `.art`:**

```
NICK: alice
DATE: 1716000000
SUBJECT: Hola mundo
---
¡Este es el cuerpo del artículo!
Puede tener múltiples líneas.
```

La línea `---` actúa de separador entre cabecera y cuerpo.

Al solicitar noticias, el servidor recorre el directorio con `readdir()`, filtra los ficheros `.art`, lee su campo `DATE` y descarta los anteriores al umbral `now - days*86400`.

### 2.4 Señales

| Señal | Comportamiento |
|-------|---------------|
| `SIGINT` | Activa flag `g_running=0`. El bucle principal finaliza en la próxima iteración, notifica a clientes y cierra sockets. |
| `SIGTERM` | Ídem. |
| `SIGPIPE` | Ignorada (`SIG_IGN`). Las escrituras a sockets cerrados devuelven `EPIPE` en lugar de terminar el proceso. |

---

## 3. Arquitectura del cliente (`cli7`)

### 3.1 Modelo de E/S

El cliente también usa `select()` para monitorizar simultáneamente:

- **`STDIN_FILENO`**: entrada del usuario (menú interactivo).
- **socket del servidor**: mensajes entrantes en tiempo real (artículos difundidos, notificación de shutdown).

Esto permite mostrar artículos de otros usuarios mientras el menú está activo, sin bloquear la interfaz.

### 3.2 Flujo de conexión

```
main()
 │
 ├─ socket() / connect()
 ├─ send MSG_AUTH(nick)
 ├─ recv MSG_AUTH_OK / MSG_AUTH_ERR
 │
 └─ bucle select()
       ├─ stdin listo → procesar opción de menú
       │     ├─ "1" → cmd_get_news()  (send GN, recv NI*, recv NE)
       │     ├─ "2" → cmd_publish()   (send PB)
       │     └─ "3" → send QT, break
       └─ socket listo → recv mensaje
             ├─ MSG_BROADCAST → imprimir artículo
             └─ MSG_SHUTDOWN  → imprimir aviso, break
```

---

## 4. Protocolo de comunicación

### 4.1 Formato de mensaje

Cada mensaje ocupa exactamente **una línea** (terminada en `\n`):

```
TYPE LENGTH PAYLOAD\n
```

| Campo | Tamaño | Descripción |
|-------|--------|-------------|
| `TYPE` | 2 chars | Código del tipo de mensaje |
| ` ` | 1 char | Separador espacio |
| `LENGTH` | 10 dígitos | Longitud del payload en bytes (relleno con ceros) |
| ` ` | 1 char | Separador espacio |
| `PAYLOAD` | variable | Contenido del mensaje |
| `\n` | 1 char | Fin de línea |

Ejemplo de mensaje de autenticación:
```
AU 0000000005 alice\n
```

### 4.2 Tipos de mensaje

| Código | Nombre | Dirección | Descripción |
|--------|--------|-----------|-------------|
| `AU` | MSG_AUTH | C→S | Autenticación: payload = nick |
| `AK` | MSG_AUTH_OK | S→C | Auth aceptada: payload = nick confirmado |
| `AE` | MSG_AUTH_ERR | S→C | Auth rechazada: payload = motivo |
| `GN` | MSG_GET_NEWS | C→S | Pedir noticias: payload = número de días |
| `NI` | MSG_NEWS_ITEM | S→C | Un artículo: payload = `FROM:…\nSUBJECT:…\n---\n…` |
| `NE` | MSG_NEWS_END | S→C | Fin de artículos |
| `PB` | MSG_PUBLISH | C→S | Publicar artículo: payload = `SUBJECT:…\n---\n…` |
| `BC` | MSG_BROADCAST | S→C | Artículo difundido: mismo formato que NI |
| `QT` | MSG_QUIT | C→S | Desconexión voluntaria del cliente |
| `SD` | MSG_SHUTDOWN | S→C | Servidor deteniéndose |
| `ER` | MSG_ERROR | S→C | Error genérico |

### 4.3 Formato del payload de artículo

Tanto en `PB`, `NI` y `BC`, el payload de artículo tiene la siguiente estructura:

```
SUBJECT:<asunto>\n---\n<cuerpo>
```

En `NI` y `BC` (mensajes del servidor hacia el cliente) se añade además el campo `FROM`:

```
FROM:<nick>\nSUBJECT:<asunto>\n---\n<cuerpo>
```

La línea `---\n` actúa de separador inequívoco entre las cabeceras y el cuerpo, que puede contener múltiples líneas.

### 4.4 Diagrama de secuencia típica

```
Cliente                          Servidor
  │                                 │
  │── AU 0000000005 alice ─────────►│
  │                                 │ (comprueba nick único)
  │◄─ AK 0000000005 alice ──────────│
  │                                 │
  │── GN 0000000001 4 ─────────────►│
  │◄─ NI 0000000042 FROM:bob... ────│
  │◄─ NI 0000000038 FROM:carol... ──│
  │◄─ NE 0000000000  ───────────────│
  │                                 │
  │── PB 0000000051 SUBJECT:... ───►│
  │                                 │──── BC → todos los demás clientes
  │                                 │
  │── QT 0000000003 bye ───────────►│
  │                                 │ (cierra conexión)
```

---

## 5. Ficheros del proyecto

| Fichero | Descripción |
|---------|-------------|
| `include/protocol.h` | Constantes, tipos de mensaje, estructura `Message` |
| `src/protocol.c` | Implementación de `send_message()` y `recv_message()` |
| `src/serv7.c` | Proceso servidor completo |
| `src/cli7.c` | Proceso cliente completo |
| `Makefile` | Reglas de compilación |
| `docs/INSTALL.md` | Manual de instalación |
| `docs/USER.md` | Manual de usuario |
| `docs/ARCHITECTURE.md` | Este documento |

---

## 6. Decisiones de diseño

### ¿Por qué `select()` en lugar de `fork()` o `pthread`?

- Evita la complejidad de sincronización entre procesos/hilos.
- Suficiente para el volumen de clientes esperado (≤ 64).
- El código es más sencillo de depurar y auditar.

### ¿Por qué un fichero por artículo?

- Permite listar y filtrar por fecha usando solo el timestamp del nombre.
- Facilita la depuración (los artículos son legibles con `cat`).
- Evita depender de una base de datos externa.

### ¿Por qué el protocolo basado en líneas?

- Fácil de depurar con `nc` (netcat) o `telnet`.
- `recv_message()` sólo necesita leer hasta `\n`, sin gestionar buffers de longitud variable complejos.
- El campo `LENGTH` permite validar la integridad del payload.

### ¿Por qué `.` como fin de cuerpo en el cliente?

- Convención clásica de UNIX (heredada de `sendmail`, `ed`, etc.).
- Permite cuerpos multi-línea sin necesidad de un escaping especial.
- Documentada explícitamente en el manual de usuario.
