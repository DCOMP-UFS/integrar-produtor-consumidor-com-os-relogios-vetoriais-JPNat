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

pthread_cond_t receive_notFull, receive_notEmpty;
pthread_cond_t send_notFull, send_notEmpty;

pthread_cond_t updatedSource, updatedDestination;

sem_t semaphore_Receive, semaphore_Send;

void printClock (int who, Clock showClock);
Clock getClock (Clock *queue, int *queueCount);
void submitClock (Clock clock, int *count, Clock *queue);
void* receiveClock();
void* sendClock();
void event();
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

   pthread_cond_init(&receive_notFull, NULL);
   pthread_cond_init(&receive_notEmpty, NULL);
   pthread_cond_init(&send_notEmpty, NULL);
   pthread_cond_init(&send_notFull, NULL);

   pthread_create(&receiver, NULL, &receiveClock, NULL);
   pthread_create(&deliver, NULL, &sendClock, NULL);

   switch (my_rank)
   {
   case 0:
      event();
      printClock(my_rank, processClock);
      toSendQueue(1);
      receiveFromQueue(1);
      toSendQueue(2);
      receiveFromQueue(2);
      toSendQueue(1);
      event();
      printClock(my_rank, processClock);
      break;
   case 1:
      toSendQueue(0);
      receiveFromQueue(0);
      receiveFromQueue(0);
      break;
   case 2:
      event();
      printClock(my_rank, processClock);
      toSendQueue(0);
      receiveFromQueue(0);
      break;
   default:
      break;
   }

   sem_destroy(&semaphore_Receive);
   sem_destroy(&semaphore_Send);

   pthread_join(receiver, NULL);
   pthread_join(deliver, NULL);

   pthread_mutex_destroy(&receive_mutex);
   pthread_mutex_destroy(&send_mutex);

   pthread_cond_destroy(&receive_notEmpty);
   pthread_cond_destroy(&receive_notFull);
   pthread_cond_destroy(&send_notEmpty);
   pthread_cond_destroy(&send_notFull);
   
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

void* receiveClock()
{
   while (1)
   {   
      int received[BUFFER_SIZE];

      sem_wait(&semaphore_Receive);
      MPI_Recv(received, BUFFER_SIZE, MPI_INT, source, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

      Clock newClock = {{received[0], received[1], received[2]}};
      printf("%d received \nFrom:", my_rank);
      printClock(source, processClock);

      pthread_mutex_lock(&receive_mutex);
      if (clockCountReceive == BUFFER_SIZE)
      {
         pthread_cond_wait(&receive_notFull, &receive_mutex);
      }

      submitClock(newClock, &clockCountReceive, clockReceiveQueue);
      pthread_mutex_unlock(&receive_mutex);
      pthread_cond_signal(&receive_notEmpty);
      return NULL;
   }
}

void* sendClock()
{
   while (1)
   {
      pthread_mutex_lock(&send_mutex);

      if (clockCountSend == 0)
      {
         pthread_cond_wait(&send_notEmpty, &send_mutex);
      }
      
      Clock newClock = getClock(clockSendQueue, &clockCountSend);

      pthread_mutex_unlock(&send_mutex);
      pthread_cond_signal(&send_notFull);

      printf("%d sending \nTo:", destination);
      printClock(my_rank, processClock);

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
      pthread_cond_wait(&send_notFull, &send_mutex);
   }

   submitClock(processClock, &clockCountSend, clockSendQueue);

   pthread_mutex_unlock(&send_mutex);
   pthread_cond_signal(&send_notEmpty);
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
      pthread_cond_wait(&receive_notEmpty, &send_mutex);
   }

   Clock newClock = getClock(clockReceiveQueue, &clockCountReceive);

   pthread_mutex_unlock(&receive_mutex);
   pthread_cond_signal(&receive_notFull);

   for (int i = 0; i < BUFFER_SIZE; i++) {
      processClock.times[i] = processClock.times[i] > newClock.times[i] ? processClock.times[i] : newClock.times[i];
   }
}