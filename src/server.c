#include "server.h"
#include "files.h"
#include "http.h"
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_EVENTOS 64

struct cliente {
    int fd;
    char in_buf[MAX_REQ];
    size_t in_len;
    char cab[512];
    size_t cab_len;
    size_t cab_pos;
    char *cuerpo;
    size_t cuerpo_len;
    size_t cuerpo_pos;
    int cerrar;
    cliente_t *ant;
    cliente_t *sig;
};

static int no_bloqueante(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int mod_epoll(int epoll_fd, cliente_t *cli, uint32_t eventos) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = eventos;
    ev.data.ptr = cli;
    return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, cli->fd, &ev);
}

static int crear_socket(int puerto) {
    char serv[16];
    snprintf(serv, sizeof(serv), "%d", puerto);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;

    struct addrinfo *res = NULL;
    int err = getaddrinfo(NULL, serv, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }

    int fd = -1;
    for (struct addrinfo *it = res; it != NULL; it = it->ai_next) {
        int s = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (s == -1)
            continue;

        int opt = 1;
        if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == 0 &&
            bind(s, it->ai_addr, it->ai_addrlen) == 0) {
            fd = s;
            break;
        }
        close(s);
    }

    freeaddrinfo(res);

    if (fd == -1) {
        perror("bind");
        return -1;
    }

    if (listen(fd, SOMAXCONN) == -1) {
        perror("listen");
        close(fd);
        return -1;
    }

    if (no_bloqueante(fd) == -1) {
        perror("fcntl");
        close(fd);
        return -1;
    }

    return fd;
}



static int agregar_cliente(servidor_t *srv, int fd) {
    cliente_t *cli = calloc(1, sizeof(*cli));
    if (!cli)
        return -1;

    cli->fd = fd;

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.ptr = cli;

    if (epoll_ctl(srv->epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        free(cli);
        return -1;
    }

    cli->sig = srv->lista;
    if (srv->lista)
        srv->lista->ant = cli;
    srv->lista = cli;
    return 0;
}

static void cerrar_cliente(servidor_t *srv, cliente_t *cli) {
    if (!cli)
        return;

    if (cli->ant)
        cli->ant->sig = cli->sig;
    else
        srv->lista = cli->sig;

    if (cli->sig)
        cli->sig->ant = cli->ant;

    epoll_ctl(srv->epoll_fd, EPOLL_CTL_DEL, cli->fd, NULL);
    close(cli->fd);
    free(cli->cuerpo);
    free(cli);
}

static int preparar_envio(
    servidor_t *srv,
    cliente_t *cli,
    int cod,
    const char *mime,
    char *datos,
    size_t tam,
    int keep
) {
    int n = armar_cabecera(cli->cab, sizeof(cli->cab), cod, mime, tam, keep);
    if (n < 0) {
        free(datos);
        return -1;
    }

    cli->cab_len = (size_t)n;
    cli->cab_pos = 0;
    cli->cuerpo = datos;
    cli->cuerpo_len = tam;
    cli->cuerpo_pos = 0;
    cli->cerrar = !keep;

    return mod_epoll(srv->epoll_fd, cli, EPOLLOUT);
}

/* 1: respuesta lista (EPOLLOUT), 0: faltan datos, -1: error fatal */
static int procesar_buffer(servidor_t *srv, cliente_t *cli) {
    size_t fin = fin_cabeceras(cli->in_buf, cli->in_len);
    if (fin == 0) {
        if (cli->in_len >= sizeof(cli->in_buf)) {
            cli->in_len = 0;
            return preparar_envio(srv, cli, 400, "text/html", NULL, 0, 0) == 0 ? 1 : -1;
        }
        return 0;
    }

    peticion_t req;
    int err = leer_peticion(cli->in_buf, fin, &req);

    size_t resto = cli->in_len - fin;
    if (resto > 0)
        memmove(cli->in_buf, cli->in_buf + fin, resto);
    cli->in_len = resto;

    if (err != 0) {
        printf("[%d] Solicitud rechazada -> %d\n", cli->fd, err);
        return preparar_envio(srv, cli, err, "text/html", NULL, 0, 0) == 0 ? 1 : -1;
    }

    archivo_t arch;
    int ok = cargar_archivo(srv->raiz, req.uri, &arch);
    int res;

    if (ok == 0) {
        printf("[%d] GET %s (Host: %s) -> 200 (%s)\n",
               cli->fd, req.uri, req.host, arch.mime);
        res = preparar_envio(srv, cli, 200, arch.mime, arch.datos, arch.tam,
                             req.persistente);
    } else {
        int keep = (arch.estado == 400 || arch.estado == 500) ? 0 : req.persistente;
        printf("[%d] GET %s (Host: %s) -> %d\n",
               cli->fd, req.uri, req.host, arch.estado);
        liberar_archivo(&arch);
        res = preparar_envio(srv, cli, arch.estado, "text/html", NULL, 0, keep);
    }

    return (res == 0) ? 1 : -1;
}

static int leer_cliente(servidor_t *srv, cliente_t *cli) {
    if (cli->cab_len > 0)
        return 0;

    while (cli->in_len < sizeof(cli->in_buf)) {
        ssize_t n = recv(
            cli->fd,
            cli->in_buf + cli->in_len,
            sizeof(cli->in_buf) - cli->in_len,
            0
        );

        if (n > 0) {
            cli->in_len += (size_t)n;
            if (fin_cabeceras(cli->in_buf, cli->in_len) > 0)
                break;
        } else if (n == 0) {
            return -1;
        } else {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return -1;
        }
    }

    return (procesar_buffer(srv, cli) == -1) ? -1 : 0;
}

/* 1: tramo completo, 0: esperar EPOLLOUT, -1: error */
static int enviar_tramo(int fd, const char *buf, size_t len, size_t *pos) {
    while (*pos < len) {
        ssize_t n = send(fd, buf + *pos, len - *pos, MSG_NOSIGNAL);
        if (n > 0) {
            *pos += (size_t)n;
        } else if (n == 0) {
            return -1;
        } else {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
            return -1;
        }
    }
    return 1;
}

static int escribir_cliente(servidor_t *srv, cliente_t *cli) {
    if (cli->cab_len == 0)
        return -1;

    int r = enviar_tramo(cli->fd, cli->cab, cli->cab_len, &cli->cab_pos);
    if (r <= 0)
        return r;

    if (cli->cuerpo && cli->cuerpo_len > 0) {
        r = enviar_tramo(cli->fd, cli->cuerpo, cli->cuerpo_len, &cli->cuerpo_pos);
        if (r <= 0)
            return r;
    }

    free(cli->cuerpo);
    cli->cuerpo = NULL;
    cli->cuerpo_len = 0;
    cli->cuerpo_pos = 0;
    cli->cab_len = 0;
    cli->cab_pos = 0;

    if (cli->cerrar)
        return -1;

    int sig = procesar_buffer(srv, cli);
    if (sig == -1)
        return -1;
    if (sig == 1)
        return 0;

    return mod_epoll(srv->epoll_fd, cli, EPOLLIN);
}

static void aceptar_pendientes(servidor_t *srv) {
    for (;;) {
        int fd = accept(srv->sock_fd, NULL, NULL);
        if (fd == -1) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            perror("accept");
            break;
        }

        if (no_bloqueante(fd) == -1 || agregar_cliente(srv, fd) == -1)
            close(fd);
    }
}

