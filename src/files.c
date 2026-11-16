#include "files.h"
#include "mime.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_ARCHIVO (16U * 1024U * 1024U)

static int hex_a_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int limpiar_uri(const char *uri, char *dst, size_t max) {
    if (!uri || uri[0] != '/')
        return 400;

    size_t pos = 0;
    for (size_t i = 0; uri[i] != '\0' && uri[i] != '?'; i++) {
        unsigned char c = (unsigned char)uri[i];

        if (c == '%') {
            int hi = uri[i + 1] ? hex_a_int(uri[i + 1]) : -1;
            int lo = (hi >= 0 && uri[i + 2]) ? hex_a_int(uri[i + 2]) : -1;
            if (hi < 0 || lo < 0)
                return 400;
            c = (unsigned char)((hi << 4) | lo);
            i += 2;
        }

        if (c < ' ' || c == 127)
            return 400;
        if (pos + 1 >= max)
            return 400;

        dst[pos++] = (char)c;
    }
    dst[pos] = '\0';

    if (strcmp(dst, "/") == 0) {
        if (max < sizeof("/index.html"))
            return 400;
        memcpy(dst, "/index.html", sizeof("/index.html"));
    }
    return 0;
}

static int tiene_salto(const char *ruta) {
    for (const char *p = ruta; *p; p++) {
        if (p[0] == '/' && p[1] == '.' && p[2] == '.' &&
            (p[3] == '/' || p[3] == '\0'))
            return 1;
    }
    return 0;
}

static int error_errno(int err) {
    if (err == ENOENT || err == ENOTDIR)
        return 404;
    if (err == EACCES || err == EPERM || err == ELOOP)
        return 403;
    return 500;
}

int cargar_archivo(const char *raiz, const char *uri, archivo_t *out) {
    if (!raiz || !uri || !out)
        return -1;

    out->datos = NULL;
    out->tam = 0;
    out->mime = "text/html";
    out->estado = 500;

    char rel[PATH_MAX];
    int err_uri = limpiar_uri(uri, rel, sizeof(rel));
    if (err_uri != 0) {
        out->estado = err_uri;
        return -1;
    }

    char raiz_abs[PATH_MAX];
    if (!realpath(raiz, raiz_abs)) {
        out->estado = 500;
        return -1;
    }

    char ruta_tmp[PATH_MAX];
    int n = snprintf(ruta_tmp, sizeof(ruta_tmp), "%s%s", raiz_abs, rel);
    if (n < 0 || (size_t)n >= sizeof(ruta_tmp)) {
        out->estado = 400;
        return -1;
    }

    char ruta_abs[PATH_MAX];
    if (!realpath(ruta_tmp, ruta_abs)) {
        out->estado = tiene_salto(rel) ? 403 : error_errno(errno);
        return -1;
    }

    size_t len_raiz = strlen(raiz_abs);
    if (strncmp(ruta_abs, raiz_abs, len_raiz) != 0 ||
        (ruta_abs[len_raiz] != '\0' && ruta_abs[len_raiz] != '/')) {
        out->estado = 403;
        return -1;
    }

    int fd = open(ruta_abs, O_RDONLY);
    if (fd == -1) {
        out->estado = error_errno(errno);
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        close(fd);
        out->estado = 500;
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        close(fd);
        out->estado = 403;
        return -1;
    }
    if (st.st_size < 0 || (uintmax_t)st.st_size > MAX_ARCHIVO) {
        close(fd);
        out->estado = 500;
        return -1;
    }

    size_t tam = (size_t)st.st_size;
    char *buf = malloc(tam > 0 ? tam : 1);
    if (!buf) {
        close(fd);
        out->estado = 500;
        return -1;
    }

    size_t leido = 0;
    while (leido < tam) {
        ssize_t r = read(fd, buf + leido, tam - leido);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            free(buf);
            close(fd);
            out->estado = 500;
            return -1;
        }
        if (r == 0)
            break;
        leido += (size_t)r;
    }

    close(fd);
    out->datos = buf;
    out->tam = leido;
    out->mime = buscar_mime(ruta_abs);
    out->estado = 200;
    return 0;
}

void liberar_archivo(archivo_t *arch) {
    if (arch && arch->datos) {
        free(arch->datos);
        arch->datos = NULL;
        arch->tam = 0;
    }
}