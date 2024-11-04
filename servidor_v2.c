#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

#define NUM_THREADS 4        // Número de threads no pool
#define NUM_CONTAS 10        // Número de contas bancárias
#define MAX_REQUISICOES 50   // Tamanho máximo da fila de requisições
#define NUM_CLIENTES 2       // Número de threads clientes
#define OPERACOES_PARA_BALANCO 10 // Insere balanço a cada 10 operações

// Estrutura para armazenar uma conta bancária
typedef struct {
    int id;
    float saldo;
} Conta;

// Estrutura para armazenar uma requisição
typedef struct {
    int id;          // ID único da operação
    int operacao;    // 1 = deposito, 2 = transferencia, 3 = balanco
    int id_origem;
    int id_destino;
    float valor;
} Requisicao;

// Estrutura para a fila de requisições
typedef struct {
    Requisicao dados[MAX_REQUISICOES];
    int inicio;
    int fim;
    int tamanho;
    pthread_mutex_t mutex;
    pthread_cond_t cond_nao_vazia;
    pthread_cond_t cond_nao_cheia;
} FilaRequisicoes;

// Variáveis globais
Conta contas[NUM_CONTAS];
FilaRequisicoes fila_requisicoes;
int contador_operacoes = 0; // Conta operações para inserir balanço a cada 10
pthread_mutex_t mutex_contas;
pthread_mutex_t mutex_contador;
bool shutdown_flag = false;

// Funções de operações
void deposito(int id, float valor, int op_id) {
    pthread_mutex_lock(&mutex_contas);
    contas[id].saldo += valor;
    printf("Operação %d: Depósito de %.2f na conta %d. Novo saldo: %.2f\n", op_id, valor, id, contas[id].saldo);
    pthread_mutex_unlock(&mutex_contas);
    sleep(1);
}

void transferencia(int origem, int destino, float valor, int op_id) {
    pthread_mutex_lock(&mutex_contas);
    if (contas[origem].saldo >= valor) {
        contas[origem].saldo -= valor;
        contas[destino].saldo += valor;
        printf("Operação %d: Transferência de %.2f da conta %d para a conta %d\n", op_id, valor, origem, destino);
    } else {
        printf("Operação %d: Transferência falhou: saldo insuficiente na conta %d\n", op_id, origem);
    }
    pthread_mutex_unlock(&mutex_contas);
    sleep(1);
}

void balanco(int op_id) {
    pthread_mutex_lock(&mutex_contas);
    printf("Operação %d: Balanço geral:\n", op_id);
    for (int i = 0; i < NUM_CONTAS; i++) {
        printf("Conta %d: Saldo = %.2f\n", contas[i].id, contas[i].saldo);
    }
    pthread_mutex_unlock(&mutex_contas);
    sleep(1);
}

// Inicializa a fila de requisições
void inicializar_fila(FilaRequisicoes *fila) {
    fila->inicio = 0;
    fila->fim = 0;
    fila->tamanho = 0;
    pthread_mutex_init(&fila->mutex, NULL);
    pthread_cond_init(&fila->cond_nao_vazia, NULL);
    pthread_cond_init(&fila->cond_nao_cheia, NULL);
}

// Adiciona uma requisição na fila
bool enfileirar(FilaRequisicoes *fila, Requisicao req) {
    pthread_mutex_lock(&fila->mutex);
    while (fila->tamanho == MAX_REQUISICOES && !shutdown_flag) {
        pthread_cond_wait(&fila->cond_nao_cheia, &fila->mutex);
    }

    if (shutdown_flag) {
        pthread_mutex_unlock(&fila->mutex);
        return false;
    }

    fila->dados[fila->fim] = req;
    fila->fim = (fila->fim + 1) % MAX_REQUISICOES;
    fila->tamanho++;
    pthread_cond_signal(&fila->cond_nao_vazia);
    pthread_mutex_unlock(&fila->mutex);
    return true;
}

// Remove uma requisição da fila
bool desenfileirar(FilaRequisicoes *fila, Requisicao *req) {
    pthread_mutex_lock(&fila->mutex);
    while (fila->tamanho == 0 && !shutdown_flag) {
        pthread_cond_wait(&fila->cond_nao_vazia, &fila->mutex);
    }

    if (fila->tamanho == 0 && shutdown_flag) {
        pthread_mutex_unlock(&fila->mutex);
        return false;
    }

    *req = fila->dados[fila->inicio];
    fila->inicio = (fila->inicio + 1) % MAX_REQUISICOES;
    fila->tamanho--;
    pthread_cond_signal(&fila->cond_nao_cheia);
    pthread_mutex_unlock(&fila->mutex);
    return true;
}

// Função para threads trabalhadoras processarem requisições
void *trabalhador(void *arg) {
    while (1) {
        Requisicao req;
        bool sucesso = desenfileirar(&fila_requisicoes, &req);
        if (!sucesso) {
            break; // Sinal para encerrar a thread
        }

        // Processa a requisição
        if (req.operacao == 1) {
            deposito(req.id_origem, req.valor, req.id);
        } else if (req.operacao == 2) {
            transferencia(req.id_origem, req.id_destino, req.valor, req.id);
        } else if (req.operacao == 3) {
            balanco(req.id);
        }
    }
    return NULL;
}

