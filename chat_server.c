#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Otimizacao 1: tamanho do bloco de recv(): 64 KB. */
#define DEFAULT_BLOCK 65536
#define MAX_CLIENTS 10

/* Otimizacao 2: um contador por cliente, sem mutex, evitando disputa entre threads. */
typedef struct {
    int fd;
    int valid;        
    uint64_t bytes;  
    char *buf;        
} Client;

static Client clients[MAX_CLIENTS];
static pthread_t threads[MAX_CLIENTS];
static pthread_barrier_t barrier;
static double measure_start, measure_end;
static size_t block = DEFAULT_BLOCK;   /* tamanho do recv() */

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void *handle_client(void *arg) {
    Client *client = arg;

    pthread_barrier_wait(&barrier);   /* todas as threads comecam juntas */

    for (;;) {
        ssize_t n = recv(client->fd, client->buf, block, 0);
        double t = now();

        if (n > 0) {
            if (t >= measure_start && t < measure_end)
                client->bytes += (uint64_t)n;
            if (t >= measure_end)
                break;
            continue;
        }

        if (n == -1 && errno == EINTR)
            continue;

        if (t < measure_end)        
            client->valid = 0; /* erro antes do fim da janela */
        break;
    }

    close(client->fd);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 5 && argc != 6) {
        fprintf(stderr,
                "Uso: %s <porta> <N clientes> <warmup s> <duracao s> [bloco]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    int n_clients = atoi(argv[2]);
    double warmup = atof(argv[3]);
    double duration = atof(argv[4]);
    long b = (argc == 6) ? atol(argv[5]) : DEFAULT_BLOCK;

    if (n_clients < 1 || n_clients > MAX_CLIENTS || duration <= 0 || b <= 0) {
        fprintf(stderr, "Parametros invalidos.\n");
        return EXIT_FAILURE;
    }
    block = (size_t)b;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) { perror("socket"); return EXIT_FAILURE; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind"); return EXIT_FAILURE;
    }
    if (listen(server_fd, n_clients) == -1) {
        perror("listen"); return EXIT_FAILURE;
    }

    fprintf(stderr, "Aguardando %d cliente(s) na porta %d (bloco recv: %zu bytes)...\n",
            n_clients, port, block);
    pthread_barrier_init(&barrier, NULL, n_clients + 1);   /* N threads + main */

    /* Sincronizacao: so comeca depois que os N clientes conectarem. */
    for (int i = 0; i < n_clients; i++) {
        clients[i].fd = accept(server_fd, NULL, NULL);
        if (clients[i].fd == -1) { perror("accept"); return EXIT_FAILURE; }
        clients[i].bytes = 0;
        clients[i].valid = 1;
        clients[i].buf = malloc(block);
        if (clients[i].buf == NULL) { perror("malloc"); return EXIT_FAILURE; }
        if (pthread_create(&threads[i], NULL, handle_client, &clients[i]) != 0) {
            fprintf(stderr, "Erro ao criar thread.\n");
            return EXIT_FAILURE;
        }
    }
    close(server_fd);

    /* Janela de medicao unica, definida pelo servidor. */
    measure_start = now() + warmup;
    measure_end = measure_start + duration;

    for (int i = 0; i < n_clients; i++)          /* envia 1 byte para liberar os clientes */
        send(clients[i].fd, "S", 1, MSG_NOSIGNAL);

    pthread_barrier_wait(&barrier);              /* libera as threads receptoras */

    for (int i = 0; i < n_clients; i++)
        pthread_join(threads[i], NULL);

    uint64_t total = 0;
    int all_valid = 1;

    /* Soma dos bytes recebidos por todos os clientes. */
    for (int i = 0; i < n_clients; i++) {
        free(clients[i].buf);
        total += clients[i].bytes;
        if (!clients[i].valid) all_valid = 0;
        fprintf(stderr, "cliente %d: %.2f Mbps%s\n", i + 1,
                clients[i].bytes * 8.0 / duration / 1e6,
                clients[i].valid ? "" : "  (INVALIDO: caiu antes do fim)");
    }

    double mbps = (double)total * 8.0 / duration / 1e6; /* conversão para Mbps */
    printf("%d,%.2f\n", n_clients, mbps);         

    pthread_barrier_destroy(&barrier);
    return all_valid ? EXIT_SUCCESS : 2;
}