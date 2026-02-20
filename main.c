#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

#define COR_VERMELHO "\033[1;31m"
#define COR_VERDE    "\033[1;32m"
#define COR_RESET    "\033[0m"

static volatile sig_atomic_t running = 1;
static void handle_sigint(int sig) { 
    (void)sig; running = 0; }

typedef enum {
    MODO_DEADLOCK,
    MODO_CORRECAO
} ModoExecucao;

typedef struct {
    int id;
    pthread_mutex_t chaveMoto;
    pthread_mutex_t lanchePronto;
} Restaurante;

typedef struct {
    Restaurante* restaurantes;
    ModoExecucao modo;
    int id;
    int numRestaurantes;
} DadosEntregador;

typedef struct {
    int numRestaurantes;
    int numEntregadores;
    ModoExecucao modo;
    Restaurante* restaurantes;
    pthread_t* threadsEntregadores;
    DadosEntregador* dadosEntregadores;
    pthread_mutex_t mutexAtivos;
    pthread_cond_t condAtiva;
    int total;
} Simulacao;

static Simulacao* gsim = NULL;

void config(Simulacao* sim);
void mutexes(Simulacao* sim);
void threadsE(Simulacao* sim);
void* veterano(void* arg);
void* novato(void* arg);
void stopThreads(Simulacao* sim);
void cleanup(Simulacao* sim);

static inline void sleep_ms(int ms) { 
    usleep(ms * 1000); 
}

int main() {
    srand(time(NULL));
    signal(SIGINT, handle_sigint);
    Simulacao sim;

    config(&sim);

    mutexes(&sim);
    gsim = &sim;

    threadsE(&sim);
    while (running) { sleep_ms(1000); }
    if (sim.modo == MODO_DEADLOCK) {
        stopThreads(&sim);
        cleanup(&sim);
    } else {
        pthread_mutex_lock(&sim.mutexAtivos);
        while (sim.total > 0) {
            pthread_cond_wait(&sim.condAtiva, &sim.mutexAtivos);
        }
        pthread_mutex_unlock(&sim.mutexAtivos);
        cleanup(&sim);
    }

    return 0;
}

void config(Simulacao* sim) {
    int opcao;
    printf("----- LARANJAL FOODS -----\n");
    printf("Escolha o modo de execução:\n1- Modo deadlock\n2- Modo correção\nOpção: ");

    if (scanf("%d", &opcao) != 1) {
        opcao = 0; 
    }

    switch (opcao) {
        case 1:
            sim->modo = MODO_DEADLOCK;
            break;
        case 2:
            sim->modo = MODO_CORRECAO;
            break;
        default:
            exit(0);
    }

    printf("\nConfigurações:\n");
    printf("Número de restaurantes: ");
    if (scanf("%d", &sim->numRestaurantes) != 1 || sim->numRestaurantes < 1) {
        exit(0);
    }

    printf("Número de entregadores: ");
    if (scanf("%d", &sim->numEntregadores) != 1 || sim->numEntregadores < 1) {
        exit(0);
    }
}

void mutexes(Simulacao* sim) {
    sim->restaurantes = malloc(sizeof(Restaurante) * sim->numRestaurantes);

    for (int i = 0; i < sim->numRestaurantes; i++) {
        sim->restaurantes[i].id = i + 1;
        pthread_mutex_init(&sim->restaurantes[i].chaveMoto, NULL);
        pthread_mutex_init(&sim->restaurantes[i].lanchePronto, NULL);
    }

    sim->threadsEntregadores = malloc(sizeof(pthread_t) * sim->numEntregadores);
    sim->dadosEntregadores = malloc(sizeof(DadosEntregador) * sim->numEntregadores);
    pthread_mutex_init(&sim->mutexAtivos, NULL);
    pthread_cond_init(&sim->condAtiva, NULL);
    sim->total = 0;

    for (int i = 0; i < sim->numEntregadores; i++) {
        sim->dadosEntregadores[i].id = i + 1;
        sim->dadosEntregadores[i].modo = sim->modo;
        sim->dadosEntregadores[i].restaurantes = sim->restaurantes;
        sim->dadosEntregadores[i].numRestaurantes = sim->numRestaurantes;
    }
}

void threadsE(Simulacao* sim) {
    void* (*rotinas[])(void*) = {veterano, novato};

    for (int i = 0; i < sim->numEntregadores; i++) {
        pthread_create(&sim->threadsEntregadores[i], NULL, rotinas[i % 2], &sim->dadosEntregadores[i]);
        pthread_detach(sim->threadsEntregadores[i]);
        pthread_mutex_lock(&sim->mutexAtivos);
        sim->total++;
        pthread_mutex_unlock(&sim->mutexAtivos);
    }
}

