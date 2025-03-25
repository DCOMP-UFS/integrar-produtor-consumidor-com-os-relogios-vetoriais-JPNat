#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include <time.h>
#include <mpi.h>

#define BUFFER_SIZE 3

typedef struct{
   int times[BUFFER_SIZE];
}Clock;

Clock processClock = {{0, 0, 0}};

Clock clockReceiveQueue[BUFFER_SIZE]; // Primeira fila para chegar na thread relogio
Clock clockSendQueue[BUFFER_SIZE];  // Fila para sair do relogio

int clockCountReceive = 0;
int clockCountSend = 0;
int my_rank;

int source, destination;

pthread_mutex_t receive_mutex, send_mutex;

pthread_cond_t cond_receive_full, cond_receive_empty;
pthread_cond_t cond_send_full, cond_send_empty;

pthread_cond_t updatedSource, updatedDestination;

sem_t semaphore_Receive, semaphore_Send;

void printClock
(
   int who,
   Clock showClock
);
// função responsável por mostrar o relógio

Clock getClock
(
   Clock *queue,
   int *queueCount
);
// função responsável por pegar os valores do relógio na primeira
//  posição e enfileirar a fila, apagando a primeira posição

void submitClock
(
   Clock clock,
   int *count,
   Clock *queue
);
// Funcao responsável por colocar um relógio em uma das filas

void* receiveClock
(
   void *whoSent
);
// função da thread que recebe relógio de outros processos

void* sendClock
(
   void *clock
);
// função da thread que envia relógio de outros processos

void event
();
// função que aumenta o valor do relógio a depender do processo

void toSendQueue(int to);
void receiveFromQueue(int from);

int main
(
   int argc,
   char *argv[]
){
   MPI_Init(NULL, NULL);
   MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

   pthread_t receiver;
   pthread_t deliver;

   sem_init(&semaphore_Send, 0, 0);
   sem_init(&semaphore_Receive, 0, 0);
   
   pthread_mutex_init(&receive_mutex, NULL);
   pthread_mutex_init(&send_mutex, NULL);

   pthread_cond_init(&cond_receive_full, NULL);
   pthread_cond_init(&cond_receive_empty, NULL);
   pthread_cond_init(&cond_send_empty, NULL);
   pthread_cond_init(&cond_send_full, NULL);

   pthread_create(&receiver, NULL, &receiveClock, NULL);
   pthread_create(&deliver, NULL, &sendClock, NULL);

   switch (my_rank)
   {
   case 0:
      event();
      toSendQueue(1);
      receiveFromQueue(1);
      toSendQueue(2);
      receiveFromQueue(2);
      toSendQueue(1);
      event();
      break;
   case 1:
      toSendQueue(0);
      receiveFromQueue(0);
      receiveFromQueue(0);
      break;
   case 2:
      event();
      toSendQueue(0);
      receiveFromQueue(0);
      break;
   default:
      break;
   }

   for (int i = 0; i < 3; i++)
   {
      if (my_rank == i){
         printClock(my_rank, processClock);
      }
   }
   sem_destroy(&semaphore_Receive);
   sem_destroy(&semaphore_Send);

   MPI_Finalize();
   return 0;
} /* main */

void printClock
(
   int who,
   Clock showClock
){
   printf("Process: %d, Clock: (%d, %d, %d)\n", who, showClock.times[0], showClock.times[1], showClock.times[2]);
}

Clock getClock
(
   Clock *queue,
   int *queueCount
){
   Clock clock = queue[0];
   int i;
   for (i = 0; i < *queueCount - 1; i++)
   {
      queue[i] = queue[i + 1];
   }

   (*queueCount)--;

   return clock;
}

void submitClock
(
   Clock clock,
   int *count,
   Clock *queue
){
   queue[*count] = clock;
   (*count)++;
}

void* receiveClock
(
   void *whoSent
){
   while (1)
   {   
      int received[BUFFER_SIZE];

      sem_wait(&semaphore_Receive);
      MPI_Recv(received, BUFFER_SIZE, MPI_INT, source, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE );

      pthread_mutex_lock(&receive_mutex);
      
      Clock newClock = {{received[0], received[1], received[2]}};
      printf("%d received \nFrom:", my_rank);
      printClock(source, newClock);

      if (clockCountReceive == BUFFER_SIZE)
      {
         pthread_cond_wait(&cond_receive_full, &receive_mutex);
      }

      submitClock(newClock, &clockCountReceive, clockReceiveQueue);
      pthread_mutex_unlock(&receive_mutex);
      pthread_cond_signal(&cond_send_empty);
      return NULL;
   }
}

void* sendClock(
   void *toWho
){
   while (1)
   {
      pthread_mutex_lock(&send_mutex);

      if (clockCountSend == 0)
      {
         pthread_cond_wait(&cond_send_empty, &send_mutex);
      }
      
      Clock newClock = getClock(clockSendQueue, &clockCountSend);

      pthread_mutex_unlock(&send_mutex);
      pthread_cond_signal(&cond_send_full);

      printf("sending from %d\nTo ", my_rank);
      printClock(destination, newClock);

      sem_wait(&semaphore_Send);
      MPI_Send(newClock.times, BUFFER_SIZE, MPI_INT, destination, 0, MPI_COMM_WORLD);
   }
   return NULL;
}

void event
(){
   processClock.times[my_rank]++;
}

void toSendQueue(int to)
{
   processClock.times[my_rank]++;
   pthread_mutex_lock(&send_mutex);
   if (clockCountSend == BUFFER_SIZE){
      pthread_cond_wait(&cond_send_full, &send_mutex);
   }

   submitClock(processClock, &clockCountSend, clockSendQueue);

   pthread_mutex_unlock(&send_mutex);
   pthread_cond_signal(&cond_receive_empty);
   destination = to;
   sem_post(&semaphore_Send);
}

void receiveFromQueue(int from)
{
   source = from;
   sem_post(&semaphore_Receive);

   processClock.times[my_rank]++;
   pthread_mutex_lock(&receive_mutex);
   if (clockCountReceive == 0){
      pthread_cond_wait(&cond_receive_empty, &send_mutex);
   }

   Clock newClock = getClock(clockReceiveQueue, &clockCountReceive);

   pthread_mutex_unlock(&receive_mutex);
   pthread_cond_signal(&cond_receive_full);

   for (int i = 0; i < BUFFER_SIZE; i++) {
      processClock.times[i] = processClock.times[i] > newClock.times[i] ? processClock.times[i] : newClock.times[i];
   }
}