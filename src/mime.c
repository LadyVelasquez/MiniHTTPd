#include "mime.h"
#include <string.h>
#include <strings.h>

typedef struct {
    const char *ext;
    const char *tipo;
} par_mime_t;

static const par_mime_t TABLA[] = {
    {".html", "text/html"},
    {".css",  "text/css"},
    {".js",   "application/javascript"},
    {".png",  "image/png"},
    {".jpg",  "image/jpeg"}
};

const char *buscar_mime(const char *ruta) {
    if (!ruta)
        return "application/octet-stream";

    const char *punto = strrchr(ruta, '.');
    const char *barra = strrchr(ruta, '/');

    if (!punto || punto == ruta || (barra && punto <= barra + 1))
        return "application/octet-stream";

    size_t n = sizeof(TABLA) / sizeof(TABLA[0]);
    for (size_t i = 0; i < n; i++) {
        if (strcasecmp(punto, TABLA[i].ext) == 0)
            return TABLA[i].tipo;
    }
    return "application/octet-stream";
}