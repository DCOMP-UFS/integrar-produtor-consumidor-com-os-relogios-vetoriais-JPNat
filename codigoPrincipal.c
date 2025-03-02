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
 #include <mpi.h> 
 
 #define THREAD_NUM 3    // Tamanho do pool de threads consumidoras e produtoras
 #define BUFFER_SIZE 7   // Númermo máximo de relógios enfileiradas
 
 typedef struct Clock{
    int value1, value2, value3;
 }Clock;
 
 Clock clockQueueEntry[BUFFER_SIZE]; //Primeira fila para chegar na thread relogio
 Clock clockQueueExit[BUFFER_SIZE];  //Fila para sair do relogio
 
 int clockCountEntry = 0;
 int clockCountExit = 0;
 
 pthread_mutex_t mutex;
 
 pthread_cond_t condFull;
 pthread_cond_t condEmpty;
 
 
 void printClock(Clock* clock, int id){  //função responsável por tirar print do relógio
 
    printf("(%d,%d,%d) - ID: %d", clock->value1, clock->value2, clock->value3, id);
 }
 
 
 Clock getClock(Clock queue, int count){ //função responsável por pegar os valores do relógio na 
                   // primeira posição e enfileirar a fila, apagando a primeira posição
 
    pthread_mutex_lock(&mutex);
    
    while (count == 0){
       pthread_cond_wait(&condEmpty, &mutex);
    }
    
    Clock clock = queue[0];
    int i;
    for (i = 0; i < count - 1; i++){
       queue[i] = queue[i+1];
    }
    count--;
    
    pthread_mutex_unlock(&mutex);
    pthread_cond_signal(&condFull);
    return clock;
 }
 
 
 void submitClock(Clock clock, int count, Clock queue){  //função que coloca o relógio na primeira posição da fila
    
   pthread_mutex_lock(&mutex);
 
    while (count == BUFFER_SIZE){
       pthread_cond_wait(&condFull, &mutex);
    }
 
    queue[count] = clock;
    count++;
 
    pthread_mutex_unlock(&mutex);
    pthread_cond_signal(&condEmpty);
 }
 
 
 void *producer(void* args){
    
   long id = (long) args;
    while(1){
       Clock producerClock;
       producerClock.value1 = rand() % 100;
       producerClock.value2 = rand() % 100;
       producerClock.value3 = rand() % 100;
 
       submitClock(producerClock);
 
       sleep(1);
    }
    return NULL;
 }
 
 
 void *consumers(void* args){
 
    long id = (long) args; 
 
    while(1){
 
       Clock clock = getClock();
       printClock(&clock, id);
       sleep(2);
 
    }
 
    return NULL;
 }
 
 void Event(int pid, Clock *clock){
   clock->p[pid]++;   
}


void Receive(int pid, Clock *clock, int remetente){

   int message[3];
   MPI_Recv(message, 3, MPI_INT, remetente, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

   for (int i = 0; i < 3; i++) {
        if (message[i] > clock->p[i]) {
            clock->p[i] = message[i];
        }
    }
    
   clock->p[pid]++;


}


 
 void *startThread(void* args);  
 
 /*--------------------------------------------------------------------*/
 int main(int argc, char* argv[]) {
 
    pthread_mutex_init(&mutex, NULL);
    
    pthread_cond_init(&condEmpty, NULL);
    pthread_cond_init(&condFull, NULL);
 
 
    pthread_t thread[THREAD_NUM]; 
    long i;

    for (i = 0; i < THREAD_NUM; i++){  
       if (pthread_create(&thread[i], NULL, &startThread, (void*) i) != 0) {
          perror("Failed to create the thread");
       }  
    }
    
   
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&condEmpty);
    pthread_cond_destroy(&condFull);
    return 0;
 }  /* main */
 
 /*-------------------------------------------------------------------*/
 
 
 void *startThread(void* args) {
    long id = (long) args; 
    while (1){ 
       Clock clock = getClock();
       executeClock(&clock, id);
       sleep(rand()%5);
    }
    return NULL;
 } 
 
 