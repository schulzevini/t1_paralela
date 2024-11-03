#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define NUM_THREADS 4      // Número de threads no pool
#define NUM_CONTAS 10      // Número de contas bancárias
#define MAX_REQUISICOES 50 // Tamanho máximo da fila de requisições

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

// Variáveis globais
Conta contas[NUM_CONTAS];
Requisicao fila_requisicoes[MAX_REQUISICOES];
int inicio_fila = 0, fim_fila = 0;
int contador_operacoes = 0; // Conta operações para inserir balanço a cada 10
pthread_mutex_t mutex_contas, mutex_fila;
pthread_cond_t cond_requisicao;

// Funções de operações
void deposito(int id, float valor, int op_id) {
    pthread_mutex_lock(&mutex_contas);
    contas[id].saldo += valor;
    printf("Operação %d: Deposito de %.2f na conta %d. Novo saldo: %.2f\n", op_id, valor, id, contas[id].saldo);
    pthread_mutex_unlock(&mutex_contas);
    sleep(1);
}

void transferencia(int origem, int destino, float valor, int op_id) {
    pthread_mutex_lock(&mutex_contas);
    if (contas[origem].saldo >= valor) {
        contas[origem].saldo -= valor;
        contas[destino].saldo += valor;
        printf("Operação %d: Transferencia de %.2f da conta %d para a conta %d\n", op_id, valor, origem, destino);
    } else {
        printf("Operação %d: Transferencia falhou: saldo insuficiente na conta %d\n", op_id, origem);
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

// Função para threads trabalhadoras processarem requisições
void *trabalhador(void *arg) {
    while (1) {
        Requisicao req;

        pthread_mutex_lock(&mutex_fila);
        while (inicio_fila == fim_fila) {  // Fila vazia
            pthread_cond_wait(&cond_requisicao, &mutex_fila);
        }

        // Retira requisição da fila
        req = fila_requisicoes[inicio_fila];
        inicio_fila = (inicio_fila + 1) % MAX_REQUISICOES;
        pthread_mutex_unlock(&mutex_fila);

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
void adicionar_requisicao(int operacao, int id_origem, int id_destino, float valor) {
    static int id_contador = 0; // Contador global para IDs únicos

    pthread_mutex_lock(&mutex_fila);

    // Adiciona nova requisição na fila com ID único
    fila_requisicoes[fim_fila].id = id_contador++;
    fila_requisicoes[fim_fila].operacao = operacao;
    fila_requisicoes[fim_fila].id_origem = id_origem;
    fila_requisicoes[fim_fila].id_destino = id_destino;
    fila_requisicoes[fim_fila].valor = valor;
    fim_fila = (fim_fila + 1) % MAX_REQUISICOES;

    contador_operacoes++;
    if (contador_operacoes % 10 == 0) {  // Insere balanço geral a cada 10 operações
        fila_requisicoes[fim_fila].id = id_contador++;
        fila_requisicoes[fim_fila].operacao = 3;  // Operação de balanço
        fim_fila = (fim_fila + 1) % MAX_REQUISICOES;
        printf("Operação de balanço adicionada automaticamente após 10 operações.\n");
    }

    pthread_cond_signal(&cond_requisicao);
    pthread_mutex_unlock(&mutex_fila);
}

// Função para gerar requisições aleatórias
void *cliente(void *arg) {
    int id = *(int *)arg;
    while (1) {
        int operacao = rand() % 2 + 1;  // 1 = deposito, 2 = transferencia
        int id_origem = rand() % NUM_CONTAS;
        int id_destino = rand() % NUM_CONTAS;
        float valor = (float)(rand() % 1000) / 10.0;

        if (operacao == 1) {
            adicionar_requisicao(operacao, id_origem, -1, valor);
        } else if (operacao == 2 && id_origem != id_destino) {
            adicionar_requisicao(operacao, id_origem, id_destino, valor);
        }
        sleep(1);  // Espera antes de gerar nova requisição
    }
    return NULL;
}

int main() {
    pthread_t threads[NUM_THREADS];
    pthread_t clientes[2];
    int cliente_ids[2] = {0, 1};

    // Inicializa mutexes e variáveis de condição
    pthread_mutex_init(&mutex_contas, NULL);
    pthread_mutex_init(&mutex_fila, NULL);
    pthread_cond_init(&cond_requisicao, NULL);

    // Inicializa contas
    for (int i = 0; i < NUM_CONTAS; i++) {
        contas[i].id = i;
        contas[i].saldo = 1000.0;  // Saldo inicial de 1000
    }

    // Cria threads trabalhadoras
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, trabalhador, NULL);
    }

    // Cria threads clientes
    for (int i = 0; i < 2; i++) {
        pthread_create(&clientes[i], NULL, cliente, &cliente_ids[i]);
    }

    // Junta threads (não encerra o programa)
    for (int i = 0; i < 2; i++) {
        pthread_join(clientes[i], NULL);
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    // Libera recursos
    pthread_mutex_destroy(&mutex_contas);
    pthread_mutex_destroy(&mutex_fila);
    pthread_cond_destroy(&cond_requisicao);

    return 0;
}
