#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include <time.h>
#include <mpi.h>

#define BUFFER_SIZE 3
#define THREAD_NUM 3

typedef struct{
   int times[3];
}Clock;

Clock processClock = {{0, 0, 0}};

Clock clockQueueEntry[BUFFER_SIZE]; // Primeira fila para chegar na thread relogio
Clock clockQueueExit[BUFFER_SIZE];  // Fila para sair do relogio

int clockCountEntry = 0;
int clockCountExit = 0;
int my_rank;

pthread_mutex_t mutex;

pthread_cond_t cond_recieve, cond_send, cond_process;

void printClock
(
   int who,
   Clock showClock
);
// função responsável por mostrar o relógio

Clock getClock
(
   Clock *queue,
   int queueCount
);
// função responsável por pegar os valores do relógio na primeira
//  posição e enfileirar a fila, apagando a primeira posição

void submitClock
(
   Clock clock,
   int count,
   Clock *queue
);
// Funcao responsável por colocar um relógio em uma das filas

void recieveClock
(
   void *whoSent
);
// função da thread que recebe relógio de outros processos

void sendClock
(
   void *clock
);
// função da thread que envia relógio de outros processos

void updateClock
(
   void *clock
);
// função responsável pelo controle da thread central
// a ideia é que seja uma lista com comando e processo alvo

void event
();
// função que aumenta o valor do relógio a depender do processo

void process0();

void process1();

void process2();

int main
(
   int argc,
   char *argv[]
){
   MPI_Init(NULL, NULL);
   MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

   switch (my_rank)
   {
   case 0:
      process0();
      break;
   case 1:
      process1();
      break;
   case 2:
      process2();
      break;
   default:
      break;
   }

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
   int queueCount
){
   Clock clock = queue[0];
   int i;
   for (i = 0; i < queueCount - 1; i++)
   {
      queue[i] = queue[i + 1];
   }

   queueCount--;

   return clock;
}

void submitClock
(
   Clock clock,
   int count,
   Clock *queue
){
   queue[count] = clock;
   count++;
}

void recieveClock
(
   void *whoSent
){
   int recieved[3];

   MPI_Recv(recieved, 3, MPI_INT, (long)whoSent, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE );

   pthread_mutex_lock(&mutex);
   if (clockCountEntry == 2)
   {
      pthread_cond_wait(&cond_recieve, &mutex);
   }

   Clock newClock = {{recieved[0], recieved[1], recieved[2]}};

   printf("%d recieved \nFrom:", my_rank);
   printClock((int)(long)whoSent, newClock);

   submitClock( newClock, clockCountEntry, (Clock*)clockQueueEntry );
   pthread_mutex_unlock(&mutex);
   pthread_cond_signal(&cond_process);
}

void sendClock(
   void *toWho
){
   pthread_mutex_lock(&mutex);

   if (clockCountExit == 0)
   {
      pthread_cond_wait(&cond_send, &mutex);
   }

   Clock newClock = getClock(clockQueueExit, clockCountExit);

   printf("sending from %d\nTo ", my_rank);
   printClock((int)(long)toWho, newClock);

   pthread_mutex_unlock(&mutex);
   pthread_cond_signal(&cond_process);

   MPI_Send(newClock.times, 3, MPI_INT, (long)toWho, 0, MPI_COMM_WORLD);
}

void updateClock
(
   void* arg
){
   pthread_mutex_lock(&mutex);
   int action = *(int* )arg;

   switch (action)
   {
   case 1:// evento
      event();
      printf("Event on ");
      printClock(my_rank, processClock);
      break;
   case 2:// pega relógio da fila de entrada
      if (clockCountEntry == 0){
         pthread_cond_wait(&cond_process, &mutex);
      }
      Clock newClok = getClock(clockQueueEntry, clockCountEntry);
      for (int i = 0; i < 3; i++) {
         processClock.times[i] = processClock.times[i] > newClok.times[i] ? processClock.times[i] : newClok.times[i];
      }
      printf("Update on ");
      printClock(my_rank, processClock);
      break;
   case 3:// envia relógio pra fila de envio
      event();
      if (clockCountExit == 2){
         pthread_cond_wait(&cond_process, &mutex);
      }
      submitClock (processClock, clockCountExit, clockQueueExit);
      break;
   default:
      break;
   }

   pthread_mutex_unlock(&mutex);
   if(action == 2)
   {
      pthread_cond_signal(&cond_recieve);
   }
   if(action == 3)
   {
      pthread_cond_signal(&cond_send);
   }
}

