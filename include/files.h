#ifndef FILES_H
#define FILES_H

#include <stddef.h>

typedef struct {
    int estado;
    char *datos;
    size_t tam;
    const char *mime;
} archivo_t;

int cargar_archivo(const char *raiz, const char *uri, archivo_t *out);
void liberar_archivo(archivo_t *arch);

#endif