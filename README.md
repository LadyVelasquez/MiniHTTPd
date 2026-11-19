# MiniHTTPd

Lady Velasquez

Servidor HTTP/1.1 de archivos estáticos en C sobre Linux. Implementa sockets no bloqueantes con `epoll`, parsing de solicitudes sin bibliotecas HTTP externas y soporte para conexiones persistentes.

## Uso

```bash
make
./minihttpd [puerto] [directorio]
```

Si se omiten argumentos, escucha en el puerto `8080` y sirve el directorio `./www`.

## Estructura

```text
minihttpd/
├── Makefile
├── README.md
├── include/
│   ├── files.h
│   ├── http.h
│   ├── mime.h
│   └── server.h
├── src/
│   ├── files.c
│   ├── http.c
│   ├── main.c
│   ├── mime.c
│   └── server.c
└── www/
    ├── index.html
    ├── style.css
    └── image.png
```

* `main.c`: lectura de argumentos, señales (`SIGINT`, `SIGTERM`, `SIGPIPE`) y arranque.
* `server.c`: socket pasivo con `getaddrinfo`, bucle `epoll`, conexiones `keep-alive`, reajuste de buffer con `memmove` y cierre ordenado de clientes.
* `http.c`: parsing HTTP/1.1 con `memchr`, validación de headers y armado de respuestas en buffer fijo.
* `files.c`: decodificación `%XX`, limpieza de parámetros tras `?`, validación con `realpath` y lectura con `open`, `fstat` y `read`.
* `mime.c`: resolución de `Content-Type` por extensión, ignorando puntos en nombres de directorios.

## Especificación

| Componente | Implementación |
| --- | --- |
| Protocolo | HTTP/1.1, método `GET`, `keep-alive` y pipelining |
| Headers | `Host`, `Connection`, `User-Agent`, `Content-Type`, `Content-Length` |
| Códigos | `200`, `400`, `403`, `404`, `405`, `500` |
| Tipos MIME | `.html` (`text/html`), `.css` (`text/css`), `.js` (`application/javascript`), `.png` (`image/png`), `.jpg` (`image/jpeg`) |
| Seguridad | `realpath` contra directory traversal, verificación de archivo regular y tope de 16 MB con `fstat`, sin `strcpy` ni `sprintf` |

## Pruebas

### Archivos estáticos, MIME y query strings

```bash

curl -i http://127.0.0.1:8080/

curl -sD - -o /dev/null http://127.0.0.1:8080/image.png

# Ignorar parámetros después de ? -> 200 OK, text/css
curl -sD - -o /dev/null "http://127.0.0.1:8080/style.css?v=123"
```

### Keep-alive, pipelining y lecturas parciales

```bash
# verificar "Re-using existing connection"
curl -v http://127.0.0.1:8080/index.html http://127.0.0.1:8080/style.css -o /dev/null

printf "GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\nGET /style.css HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n" | nc -w 2 127.0.0.1 8080

(printf "GET /index"; sleep 0.5; printf ".html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n") | nc -w 2 127.0.0.1 8080
```

### Concurrencia

```bash
seq 1 30 | xargs -P 15 -I{} curl -s -o /dev/null -w "%{http_code}\n" http://127.0.0.1:8080/image.png | sort | uniq -c
```

### Seguridad y directory traversal

```bash
# Directory traversal directo -> 403 Forbidden
curl -i --path-as-is http://127.0.0.1:8080/../../etc/passwd

# 403 Forbidden
curl -i --path-as-is http://127.0.0.1:8080/%2e%2e/%2e%2e/etc/passwd
```

### Control de errores

```bash
# Recurso inexistente - 404 Not Found
curl -i http://127.0.0.1:8080/no-existe.html

# Método no permitido - 405 Method Not Allowed
curl -i -X POST http://127.0.0.1:8080/index.html

# Sin header Host - 400 Bad Request
printf "GET / HTTP/1.1\r\n\r\n" | nc -w 1 127.0.0.1 8080

# Header Host duplicado - 400 Bad Request
printf "GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n" | nc -w 1 127.0.0.1 8080

# Solicitud sobre el límite del buffer - 400 Bad Request
printf "GET /%09000d HTTP/1.1\r\nHost: localhost\r\n\r\n" 0 | nc -w 1 127.0.0.1 8080
```

### Memoria

```bash

valgrind --leak-check=full --show-leak-kinds=all ./minihttpd
```