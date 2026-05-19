# Manual de Usuario — News7

## Descripción general

News7 es un sistema de publicación de noticias cliente-servidor. Los clientes se conectan a un servidor central, se identifican con un nombre único (nick), y pueden:

- **Consultar** los artículos publicados en los últimos N días.
- **Publicar** nuevos artículos que el servidor difunde en tiempo real a todos los clientes conectados.

---

## Arrancar el servidor (`serv7`)

```
serv7 [-p puerto] [-d días] [-a directorio_articulos]
```

| Opción | Descripción | Valor por defecto |
|--------|-------------|-------------------|
| `-p puerto` | Puerto TCP en el que escucha | 7777 |
| `-d días`   | Días mínimos de retención de artículos | 4 |
| `-a dir`    | Directorio donde se almacenan los artículos | `./articles` |

**Ejemplo:**
```bash
./serv7 -p 7777 -d 7 -a /var/news7
```

El servidor permanece en ejecución indefinidamente. Para detenerlo de forma limpia, envíe la señal `SIGINT` (Ctrl+C) o `SIGTERM`:

```bash
kill -TERM $(pgrep serv7)
```

Al detenerse, el servidor notifica a todos los clientes conectados para que se desconecten.

---

## Usar el cliente (`cli7`)

```
cli7 -n nick [-h host] [-p puerto]
```

| Opción | Descripción | Valor por defecto |
|--------|-------------|-------------------|
| `-n nick`  | Nombre de usuario (obligatorio, único) | — |
| `-h host`  | Dirección IP o nombre del servidor | `127.0.0.1` |
| `-p puerto`| Puerto del servidor | 7777 |

**Ejemplo:**
```bash
./cli7 -n alice -h 192.168.1.10 -p 7777
```

### Menú interactivo

Una vez conectado y autenticado, aparece el menú:

```
══════════ NEWS7 ══════════
  1. Ver noticias
  2. Publicar artículo
  3. Salir
Opción:
```

---

### Opción 1: Ver noticias

El cliente solicita los artículos publicados en un período de tiempo:

```
¿Noticias de cuántos días atrás? [4]:
```

Pulse Enter para aceptar el valor por defecto, o escriba un número de días. El servidor devuelve todos los artículos almacenados en ese rango, mostrados así:

```
─────────────────────────────────────────────────────────
De:      bob
Asunto:  Primer artículo de prueba
---
Este es el cuerpo del artículo.
Puede tener varias líneas.
─────────────────────────────────────────────────────────
```

---

### Opción 2: Publicar artículo

El cliente pide el asunto y el cuerpo:

```
Asunto: Título del artículo
Texto del artículo (termina con una línea que contenga solo '.'):
Primera línea del cuerpo.
Segunda línea.
.
[OK] Artículo publicado: 'Título del artículo'
```

**Delimitación del artículo:**
- El **asunto** es la primera línea introducida tras el prompt `Asunto:`.
- El **cuerpo** es el texto libre que sigue hasta que el usuario escribe una línea que contenga únicamente el punto `.`.
- Esta convención es similar a la usada por `sendmail` y otros sistemas de texto clásicos de UNIX.

El servidor difunde automáticamente el artículo a todos los clientes conectados en ese momento, mostrando:

```
*** ARTÍCULO EN DIRECTO ***
De:      alice
Asunto:  Título del artículo
---
Primera línea del cuerpo.
Segunda línea.
```

---

### Opción 3: Salir

Envía un mensaje de desconexión al servidor y cierra el cliente limpiamente. También puede pulsar Ctrl+C.

---

## Nicks duplicados

Si intenta conectarse con un nick que ya está en uso, el servidor rechazará la conexión con el mensaje:

```
[!] Error de autenticación: DUPLICATE_NICK
```

Use un nick diferente.

---

## Recepción de artículos en tiempo real

Mientras el menú está activo, el cliente puede recibir artículos publicados por otros usuarios en cualquier momento. El artículo aparecerá encima del menú, señalizado como **ARTÍCULO EN DIRECTO**. El menú se redibuja a continuación para que pueda seguir usando la aplicación con normalidad.

---

## Ejemplo de sesión completa

**Terminal 1 (servidor):**
```
$ ./serv7 -p 7777 -d 4
[SERVER] serv7 arrancado en puerto 7777 (días=4, dir=./articles)
[SERVER] Nueva conexión fd=4 (total=1)
[SERVER] Cliente autenticado: 'alice'
[SERVER] Artículo guardado: ./articles/1716000000_alice.art
[SERVER] Artículo de 'alice' difundido: Hola mundo
```

**Terminal 2 (cliente alice):**
```
$ ./cli7 -n alice -p 7777
[cli7] Conectado a 127.0.0.1:7777
[cli7] Autenticado como 'alice'

══════════ NEWS7 ══════════
  1. Ver noticias
  2. Publicar artículo
  3. Salir
Opción: 2
Asunto: Hola mundo
Texto del artículo (termina con una línea que contenga solo '.'):
¡Este es mi primer artículo en News7!
.
[OK] Artículo publicado: 'Hola mundo'
```

**Terminal 3 (cliente bob, conectado simultáneamente):**
```
*** ARTÍCULO EN DIRECTO ***
De:      alice
Asunto:  Hola mundo
---
¡Este es mi primer artículo en News7!
```