// Função do servidor para adicionar uma nova requisição
bool adicionar_requisicao(int operacao, int id_origem, int id_destino, float valor) {
    static int id_contador = 0; // Contador global para IDs únicos
    Requisicao req;

    req.id = __sync_fetch_and_add(&id_contador, 1);
    req.operacao = operacao;
    req.id_origem = id_origem;
    req.id_destino = id_destino;
    req.valor = valor;

    bool enfileirou = enfileirar(&fila_requisicoes, req);
    if (!enfileirou) {
        return false;
    }

    // Atualiza o contador de operações
    pthread_mutex_lock(&mutex_contador);
    contador_operacoes++;
    int operacoes = contador_operacoes;
    pthread_mutex_unlock(&mutex_contador);

    if (operacoes % OPERACOES_PARA_BALANCO == 0) {  // Insere balanço geral a cada 10 operações
        Requisicao bal_req;
        bal_req.id = __sync_fetch_and_add(&id_contador, 1);
        bal_req.operacao = 3;  // Operação de balanço
        bal_req.id_origem = -1;
        bal_req.id_destino = -1;
        bal_req.valor = 0.0;

        enfileirar(&fila_requisicoes, bal_req);
        printf("Operação de balanço adicionada automaticamente após %d operações.\n", OPERACOES_PARA_BALANCO);
    }

    return true;
}

// Função para gerar requisições aleatórias
void *cliente(void *arg) {
    int id = *(int *)arg;
    while (!shutdown_flag) {
        int operacao = rand() % 2 + 1;  // 1 = deposito, 2 = transferencia
        int id_origem = rand() % NUM_CONTAS;
        int id_destino = rand() % NUM_CONTAS;
        float valor = (float)(rand() % 1000) / 10.0;

        bool adicionou = false;
        if (operacao == 1) {
            adicionou = adicionar_requisicao(operacao, id_origem, -1, valor);
        } else if (operacao == 2 && id_origem != id_destino) {
            adicionou = adicionar_requisicao(operacao, id_origem, id_destino, valor);
        }

        if (!adicionou) {
            break; // Sinal para encerrar a thread
        }

        sleep(1);  // Espera antes de gerar nova requisição
    }
    return NULL;
}

// Função para encerrar todas as threads
void encerrar_threads(pthread_t *clientes, pthread_t *trabalhadores, int num_clientes, int num_trabalhadores) {
    // Sinaliza o shutdown
    pthread_mutex_lock(&fila_requisicoes.mutex);
    shutdown_flag = true;
    pthread_cond_broadcast(&fila_requisicoes.cond_nao_vazia);
    pthread_cond_broadcast(&fila_requisicoes.cond_nao_cheia);
    pthread_mutex_unlock(&fila_requisicoes.mutex);

    // Aguarda as threads clientes
    for (int i = 0; i < num_clientes; i++) {
        pthread_join(clientes[i], NULL);
    }

    // Aguarda as threads trabalhadoras
    for (int i = 0; i < num_trabalhadores; i++) {
        pthread_join(trabalhadores[i], NULL);
    }
}

int main() {
    pthread_t threads[NUM_THREADS];
    pthread_t clientes_ids[NUM_CLIENTES];
    int cliente_ids[NUM_CLIENTES];
    srand(time(NULL));

    // Inicializa mutexes e variáveis de condição
    pthread_mutex_init(&mutex_contas, NULL);
    pthread_mutex_init(&mutex_contador, NULL);
    inicializar_fila(&fila_requisicoes);

    // Inicializa contas
    for (int i = 0; i < NUM_CONTAS; i++) {
        contas[i].id = i;
        contas[i].saldo = 1000.0;  // Saldo inicial de 1000
    }

    // Cria threads trabalhadoras
    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, trabalhador, NULL) != 0) {
            perror("Falha ao criar thread trabalhadora");
            exit(EXIT_FAILURE);
        }
    }

    // Cria threads clientes
    for (int i = 0; i < NUM_CLIENTES; i++) {
        cliente_ids[i] = i;
        if (pthread_create(&clientes_ids[i], NULL, cliente, &cliente_ids[i]) != 0) {
            perror("Falha ao criar thread cliente");
            exit(EXIT_FAILURE);
        }
    }

    // Executa por um período determinado ou até uma condição de encerramento
    // Aqui, vamos executar por 30 segundos como exemplo
    sleep(30);

    // Encerra as threads
    encerrar_threads(clientes_ids, threads, NUM_CLIENTES, NUM_THREADS);

    // Libera recursos
    pthread_mutex_destroy(&mutex_contas);
    pthread_mutex_destroy(&mutex_contador);
    pthread_mutex_destroy(&fila_requisicoes.mutex);
    pthread_cond_destroy(&fila_requisicoes.cond_nao_vazia);
    pthread_cond_destroy(&fila_requisicoes.cond_nao_cheia);

    printf("Sistema encerrado com sucesso.\n");
    return 0;
}
