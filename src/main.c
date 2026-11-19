#include "server.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PUERTO_BASE 8080

static volatile sig_atomic_t activo = 1;

static void detener(int sig) {
    (void)sig;
    activo = 0;
}

int main(int argc, char *argv[]) {
    int puerto = PUERTO_BASE;
    const char *raiz = "./www";

    if (argc >= 2) {
        puerto = atoi(argv[1]);
        if (puerto <= 0 || puerto > 65535) {
            fprintf(stderr, "Uso: %s [puerto] [dir_raiz]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }
    if (argc >= 3)
        raiz = argv[2];

    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = detener;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    servidor_t srv;
    if (iniciar_servidor(&srv, puerto, raiz) == -1)
        return EXIT_FAILURE;

    printf("MiniHTTPd escuchando en http://localhost:%d (raiz: %s)\n", puerto, raiz);
    int res = correr_servidor(&srv, &activo);
    cerrar_servidor(&srv);

    printf("\nServidor detenido.\n");
    return (res == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}