void* veterano(void* arg) {
    DadosEntregador* dados = (DadosEntregador*)arg;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    
    while (running) {
        int idx_restaurante = rand() % dados->numRestaurantes;
        Restaurante* res = &dados->restaurantes[idx_restaurante];
        bool sucesso = false;

        // pega a moto
        pthread_mutex_lock(&res->chaveMoto);
        printf("[Veterano %d]: Pegou a chave da moto do Restaurante %d\n", dados->id, res->id);
        sleep_ms(800);

        // tenta pegar o lanche
        printf("%s[Veterano %d]: Aguardando lanche do Restaurante %d...%s\n", COR_VERMELHO, dados->id, res->id, COR_RESET);
        
        if (dados->modo == MODO_CORRECAO) {
            if (pthread_mutex_trylock(&res->lanchePronto) == 0) {
                sucesso = true;
            } else {
                // solta a moto para evitar deadlock
                printf("%s[Sistema]: Veterano %d largou moto do Restaurante %d%s\n", COR_VERDE, dados->id, res->id, COR_RESET);
                pthread_mutex_unlock(&res->chaveMoto);
                sleep_ms(1000 + (rand() % 1200));
            }
        } else { 
            pthread_mutex_lock(&res->lanchePronto);
            sucesso = true;
        }

        if (sucesso) {
            printf("[Veterano %d]: Pegou o lanche do Restaurante %d\n", dados->id, res->id);
            printf("%s[Sistema]: Veterano %d entregou o pedido do Restaurante %d%s\n", COR_VERDE, dados->id, res->id, COR_RESET);
            
            // libera os recursos
            pthread_mutex_unlock(&res->lanchePronto);
            pthread_mutex_unlock(&res->chaveMoto);
            sleep_ms(1000 + (rand() % 1500));
        }
        
        sleep_ms(200);
    }
    
    pthread_mutex_lock(&gsim->mutexAtivos);
    gsim->total--;
    pthread_cond_signal(&gsim->condAtiva);
    pthread_mutex_unlock(&gsim->mutexAtivos);
    return NULL;
}

void* novato(void* arg) {
    DadosEntregador* dados = (DadosEntregador*)arg;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    while (running) {
        int idx_restaurante = rand() % dados->numRestaurantes;
        Restaurante* res = &dados->restaurantes[idx_restaurante];
        bool sucesso = false;

        // pega o lanche
        pthread_mutex_lock(&res->lanchePronto);
        printf("[Novato %d]: Pegou o lanche do Restaurante %d\n", dados->id, res->id);
        sleep_ms(800);

        // tenta pegar a moto
        printf("%s[Novato %d]: Aguardando moto do Restaurante %d...%s\n", COR_VERMELHO, dados->id, res->id, COR_RESET);

        if (dados->modo == MODO_CORRECAO) {
            if (pthread_mutex_trylock(&res->chaveMoto) == 0) {
                sucesso = true;
            } else {
                // no modo correção solta o lanche para evitar deadlock
                printf("%s[Sistema]: Novato %d largou lanche do Restaurante %d%s\n", COR_VERDE, dados->id, res->id, COR_RESET);
                pthread_mutex_unlock(&res->lanchePronto);
                sleep_ms(1000 + (rand() % 1200));
            }
        } else { 
            pthread_mutex_lock(&res->chaveMoto);
            sucesso = true;
        }

        if (sucesso) {
            printf("[Novato %d]: Pegou a chave da moto do Restaurante %d\n", dados->id, res->id);
            printf("%s[Sistema]: Novato %d entregou o pedido do Restaurante %d%s\n", COR_VERDE, dados->id, res->id, COR_RESET);
            
            // libera os recursos
            pthread_mutex_unlock(&res->chaveMoto);
            pthread_mutex_unlock(&res->lanchePronto);
            sleep_ms(1000 + (rand() % 1500));
        }
        
        sleep_ms(200);
    }

    pthread_mutex_lock(&gsim->mutexAtivos);
    gsim->total--;
    pthread_cond_signal(&gsim->condAtiva);
    pthread_mutex_unlock(&gsim->mutexAtivos);
    return NULL;
}

void stopThreads(Simulacao* sim) {
    for (int i = 0; i < sim->numEntregadores; i++) {
        pthread_cancel(sim->threadsEntregadores[i]);
    }
}
void cleanup(Simulacao* sim) {
    if (sim->restaurantes) {
        for (int i = 0; i < sim->numRestaurantes; i++) {
            pthread_mutex_destroy(&sim->restaurantes[i].chaveMoto);
            pthread_mutex_destroy(&sim->restaurantes[i].lanchePronto);
        }
        free(sim->restaurantes);
        sim->restaurantes = NULL;
    }
    if (sim->threadsEntregadores) {
        free(sim->threadsEntregadores);
        sim->threadsEntregadores = NULL;
    }
    if (sim->dadosEntregadores) {
        free(sim->dadosEntregadores);
        sim->dadosEntregadores = NULL;
    }
    pthread_mutex_destroy(&sim->mutexAtivos);
    pthread_cond_destroy(&sim->condAtiva);
}
