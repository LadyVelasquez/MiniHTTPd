#ifndef FILES_H
#define FILES_H

#include <stddef.h>

typedef struct {
    int codigo_estado;
    char *datos;
    size_t tamano;
    const char *tipo_mime;
} recurso_archivo_t;

int cargar_recurso(const char *dir_raiz, const char *uri, recurso_archivo_t *recurso);
void liberar_recurso(recurso_archivo_t *recurso);

#endif