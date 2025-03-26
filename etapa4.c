/* File:
 *
 *
 * Purpose: Implementação do algoritmo de snapshot de Chandy-Lamport
 *
 *
 *
 * Compile: mpicc -o etapa4-v2 etapa4-v2.c -lpthread
 * Usage:   mpiexec -n 3 ./etapa4-v2
 */

#include <stdio.h>
#include <pthread.h>
#include <math.h>
#include <string.h>
#include <mpi.h>

#define NUM_OF_PROCESS 3
#define BUFFER_SIZE 5 // Número máximo de tarefas enfileiradas

typedef struct Clock
{
    int p[NUM_OF_PROCESS];
} Clock;
typedef struct Message
{
    Clock clock;
    int destination;
    int is_marker;
} Message;

// Buffers para armazenamento de mensagens
Message BUFFERin[BUFFER_SIZE];
int bufferCountIn = 0;

Message BUFFERout[BUFFER_SIZE];
int bufferCountOut = 0;

// Mutexes e condições para sincronização dos buffers
pthread_mutex_t mutex_in;
pthread_cond_t condFull_in;
pthread_cond_t condEmpty_in;

pthread_mutex_t mutex_out;
pthread_cond_t condFull_out;
pthread_cond_t condEmpty_out;

pthread_mutex_t clock_mutex;

// Variáveis para o snapshot
int realizandoSnapshot = 0;
int controleMarcadorRecebido[NUM_OF_PROCESS] = {0};
Clock snapshotClock;
Clock process_clock_global;

// Estruturas para armazenar mensagens em trânsito durante o snapshot
typedef struct
{
    int sender;
    Message message;
} InTransitMessage;

InTransitMessage messagesInTransit[NUM_OF_PROCESS][BUFFER_SIZE];
int inTransitCount[NUM_OF_PROCESS] = {0};

// Variáveis para sincronização do snapshot
pthread_mutex_t snapshot_mutex;
pthread_cond_t snapshot_cond;
int snapshotComplete = 0;

// Funções de manipulação dos buffers
Message producerIn(int id)
{
    pthread_mutex_lock(&mutex_in);

    while (bufferCountIn == 0)
    {
        pthread_cond_wait(&condEmpty_in, &mutex_in);
    }

    Message message = BUFFERin[0];

    for (int i = 0; i < bufferCountIn - 1; i++)
    {
        BUFFERin[i] = BUFFERin[i + 1];
    }
    bufferCountIn--;

    pthread_mutex_unlock(&mutex_in);
    pthread_cond_signal(&condFull_in);

    return message;
}

void consumerClk(Message message, int id)
{
    pthread_mutex_lock(&mutex_in);

    while (bufferCountIn == BUFFER_SIZE)
    {
        pthread_cond_wait(&condFull_in, &mutex_in);
    }

    BUFFERin[bufferCountIn++] = message;

    pthread_mutex_unlock(&mutex_in);
    pthread_cond_signal(&condEmpty_in);
}

Message producerClk(int id)
{
    pthread_mutex_lock(&mutex_out);

    while (bufferCountOut == 0)
    {
        pthread_cond_wait(&condEmpty_out, &mutex_out);
    }

    Message message = BUFFERout[0];

    for (int i = 0; i < bufferCountOut - 1; i++)
    {
        BUFFERout[i] = BUFFERout[i + 1];
    }
    bufferCountOut--;

    pthread_mutex_unlock(&mutex_out);
    pthread_cond_signal(&condFull_out);

    return message;
}

void consumerOut(Message message, int id)
{
    pthread_mutex_lock(&mutex_out);

    while (bufferCountOut == BUFFER_SIZE)
    {
        pthread_cond_wait(&condFull_out, &mutex_out);
    }

    BUFFERout[bufferCountOut++] = message;

    pthread_mutex_unlock(&mutex_out);
    pthread_cond_signal(&condEmpty_out);
}

