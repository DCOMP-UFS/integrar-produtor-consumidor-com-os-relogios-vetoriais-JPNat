/* File:  
 *    pth_pool.c
 *
 * Purpose:
 *    Implementação de um pool de threads
 *
 *
 * Compile:  gcc -g -Wall -o pth_pool pth_pool.c -lpthread -lrt
 * Usage:    ./pth_hello
 */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h> 
#include <unistd.h>
#include <semaphore.h>
#include <time.h>

#define THREAD_NUM 3    // Tamanho do pool de threads consumidoras e produtoras
#define BUFFER_SIZE 7   // Númermo máximo de relógios enfileiradas
#define OPERATIONS_NUM 5


// Coloquei a definição das funções embaixo pois prefiro codar em C assim kasksakasksaksaksakas

typedef struct Clock{
   int value1, value2, value3;
}Clock;

Clock clockQueue[BUFFER_SIZE];
int clockCount = 0;

int countThread;

pthread_mutex_t mutex;

pthread_cond_t condFull;
pthread_cond_t condEmpty;

void printClock(Clock* clock, int id);
//função responsável por tirar print do relógio

void getClock(long id);
//função responsável por pegar os valores do relógio na 
//primeira posição e enfileirar a fila, apagando a primeira posição

void submitClock(Clock clock, long id);
//função que coloca o relógio na primeira posição da fila
//getClock e subimitClock estavam com um problema onde a função cond estava liberando apenas uma
//troquei pthread_cond_signal por pthread_cond_broadcast pra acordar todas caso mais de uma durma

void *fastConsumers(void* args);

void *slowConsumers(void* args);

void *fastProducer(void* args);

void *slowProducer(void* args);

void joinThreads(pthread_t thread[]);
//função que eu gerei pra dar join nas threads de forma mais sucinta no main
//ela pode ser a causa do seg fault e de falhar nos joins na real

/*---------------------------------------------------------------------------------------------------------------------*/
int main(int argc, char* argv[]) {
   pthread_mutex_init(&mutex, NULL);
   
   pthread_cond_init(&condEmpty, NULL);
   pthread_cond_init(&condFull, NULL);

   pthread_t producerThread[THREAD_NUM];
   pthread_t consumerThread[THREAD_NUM];

   countThread = 0;
/*-------------------------------Consumidoras com delay 3 e produtoras com delay 1-------------------------------------*/

   printf("\t\t\t Casos com as produtoras mais rapido\n");

   for (long a = 0; a < THREAD_NUM; a++){
      if (pthread_create(&producerThread[a], NULL, &fastProducer, (void*) a) != 0)
      {
         perror("Failed to create the thread");
      } 
   }

   for (long b = 0; b < THREAD_NUM; b++){
      if (pthread_create(&consumerThread[b], NULL, &slowConsumers, (void*) b) != 0)
      {
         perror("Failed to create the thread");
      }  
   }
   
   while (countThread < OPERATIONS_NUM*6){sleep(10);}
/*-------------------------------Consumidoras com delay 1 e produtoras com delay 3-------------------------------------*/

   printf("\t\t\t Casos com as consumidoras mais rapido\n");
   for (long a = 0; a < THREAD_NUM; a++){
      if (pthread_create(&producerThread[a], NULL, &slowProducer, (void*) a) != 0)
      {
         perror("Failed to create the thread");
      }  
   }

   for (long b = 0; b < THREAD_NUM; b++){
      if (pthread_create(&consumerThread[b], NULL, &fastConsumers, (void*) b) != 0)
      {
         perror("Failed to create the thread");
      }  
   }

   joinThreads(producerThread);
   joinThreads(consumerThread);
   
   pthread_mutex_destroy(&mutex);
   pthread_cond_destroy(&condEmpty);
   pthread_cond_destroy(&condFull);
   return 0;
}
/*--------------------------------------------------------------------*/
void printClock(Clock* clock, int id){ 

   printf("(%d,%d,%d) - ID: %d", clock->value1, clock->value2, clock->value3, id);
}

void getClock(long id){ 

   pthread_mutex_lock(&mutex);
   while (clockCount == 0){
      pthread_cond_wait(&condEmpty, &mutex);
   }
   
   Clock clock = clockQueue[0];
   int i;
   for (i = 0; i < clockCount - 1; i++){
      clockQueue[i] = clockQueue[i+1];
   }
   clockCount--;
   
   printClock(&clock, id);
   printf("C\n");

   countThread++;

   pthread_mutex_unlock(&mutex);
   pthread_cond_signal(&condFull);
}

void submitClock(Clock clock, long id){
   pthread_mutex_lock(&mutex);

   
   while (clockCount == BUFFER_SIZE){
      pthread_cond_wait(&condFull, &mutex);
   }

   clockQueue[clockCount] = clock;
   clockCount++;

   printClock(&clock, id);
   printf("P\n");
   
   countThread++;

   pthread_mutex_unlock(&mutex);
   pthread_cond_signal(&condEmpty);
}

void *fastConsumers(void* args){
   long id = (long) args;

   for(int k = 0; k < OPERATIONS_NUM; k++){
      getClock(id);
   }

   sleep(1);

   return NULL;
}

void *slowConsumers(void* args){
   long id = (long) args;

   for(int k = 0; k < OPERATIONS_NUM; k++){
      getClock(id);
      sleep(3);
   }

   return NULL;
}

void *fastProducer(void* args){
   long id = (long) args;

   for(int k = 0; k < OPERATIONS_NUM; k++){
      Clock producerClock;
      producerClock.value1 = rand() % 100;
      producerClock.value2 = rand() % 100;
      producerClock.value3 = rand() % 100;

      submitClock(producerClock, id);

      sleep(1);
   }
   return NULL;
}

void *slowProducer(void* args){
   long id = (long) args;

   for(int k = 0; k < OPERATIONS_NUM; k++){
      Clock producerClock;
      producerClock.value1 = rand() % 100;
      producerClock.value2 = rand() % 100;
      producerClock.value3 = rand() % 100;

      submitClock(producerClock, id);

      sleep(3);
   }
   return NULL;
}

void joinThreads(pthread_t thread[])
{
   for (long i = 0; i < THREAD_NUM; i++){  
      if (pthread_join(thread[i], NULL) != 0) {
         perror("Failed to join the thread");
      }  
   }
}
