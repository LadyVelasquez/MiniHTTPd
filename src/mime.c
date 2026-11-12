#include "mime.h"
#include <string.h>
#include <strings.h>

typedef struct {
    const char *extension;
    const char *tipo;
} entrada_mime_t;

static const entrada_mime_t TABLA_MIME[] = {
    {".html", "text/html"},
    {".css",  "text/css"},
    {".js",   "application/javascript"},
    {".png",  "image/png"},
    {".jpg",  "image/jpeg"}
};

const char *obtener_tipo_mime(const char *ruta_archivo) {
    if (!ruta_archivo) {
        return "application/octet-stream";
    }

    const char *punto = strrchr(ruta_archivo, '.');
    if (!punto) {
        return "application/octet-stream";
    }

    size_t total = sizeof(TABLA_MIME) / sizeof(TABLA_MIME[0]);
    for (size_t i = 0; i < total; i++) {
        if (strcasecmp(punto, TABLA_MIME[i].extension) == 0) {
            return TABLA_MIME[i].tipo;
        }
    }

    return "application/octet-stream";
}