#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define DEFAULT_BLOCK 65536   /* bloco grande = menos chamadas de send() */

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* Envia todos os bytes, mesmo se send() enviar parcialmente. */
static int send_all(int fd, const char *data, size_t len) {
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, MSG_NOSIGNAL);

        if (n == -1) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1;

        sent += (size_t)n;
    }
    return 0;
}

/* O servidor nao envia nada de volta no teste entao a thread receptora é desnecessária. */

int main(int argc, char *argv[]) {
    if (argc < 4 || argc > 5) {
        fprintf(stderr, "Uso: %s <IP> <porta> <segundos> [bloco]\n", argv[0]);
        return EXIT_FAILURE;
    }

    double seconds = atof(argv[3]);
    int block = (argc == 5) ? atoi(argv[4]) : DEFAULT_BLOCK;

    if (seconds <= 0 || block <= 0) {
        fprintf(stderr, "Parametros invalidos.\n");
        return EXIT_FAILURE;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) { perror("socket"); return EXIT_FAILURE; }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[2]));

    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) != 1) {
        fprintf(stderr, "Endereco IPv4 invalido.\n");
        return EXIT_FAILURE;
    }
    if (connect(sockfd, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) == -1) {
        perror("connect");
        return EXIT_FAILURE;
    }

    /* Espera o "S" do servidor: ele só libera quando todos os clientes conectaram. */
    char go;
    if (recv(sockfd, &go, 1, 0) != 1) {
        fprintf(stderr, "Servidor nao liberou o inicio.\n");
        close(sockfd);
        return EXIT_FAILURE;
    }

    char *buf = malloc((size_t)block);
    if (buf == NULL) { perror("malloc"); return EXIT_FAILURE; }
    memset(buf, 'X', (size_t)block);       /* bloco preparado uma unica vez */

    uint64_t sent = 0;
    double start = now();

    while (now() - start < seconds) {
        if (send_all(sockfd, buf, (size_t)block) == -1)
            break;                          /* servidor encerrou a janela */
        sent += (uint64_t)block;
    }

    shutdown(sockfd, SHUT_WR);
    close(sockfd);
    free(buf);

    fprintf(stderr, "Cliente: %llu bytes enviados.\n", (unsigned long long)sent);
    return EXIT_SUCCESS;
}