void UpdateClock(Clock *old, Clock new)
{
    for (int i = 0; i < NUM_OF_PROCESS; i++)
    {
        old->p[i] = (old->p[i] > new.p[i]) ? old->p[i] : new.p[i];
    }
}

void Event(int pid)
{
    int my_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

    pthread_mutex_lock(&clock_mutex);
    process_clock_global.p[pid]++;
    printf("Processo: %d (EVENTO) :: (%d, %d, %d)\n", my_rank,
           process_clock_global.p[0], process_clock_global.p[1], process_clock_global.p[2]);
    pthread_mutex_unlock(&clock_mutex);
}

void Send(int dest_pid)
{
    int my_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

    Event(my_rank);

    Message message;
    pthread_mutex_lock(&clock_mutex);
    message.clock = process_clock_global;
    pthread_mutex_unlock(&clock_mutex);
    message.destination = dest_pid;
    message.is_marker = 0;

    consumerOut(message, my_rank);

    printf("Processo %d (ENVIA para %d) :: (%d, %d, %d)\n", my_rank, dest_pid,
           process_clock_global.p[0], process_clock_global.p[1], process_clock_global.p[2]);
}

void Receive(int src_pid)
{
    Message message;
    int my_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

    message = producerIn(my_rank);

    //    if (message.is_marker) {
    //        return;
    //    }

    pthread_mutex_lock(&clock_mutex);
    // Mescla o relógio recebido com o relógio local
    UpdateClock(&process_clock_global, message.clock);
    // Incrementa o relógio local
    process_clock_global.p[my_rank]++;
    pthread_mutex_unlock(&clock_mutex);

    printf("Processo: %d (RECEBE de %d) :: (%d, %d, %d)\n", my_rank, src_pid,
           process_clock_global.p[0], process_clock_global.p[1], process_clock_global.p[2]);
}

void initiateSnapshot(int my_rank)
{
    printf("Processo %d iniciando snapshot\n", my_rank);

    pthread_mutex_lock(&clock_mutex);
    realizandoSnapshot = 1;
    // Registra o estado
    snapshotClock = process_clock_global;
    printf("Processo %d registrou seu estado: [%d, %d, %d]\n", my_rank,
           snapshotClock.p[0], snapshotClock.p[1], snapshotClock.p[2]);
    pthread_mutex_unlock(&clock_mutex);

    // Marca que recebemos marcador em nossos próprios canais
    controleMarcadorRecebido[my_rank] = 1;

    // Envia marcadores para todos os canais de saída via BUFFERout
    for (int i = 0; i < NUM_OF_PROCESS; i++)
    {
        if (i != my_rank)
        {
            Message markerMessage;
            for (int j = 0; j < NUM_OF_PROCESS; j++)
            {
                markerMessage.clock.p[j] = -1;
            }
            markerMessage.destination = i;
            markerMessage.is_marker = 1;
            consumerOut(markerMessage, my_rank);
        }
    }

    // Inicia a sincronização do snapshot
    pthread_mutex_lock(&snapshot_mutex);
    snapshotComplete = 0;
    pthread_mutex_unlock(&snapshot_mutex);
}

void startClk0(void *args);
void *startClk1(void *args);
void *startClk2(void *args);
void startReceiver(void *args);
void startSender(void *args);

