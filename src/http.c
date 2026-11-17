#include "http.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int es_tchar(unsigned char c) {
    if (isalnum(c))
        return 1;
    switch (c) {
        case '!': case '#': case '$': case '%': case '&':
        case '\'': case '*': case '+': case '-': case '.':
        case '^': case '_': case '`': case '|': case '~':
            return 1;
        default:
            return 0;
    }
}

static const char *texto_estado(int cod) {
    switch (cod) {
        case 200: return "200 OK";
        case 400: return "400 Bad Request";
        case 403: return "403 Forbidden";
        case 404: return "404 Not Found";
        case 405: return "405 Method Not Allowed";
        default:  return "500 Internal Server Error";
    }
}

size_t fin_cabeceras(const char *buf, size_t len) {
    if (!buf || len < 4)
        return 0;
    const char *p = memmem(buf, len, "\r\n\r\n", 4);
    return p ? (size_t)(p - buf) + 4 : 0;
}

static void revisar_conexion(const char *val, size_t len, int *keep) {
    const char *cur = val;
    const char *fin = val + len;

    while (cur < fin) {
        const char *coma = memchr(cur, ',', (size_t)(fin - cur));
        const char *ini = cur;
        const char *tope = coma ? coma : fin;

        while (ini < tope && (*ini == ' ' || *ini == '\t'))
            ini++;
        while (tope > ini && (tope[-1] == ' ' || tope[-1] == '\t'))
            tope--;

        if ((size_t)(tope - ini) == 5 && strncasecmp(ini, "close", 5) == 0)
            *keep = 0;

        if (!coma)
            break;
        cur = coma + 1;
    }
}

static int parsear_linea(const char *ini, size_t len, char *uri) {
    if (len == 0 || len > MAX_LINEA)
        return 400;

    const char *fin = ini + len;
    const char *sp1 = memchr(ini, ' ', len);
    if (!sp1 || sp1 == ini)
        return 400;

    const char *u_ini = sp1 + 1;
    const char *sp2 = memchr(u_ini, ' ', (size_t)(fin - u_ini));
    if (!sp2 || sp2 == u_ini)
        return 400;

    const char *ver = sp2 + 1;
    if ((size_t)(fin - ver) != 8 || memcmp(ver, "HTTP/1.1", 8) != 0)
        return 400;

    for (const char *p = ini; p < sp1; p++) {
        if (!es_tchar((unsigned char)*p))
            return 400;
    }

    size_t met_len = (size_t)(sp1 - ini);
    if (met_len != 3 || memcmp(ini, "GET", 3) != 0)
        return 405;

    size_t uri_len = (size_t)(sp2 - u_ini);
    if (uri_len == 0 || uri_len >= MAX_URI)
        return 400;

    for (size_t i = 0; i < uri_len; i++) {
        unsigned char c = (unsigned char)u_ini[i];
        if (c <= 0x20 || c == 0x7F)
            return 400;
    }

    memcpy(uri, u_ini, uri_len);
    uri[uri_len] = '\0';
    return 0;
}

int leer_peticion(const char *buf, size_t len, peticion_t *req) {
    if (!buf || !req || len == 0 || len > MAX_REQ)
        return 400;

    memset(req, 0, sizeof(*req));
    req->persistente = 1;

    const char *nl = memchr(buf, '\n', len);
    if (!nl || nl == buf || nl[-1] != '\r')
        return 400;

    int err = parsear_linea(buf, (size_t)((nl - 1) - buf), req->uri);
    if (err != 0)
        return err;

    const char *ptr = nl + 1;
    const char *fin = buf + len;
    int tiene_host = 0;
    int tiene_cl = 0;
    int cant = 0;

    while (ptr < fin) {
        nl = memchr(ptr, '\n', (size_t)(fin - ptr));
        if (!nl || nl == ptr || nl[-1] != '\r')
            return 400;

        const char *l_fin = nl - 1;
        if (l_fin == ptr)
            return tiene_host ? 0 : 400;

        size_t l_len = (size_t)(l_fin - ptr);
        if (++cant > MAX_HDRS || l_len > MAX_HDR)
            return 400;

        const char *sep = memchr(ptr, ':', l_len);
        if (!sep || sep == ptr)
            return 400;

        for (const char *p = ptr; p < sep; p++) {
            if (!es_tchar((unsigned char)*p))
                return 400;
        }

        const char *v_ini = sep + 1;
        const char *v_fin = l_fin;
        while (v_ini < v_fin && (*v_ini == ' ' || *v_ini == '\t'))
            v_ini++;
        while (v_fin > v_ini && (v_fin[-1] == ' ' || v_fin[-1] == '\t'))
            v_fin--;

        for (const char *p = v_ini; p < v_fin; p++) {
            unsigned char c = (unsigned char)*p;
            if ((c < 0x20 && c != '\t') || c == 0x7F)
                return 400;
        }

        size_t nom_len = (size_t)(sep - ptr);
        size_t val_len = (size_t)(v_fin - v_ini);

        if (nom_len == 4 && strncasecmp(ptr, "Host", 4) == 0) {
            if (tiene_host || val_len == 0 || val_len >= MAX_HDR)
                return 400;
            memcpy(req->host, v_ini, val_len);
            req->host[val_len] = '\0';
            tiene_host = 1;
        } else if (nom_len == 10 && strncasecmp(ptr, "User-Agent", 10) == 0) {
            if (val_len >= MAX_HDR)
                return 400;
            memcpy(req->agente, v_ini, val_len);
            req->agente[val_len] = '\0';
        } else if (nom_len == 10 && strncasecmp(ptr, "Connection", 10) == 0) {
            revisar_conexion(v_ini, val_len, &req->persistente);
        } else if (nom_len == 17 && strncasecmp(ptr, "Transfer-Encoding", 17) == 0) {
            return 400;
        } else if (nom_len == 14 && strncasecmp(ptr, "Content-Length", 14) == 0) {
            if (tiene_cl || val_len == 0)
                return 400;
            for (size_t i = 0; i < val_len; i++) {
                if (v_ini[i] != '0')
                    return 400;
            }
            tiene_cl = 1;
        }

        ptr = nl + 1;
    }

    return 400;
}

int armar_cabecera(
    char *dst,
    size_t cap,
    int cod,
    const char *mime,
    size_t tam,
    int keep
) {
    if (!dst || cap == 0)
        return -1;

    const char *est = texto_estado(cod);
    char html_err[160];
    const char *cuerpo = "";
    size_t b_len = tam;
    const char *tipo = mime ? mime : "text/html";

    if (cod != 200) {
        int e = snprintf(
            html_err, sizeof(html_err),
            "<!DOCTYPE html><html><body><h1>%s</h1></body></html>\n",
            est
        );
        if (e < 0 || (size_t)e >= sizeof(html_err))
            return -1;
        cuerpo = html_err;
        b_len = (size_t)e;
        tipo = "text/html";
    }

    int n = snprintf(
        dst, cap,
        "HTTP/1.1 %s\r\n"
        "%s"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: %s\r\n"
        "\r\n"
        "%s",
        est,
        (cod == 405) ? "Allow: GET\r\n" : "",
        tipo,
        b_len,
        keep ? "keep-alive" : "close",
        cuerpo
    );

    if (n < 0 || (size_t)n >= cap)
        return -1;
    return n;
}