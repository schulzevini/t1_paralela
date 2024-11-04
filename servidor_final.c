#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define NUM_THREADS 2
#define NUM_CONTAS 10
#define MAX_REQUISICOES 50
#define DURACAO_EXECUCAO 20

int encerrar = 0;

typedef struct {
    int id;
    float saldo;
} Conta;

typedef struct {
    int id;
    int operacao;
    int id_origem;
    int id_destino;
    float valor;
} Requisicao;

Conta contas[NUM_CONTAS];
Requisicao fila_requisicoes[MAX_REQUISICOES];
int inicio_fila = 0, fim_fila = 0;
int contador_operacoes = 0;
pthread_mutex_t mutex_contas, mutex_fila;
pthread_cond_t cond_requisicao;

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

void *trabalhador(void *arg) {
    while (1) {
        Requisicao req;

        pthread_mutex_lock(&mutex_fila);
        if (encerrar && inicio_fila == fim_fila) {
            pthread_mutex_unlock(&mutex_fila);
            break;
        }

        while (inicio_fila == fim_fila && !encerrar) {  
            pthread_cond_wait(&cond_requisicao, &mutex_fila);
        }
        
        if (encerrar && inicio_fila == fim_fila) { 
            pthread_mutex_unlock(&mutex_fila);
            break;
        }
        while (inicio_fila == fim_fila) {
            pthread_cond_wait(&cond_requisicao, &mutex_fila);
        }

        req = fila_requisicoes[inicio_fila];
        inicio_fila = (inicio_fila + 1) % MAX_REQUISICOES;
        pthread_mutex_unlock(&mutex_fila);

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

void adicionar_requisicao(int operacao, int id_origem, int id_destino, float valor) {
    static int id_contador = 0;

    pthread_mutex_lock(&mutex_fila);
    if (encerrar) {
        pthread_mutex_unlock(&mutex_fila);
        return;
    }

    fila_requisicoes[fim_fila].id = id_contador++;
    fila_requisicoes[fim_fila].operacao = operacao;
    fila_requisicoes[fim_fila].id_origem = id_origem;
    fila_requisicoes[fim_fila].id_destino = id_destino;
    fila_requisicoes[fim_fila].valor = valor;
    fim_fila = (fim_fila + 1) % MAX_REQUISICOES;

    contador_operacoes++;
    if (contador_operacoes % 10 == 0) {
        fila_requisicoes[fim_fila].id = id_contador++;
        fila_requisicoes[fim_fila].operacao = 3;
        fim_fila = (fim_fila + 1) % MAX_REQUISICOES;
    }

    pthread_cond_signal(&cond_requisicao);
    pthread_mutex_unlock(&mutex_fila);
}

void *cliente(void *arg) {
    int id = *(int *)arg;

    srand(time(NULL) + id);

    while (!encerrar) {
        int operacao = rand() % 2 + 1;
        int id_origem = rand() % NUM_CONTAS;
        int id_destino = rand() % NUM_CONTAS;
        float valor = (float)(rand() % 1000) / 10.0;

        if (operacao == 1) {
            adicionar_requisicao(operacao, id_origem, -1, valor);
        } else if (operacao == 2 && id_origem != id_destino) {
            adicionar_requisicao(operacao, id_origem, id_destino, valor);
        }
        sleep(1);
    }
    return NULL;
}

void *temporizador(void *arg) {
    sleep(DURACAO_EXECUCAO);
    pthread_mutex_lock(&mutex_fila);
    encerrar = 1;
    pthread_cond_broadcast(&cond_requisicao);
    pthread_mutex_unlock(&mutex_fila);
    printf("Tempo de execução máximo atingido. Encerrando o programa...\n");
    return NULL;
}

int main() {
    pthread_t threads[NUM_THREADS];
    pthread_t clientes[10];
    pthread_t timer_thread;
    int cliente_ids[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

    pthread_mutex_init(&mutex_contas, NULL);
    pthread_mutex_init(&mutex_fila, NULL);
    pthread_cond_init(&cond_requisicao, NULL);

    for (int i = 0; i < NUM_CONTAS; i++) {
        contas[i].id = i;
        contas[i].saldo = 1000.0;
    }

    pthread_create(&timer_thread, NULL, temporizador, NULL);

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, trabalhador, NULL);
    }

    for (int i = 0; i < 2; i++) {
        pthread_create(&clientes[i], NULL, cliente, &cliente_ids[i]);
    }

    pthread_join(timer_thread, NULL);

    for (int i = 0; i < 2; i++) {
        pthread_join(clientes[i], NULL);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&mutex_contas);
    pthread_mutex_destroy(&mutex_fila);
    pthread_cond_destroy(&cond_requisicao);

    printf("Programa encerrado após %d segundos de execução.\n", DURACAO_EXECUCAO);
    return 0;
}