/* -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --*/
    int main(void)
{
    int my_rank;

    MPI_Init(NULL, NULL);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

    pthread_mutex_init(&mutex_in, NULL);
    pthread_cond_init(&condEmpty_in, NULL);
    pthread_cond_init(&condFull_in, NULL);

    pthread_mutex_init(&mutex_out, NULL);
    pthread_cond_init(&condEmpty_out, NULL);
    pthread_cond_init(&condFull_out, NULL);

    pthread_mutex_init(&clock_mutex, NULL);

    // Inicializa mutex e condição para o snapshot
    pthread_mutex_init(&snapshot_mutex, NULL);
    pthread_cond_init(&snapshot_cond, NULL);

    pthread_t clk;
    pthread_t receiver;
    pthread_t sender;

    // Inicializa o clock global
    pthread_mutex_lock(&clock_mutex);
    for (int i = 0; i < NUM_OF_PROCESS; i++)
    {
        process_clock_global.p[i] = 0;
    }
    pthread_mutex_unlock(&clock_mutex);

    // Inicializa controle de marcadores recebidos
    for (int i = 0; i < NUM_OF_PROCESS; i++)
    {
        controleMarcadorRecebido[i] = 0;
    }

    // Cria a thread para receber relógios de outros processos
    pthread_create(&receiver, NULL, &startReceiver, NULL);

    // Cria thread para enviar relógios para outros processos
    pthread_create(&sender, NULL, &startSender, NULL);

    // Cria as threads dos relógios principais em cada processo
    if (my_rank == 0)
    {
        pthread_create(&clk, NULL, &startClk0, NULL);
    }
    else if (my_rank == 1)
    {
        pthread_create(&clk, NULL, &startClk1, NULL);
    }
    else if (my_rank == 2)
    {
        pthread_create(&clk, NULL, &startClk2, NULL);
    }

    pthread_join(clk, NULL);

    pthread_mutex_destroy(&mutex_in);
    pthread_cond_destroy(&condEmpty_in);
    pthread_cond_destroy(&condFull_in);

    pthread_mutex_destroy(&mutex_out);
    pthread_cond_destroy(&condEmpty_out);
    pthread_cond_destroy(&condFull_out);

    pthread_mutex_destroy(&clock_mutex);

    // Destroi mutex e condição do snapshot
    pthread_mutex_destroy(&snapshot_mutex);
    pthread_cond_destroy(&snapshot_cond);

    /* Finaliza MPI */
    MPI_Finalize();
    return 0;
} /* main */

