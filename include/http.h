#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

#define MAX_REQ   8192
#define MAX_LINEA 2048
#define MAX_URI   1024
#define MAX_HDR   512
#define MAX_HDRS  64

typedef struct {
    char uri[MAX_URI];
    char host[MAX_HDR];
    char agente[MAX_HDR];
    int persistente;
} peticion_t;

size_t fin_cabeceras(const char *buf, size_t len);
int leer_peticion(const char *buf, size_t len, peticion_t *req);
int armar_cabecera(
    char *dst,
    size_t cap,
    int cod,
    const char *mime,
    size_t tam,
    int keep
);

#endif