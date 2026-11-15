#include "files.h"
#include "mime.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int cargar_recurso(const char *dir_raiz, const char *uri, recurso_archivo_t *recurso) {
    if (!dir_raiz || !uri || !recurso) {
        return -1;
    }

    recurso->datos = NULL;
    recurso->tamano = 0;
    recurso->tipo_mime = "text/html";
    recurso->codigo_estado = 500;

    if (uri[0] != '/') {
        recurso->codigo_estado = 400;
        return -1;
    }

    const char *ruta_relativa = (strcmp(uri, "/") == 0) ? "/index.html" : uri;

    char raiz_real[PATH_MAX];
    if (!realpath(dir_raiz, raiz_real)) {
        recurso->codigo_estado = 500;
        return -1;
    }

    char ruta_candidata[PATH_MAX];
    int escritos = snprintf(ruta_candidata, sizeof(ruta_candidata), "%s%s", raiz_real, ruta_relativa);
    if (escritos < 0 || (size_t)escritos >= sizeof(ruta_candidata)) {
        recurso->codigo_estado = 400;
        return -1;
    }

    char ruta_real[PATH_MAX];
    if (!realpath(ruta_candidata, ruta_real)) {
        if (errno == ENOENT || errno == ENOTDIR) {
            recurso->codigo_estado = 404;
        } else if (errno == EACCES) {
            recurso->codigo_estado = 403;
        } else {
            recurso->codigo_estado = 500;
        }
        return -1;
    }

    size_t len_raiz = strlen(raiz_real);
    if (strncmp(ruta_real, raiz_real, len_raiz) != 0 ||
        (ruta_real[len_raiz] != '\0' && ruta_real[len_raiz] != '/')) {
        recurso->codigo_estado = 403;
        return -1;
    }

    struct stat info;
    if (stat(ruta_real, &info) < 0) {
        recurso->codigo_estado = (errno == EACCES) ? 403 : 500;
        return -1;
    }

    if (!S_ISREG(info.st_mode)) {
        recurso->codigo_estado = 403;
        return -1;
    }

    FILE *archivo = fopen(ruta_real, "rb");
    if (!archivo) {
        recurso->codigo_estado = (errno == EACCES) ? 403 : 500;
        return -1;
    }

    size_t tam = (size_t)info.st_size;
    char *buffer = malloc(tam > 0 ? tam : 1);
    if (!buffer) {
        fclose(archivo);
        recurso->codigo_estado = 500;
        return -1;
    }

    if (tam > 0) {
        size_t leidos = fread(buffer, 1, tam, archivo);
        if (leidos != tam) {
            free(buffer);
            fclose(archivo);
            recurso->codigo_estado = 500;
            return -1;
        }
    }

    fclose(archivo);
    recurso->datos = buffer;
    recurso->tamano = tam;
    recurso->tipo_mime = obtener_tipo_mime(ruta_real);
    recurso->codigo_estado = 200;
    return 0;
}

void liberar_recurso(recurso_archivo_t *recurso) {
    if (recurso && recurso->datos) {
        free(recurso->datos);
        recurso->datos = NULL;
        recurso->tamano = 0;
    }
}