/* -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -*/
    void startReceiver(void *args)
{
    int p[NUM_OF_PROCESS];
    MPI_Status status;
    int my_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

    while (1)
    {
        MPI_Recv(p, NUM_OF_PROCESS, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);

        int sender = status.MPI_SOURCE;

        // Verifica se é marcador
        if (p[0] == -1)
        {
            // Marcador recebido
            printf("Processo %d recebeu marcador de %d\n", my_rank, sender);

            pthread_mutex_lock(&clock_mutex);
            if (!realizandoSnapshot)
            {
                // Primeira vez recebendo marcador, registra o estado
                realizandoSnapshot = 1;
                // Registra o estado (clock)
                snapshotClock = process_clock_global;
                printf("Processo %d registrou seu estado: [%d, %d, %d]\n", my_rank,
                       snapshotClock.p[0], snapshotClock.p[1], snapshotClock.p[2]);
                pthread_mutex_unlock(&clock_mutex);

                // Marca que recebemos marcador deste canal
                controleMarcadorRecebido[sender] = 1;

                // Envia marcadores para todos os canais de saída
                for (int i = 0; i < NUM_OF_PROCESS; i++)
                {
                    if (i != my_rank)
                    {
                        Message markerMessage;
                        for (int j = 0; j < NUM_OF_PROCESS; j++)
                        {
                            markerMessage.clock.p[j] = -1;
                        }
                        markerMessage.destination = i;
                        markerMessage.is_marker = 1;
                        consumerOut(markerMessage, my_rank);
                    }
                }
            }
            else
            {
                // Já começamos o snapshot
                pthread_mutex_unlock(&clock_mutex);

                // Marca que recebemos marcador deste canal
                controleMarcadorRecebido[sender] = 1;
            }

            // Verifica se recebemos marcadores de todos os canais
            int markersReceived = 1;
            for (int i = 0; i < NUM_OF_PROCESS; i++)
            {
                if (i != my_rank && controleMarcadorRecebido[i] == 0)
                {
                    markersReceived = 0;
                    break;
                }
            }
            if (markersReceived)
            {
                // Snapshot completo
                pthread_mutex_lock(&clock_mutex);
                realizandoSnapshot = 0;
                printf("Processo %d snapshot completo. Clock: [%d, %d, %d]\n", my_rank,
                       snapshotClock.p[0], snapshotClock.p[1], snapshotClock.p[2]);

                // Imprime mensagens em trânsito
                for (int i = 0; i < NUM_OF_PROCESS; i++)
                {
                    if (i != my_rank)
                    {
                        printf("Mensagens em trânsito do canal %d -> %d:\n", i, my_rank);
                        for (int j = 0; j < inTransitCount[i]; j++)
                        {
                            // Imprime a mensagem armazenada
                            Message msg = messagesInTransit[i][j].message;
                            printf("Mensagem de %d: Clock [%d, %d, %d]\n", i,
                                   msg.clock.p[0], msg.clock.p[1], msg.clock.p[2]);
                        }
                    }
                }

                // Reseta as contagens de mensagens em trânsito
                for (int i = 0; i < NUM_OF_PROCESS; i++)
                {
                    inTransitCount[i] = 0;
                }

                // Reseta controleMarcadorRecebido
                for (int i = 0; i < NUM_OF_PROCESS; i++)
                {
                    controleMarcadorRecebido[i] = 0;
                }
                pthread_mutex_unlock(&clock_mutex);

                // Sinaliza que o snapshot está completo
                pthread_mutex_lock(&snapshot_mutex);
                snapshotComplete = 1;
                pthread_cond_signal(&snapshot_cond);
                pthread_mutex_unlock(&snapshot_mutex);
            }
        }
        else
        {
            // Mensagem normal
            Message message;
            for (int i = 0; i < NUM_OF_PROCESS; i++)
                message.clock.p[i] = p[i];
            message.destination = my_rank;
            message.is_marker = 0;

            pthread_mutex_lock(&clock_mutex);
            if (realizandoSnapshot && controleMarcadorRecebido[sender] == 0)
            {
                // Armazena a mensagem em trânsito
                messagesInTransit[sender][inTransitCount[sender]++] = (InTransitMessage){sender, message};
                printf("Processo %d registrando mensagem de %d durante o snapshot\n", my_rank, sender);
            }
            pthread_mutex_unlock(&clock_mutex);

            consumerClk(message, my_rank);
        }
    }
    return NULL;
}

void startSender(void *args)
{
    int my_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

    while (1)
    {
        Message message = producerClk(my_rank);
        int is_marker = message.is_marker;
        int dest = message.destination;

        if (is_marker)
        {
            int marker[NUM_OF_PROCESS];
            for (int i = 0; i < NUM_OF_PROCESS; i++)
            {
                marker[i] = -1;
            }
            MPI_Send(marker, NUM_OF_PROCESS, MPI_INT, dest, 0, MPI_COMM_WORLD);
            printf("Processo %d enviou marcador para %d\n", my_rank, dest);
        }
        else
        {
            MPI_Send(message.clock.p, NUM_OF_PROCESS, MPI_INT, dest, 0, MPI_COMM_WORLD);
        }
    }
    return NULL;
}

void startClk0(void *args)
{
    int my_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

    Event(my_rank);

    Send(1);

    Receive(1);

    // Inicia o snapshot
    initiateSnapshot(my_rank);

    // Aguarda o término do snapshot
    pthread_mutex_lock(&snapshot_mutex);
    while (!snapshotComplete)
    {
        pthread_cond_wait(&snapshot_cond, &snapshot_mutex);
    }
    pthread_mutex_unlock(&snapshot_mutex);

    Send(2);

    Receive(2);

    Send(1);

    Event(my_rank);

    return NULL;
}

void *startClk1(void *args)
{
    int my_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

    Send(0);

    Receive(0);

    Receive(0);

    return NULL;
}

void *startClk2(void *args)
{
    int my_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

    Event(my_rank);

    Send(0);

    Receive(0);

    return NULL;
}