void event
(){
   processClock.times[my_rank]++;
}

void process0
(){
   pthread_t reciever;
   pthread_t body;// responsável pelo corpo do relógio
   pthread_t deliver;
   
   pthread_mutex_init(&mutex, NULL);

   pthread_cond_init(&cond_recieve, NULL);
   pthread_cond_init(&cond_send, NULL);
   pthread_cond_init(&cond_process, NULL);
   // Programe cada processo dentro do bloco
   // utilizando as funções clockSend clockRecieve e clockUpdate
   pthread_create(&body, NULL, &updateClock, (void*) 1);
   pthread_create(&deliver, NULL, &sendClock, (void*) 1);
   pthread_create(&reciever, NULL, &recieveClock, (void*) 1);
   pthread_create(&deliver, NULL, &sendClock, (void*) 2);
   pthread_create(&reciever, NULL, &recieveClock, (void*) 2);
   pthread_create(&deliver, NULL, &sendClock, (void*) 1);
   pthread_create(&body, NULL, &updateClock, (void*) 1);
   // fim da area onde se deve programar
   pthread_join(reciever, NULL);
   pthread_join(body, NULL);
   pthread_join(deliver, NULL);

   pthread_mutex_destroy(&mutex);
   pthread_cond_destroy(&cond_recieve);
   pthread_cond_destroy(&cond_process);
   pthread_cond_destroy(&cond_send);
}

void process1
(){
   pthread_t reciever;
   pthread_t body;// responsável pelo corpo do relógio
   pthread_t deliver;
   
   pthread_mutex_init(&mutex, NULL);

   pthread_cond_init(&cond_recieve, NULL);
   pthread_cond_init(&cond_send, NULL);
   pthread_cond_init(&cond_process, NULL);
   // Programe cada processo dentro do bloco
   // utilizando as funções clockSend clockRecieve e clockUpdate
   pthread_create(&deliver, NULL, &sendClock, (void*) 0);
   pthread_create(&reciever, NULL, &recieveClock, (void*) 0);
   pthread_create(&reciever, NULL, &recieveClock, (void*) 0);
   // fim da area onde se deve programar
   pthread_join(reciever, NULL);
   pthread_join(body, NULL);
   pthread_join(deliver, NULL);

   pthread_mutex_destroy(&mutex);
   pthread_cond_destroy(&cond_recieve);
   pthread_cond_destroy(&cond_process);
   pthread_cond_destroy(&cond_send);
}

void process2
(){
   pthread_t reciever;
   pthread_t body;// responsável pelo corpo do relógio
   pthread_t deliver;
   
   pthread_mutex_init(&mutex, NULL);

   pthread_cond_init(&cond_recieve, NULL);
   pthread_cond_init(&cond_send, NULL);
   pthread_cond_init(&cond_process, NULL);
   // Programe cada processo dentro do bloco
   // utilizando as funções clockSend clockRecieve e clockUpdate
   pthread_create(&body, NULL, &updateClock, (void*) 1);
   pthread_create(&deliver, NULL, &sendClock, (void*) 0);
   pthread_create(&reciever, NULL, &recieveClock, (void*) 0);
   // fim da area onde se deve programar
   pthread_join(reciever, NULL);
   pthread_join(body, NULL);
   pthread_join(deliver, NULL);

   pthread_mutex_destroy(&mutex);
   pthread_cond_destroy(&cond_recieve);
   pthread_cond_destroy(&cond_process);
   pthread_cond_destroy(&cond_send);
}