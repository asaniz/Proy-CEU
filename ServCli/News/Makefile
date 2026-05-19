# Makefile - News7
# Sistema cliente-servidor de noticias con sockets TCP (PF_INET)
#
# Uso:
#   make          Compilar servidor y cliente
#   make clean    Eliminar binarios y objetos
#   make run-server  Lanzar el servidor con valores por defecto
#   make run-client  Lanzar cliente de ejemplo (nick=usuario1)

CC      = gcc
CFLAGS  = -Wall -Wextra -pedantic -std=c11 -D_GNU_SOURCE \
           -I./include -g
LDFLAGS =

SRCDIR  = src
OBJDIR  = obj

# Fuentes compartidas
COMMON_SRC = $(SRCDIR)/protocol.c
COMMON_OBJ = $(OBJDIR)/protocol.o

# Fuentes del servidor
SERV_SRC = $(SRCDIR)/serv7.c
SERV_OBJ = $(OBJDIR)/serv7.o
SERV_BIN = serv7

# Fuentes del cliente
CLI_SRC  = $(SRCDIR)/cli7.c
CLI_OBJ  = $(OBJDIR)/cli7.o
CLI_BIN  = cli7

.PHONY: all clean run-server run-client

all: $(OBJDIR) $(SERV_BIN) $(CLI_BIN)
	@echo "✓ Compilación completada: $(SERV_BIN) y $(CLI_BIN)"

$(OBJDIR):
	mkdir -p $(OBJDIR)

# Servidor
$(SERV_BIN): $(SERV_OBJ) $(COMMON_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(SERV_OBJ): $(SERV_SRC)
	$(CC) $(CFLAGS) -c -o $@ $<

# Cliente
$(CLI_BIN): $(CLI_OBJ) $(COMMON_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(CLI_OBJ): $(CLI_SRC)
	$(CC) $(CFLAGS) -c -o $@ $<

# Protocolo (compartido)
$(COMMON_OBJ): $(COMMON_SRC)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(OBJDIR) $(SERV_BIN) $(CLI_BIN)
	@echo "✓ Limpieza completada"

run-server:
	mkdir -p articles
	./$(SERV_BIN) -p 7777 -d 4 -a ./articles

run-client:
	./$(CLI_BIN) -h 127.0.0.1 -p 7777 -n usuario1