int iniciar_servidor(servidor_t *srv, int puerto, const char *raiz) {
    if (!srv || !raiz)
        return -1;

    srv->sock_fd = -1;
    srv->epoll_fd = -1;
    srv->raiz = raiz;
    srv->lista = NULL;

    srv->sock_fd = crear_socket(puerto);
    if (srv->sock_fd == -1)
        return -1;

    srv->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (srv->epoll_fd == -1) {
        perror("epoll_create1");
        cerrar_servidor(srv);
        return -1;
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.ptr = NULL;

    if (epoll_ctl(srv->epoll_fd, EPOLL_CTL_ADD, srv->sock_fd, &ev) == -1) {
        perror("epoll_ctl");
        cerrar_servidor(srv);
        return -1;
    }

    return 0;
}

int correr_servidor(servidor_t *srv, volatile sig_atomic_t *activo) {
    struct epoll_event evs[MAX_EVENTOS];

    while (*activo) {
        int n = epoll_wait(srv->epoll_fd, evs, MAX_EVENTOS, -1);
        if (n == -1) {
            if (errno == EINTR)
                continue;
            perror("epoll_wait");
            return -1;
        }

        for (int i = 0; i < n; i++) {
            if (evs[i].data.ptr == NULL) {
                aceptar_pendientes(srv);
                continue;
            }

            cliente_t *cli = (cliente_t *)evs[i].data.ptr;

            if (evs[i].events & (EPOLLERR | EPOLLHUP)) {
                cerrar_cliente(srv, cli);
                continue;
            }

            if (evs[i].events & EPOLLIN) {
                if (leer_cliente(srv, cli) == -1)
                    cerrar_cliente(srv, cli);
            } else if (evs[i].events & EPOLLOUT) {
                if (escribir_cliente(srv, cli) == -1)
                    cerrar_cliente(srv, cli);
            }
        }
    }

    return 0;
}

void cerrar_servidor(servidor_t *srv) {
    if (!srv)
        return;

    while (srv->lista)
        cerrar_cliente(srv, srv->lista);

    if (srv->epoll_fd != -1) {
        close(srv->epoll_fd);
        srv->epoll_fd = -1;
    }
    if (srv->sock_fd != -1) {
        close(srv->sock_fd);
        srv->sock_fd = -1;
    }
}