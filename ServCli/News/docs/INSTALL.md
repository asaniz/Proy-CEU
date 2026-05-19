# Manual de Instalación — News7

## Requisitos del sistema

| Requisito | Versión mínima |
|-----------|---------------|
| Sistema operativo | Linux / macOS / cualquier POSIX |
| Compilador C | GCC 7+ o Clang 6+ |
| GNU Make | 3.81+ |
| Librería C estándar | glibc 2.17+ (incluida en cualquier distro moderna) |

No se requieren bibliotecas externas. El sistema usa exclusivamente llamadas POSIX estándar.

---

## Obtención del código fuente

Descomprima el archivo entregado:

```
tar -xzf news7.tar.gz
cd news7/
```

La estructura resultante será:

```
news7/
├── Makefile
├── include/
│   └── protocol.h
├── src/
│   ├── protocol.c
│   ├── serv7.c
│   └── cli7.c
├── articles/          (creado automáticamente al arrancar el servidor)
└── docs/
    ├── INSTALL.md     (este fichero)
    ├── USER.md
    └── ARCHITECTURE.md
```

---

## Compilación

```bash
make
```

Esto genera dos ejecutables en el directorio raíz del proyecto:

- `serv7` — proceso servidor
- `cli7`  — proceso cliente

Para limpiar los objetos y binarios:

```bash
make clean
```

---

## Instalación (opcional)

Para instalar los binarios en el sistema (requiere permisos):

```bash
sudo cp serv7 cli7 /usr/local/bin/
```

O bien añadir el directorio del proyecto al PATH:

```bash
export PATH="$PATH:$(pwd)"
```

---

## Directorio de artículos

El servidor guarda los artículos en el directorio `./articles/` por defecto (relativo al directorio de trabajo donde se ejecuta `serv7`). Dicho directorio se crea automáticamente si no existe.

Para usar otra ruta, pase la opción `-a`:

```bash
./serv7 -a /var/news7/articles
```

Asegúrese de que el usuario que ejecuta el servidor tiene permisos de escritura sobre ese directorio.

---

## Verificación de la instalación

1. Arranque el servidor en una terminal:
   ```bash
   ./serv7 -p 7777 -d 4
   ```
   Debe aparecer:
   ```
   [SERVER] serv7 arrancado en puerto 7777 (días=4, dir=./articles)
   ```

2. En otra terminal, conecte un cliente:
   ```bash
   ./cli7 -n alice -p 7777
   ```
   Debe aparecer el menú interactivo de News7.

3. Compruebe que los procesos aparecen con sus nombres correctos:
   ```bash
   ps aux | grep -E 'serv7|cli7'
   ```

---

## Desinstalación

Basta con eliminar los binarios y el directorio del proyecto:

```bash
rm -f /usr/local/bin/serv7 /usr/local/bin/cli7
rm -rf /ruta/al/proyecto/news7/
```
