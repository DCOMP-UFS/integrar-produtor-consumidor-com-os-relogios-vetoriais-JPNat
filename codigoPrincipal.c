#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <mpi.h>

#define BUFFER_SIZE 3

typedef struct{
   int times[BUFFER_SIZE];
}Clock;
typedef struct
{
   Clock clock;
   int adress;
}Message;

Clock processClock = {{0, 0, 0}};

Message messageReceiveQueue[BUFFER_SIZE]; // Primeira fila para chegar na thread relogio
Message messageSendQueue[BUFFER_SIZE];  // Fila para sair do relogio

int messageCountReceive = 0;
int messageCountSend = 0;
int my_rank;

pthread_mutex_t receive_mutex, send_mutex;

pthread_cond_t receive_notFull, receive_notEmpty;
pthread_cond_t send_notFull, send_notEmpty;

void printClock (int who, Clock showClock);
Message getMessage (Message* queue, int *queueCount);
void submitMessage (Message message, int *count, Message* queue);
void* receiveClock();
void* sendClock();
void event();
void toSendQueue(int to);
void receiveFromQueue(int from);

int main (int argc, char *argv[])
{
   MPI_Init(NULL, NULL);
   MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

   pthread_t receiver;
   pthread_t deliver;
   
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
      printf("a");
      printClock(my_rank, processClock);
      toSendQueue(1);
      printf("b");
      printClock(my_rank, processClock);
      receiveFromQueue(1);
      printf("c");
      printClock(my_rank, processClock);
      toSendQueue(2);
      printf("d");
      printClock(my_rank, processClock);
      receiveFromQueue(2);
      printf("e");
      printClock(my_rank, processClock);
      toSendQueue(1);
      printf("f");
      printClock(my_rank, processClock);
      event();
      printf("g");
      printClock(my_rank, processClock);
      break;
   case 1:
      toSendQueue(0);
      printf("h");
      printClock(my_rank, processClock);
      receiveFromQueue(0);
      printf("i");
      printClock(my_rank, processClock);
      receiveFromQueue(0);
      printf("j");
      printClock(my_rank, processClock);
      break;
   case 2:
      event();
      printf("k");
      printClock(my_rank, processClock);
      toSendQueue(0);
      printf("l");
      printClock(my_rank, processClock);
      receiveFromQueue(0);
      printf("m");
      printClock(my_rank, processClock);
      break;
   default:
      break;
   }

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
   printf("%d clock: (%d, %d, %d)\n", who, showClock.times[0], showClock.times[1], showClock.times[2]);
}

Message getMessage
(
   Message* queue,
   int *queueCount
){
   Message message = queue[0];
   for (int i = 0; i < *queueCount - 1; i++)
   {
      queue[i] = queue[i + 1];
   }

   (*queueCount)--;

   return message;
}

void submitMessage
(
   Message message,
   int *count,
   Message* queue
){
   queue[*count] = message;
   (*count)++;
}

void* receiveClock()
{

   int received[BUFFER_SIZE];
   MPI_Status status;
   Clock newClock = {{0, 0, 0}};
   Message message = {newClock, -1}; 
   
   while (1)
   {   
      pthread_mutex_lock(&receive_mutex);

      while (messageCountReceive == BUFFER_SIZE)
      {
         pthread_cond_wait(&receive_notFull, &receive_mutex);
      }

      MPI_Recv(received, BUFFER_SIZE, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);

      for (int i = 0; i < BUFFER_SIZE; i++) 
      {
         newClock.times[i] = received[i];
      }

      message.clock = newClock;
      message.adress = status.MPI_SOURCE;

      submitMessage(message, &messageCountReceive, messageReceiveQueue);
      pthread_mutex_unlock(&receive_mutex);

      pthread_cond_signal(&receive_notEmpty);

      return NULL;
   }
}

void* sendClock()
{
   Message newMessage;

   while (1)
   {

      pthread_mutex_lock(&send_mutex);
      while (messageCountSend == 0)
      {
         pthread_cond_wait(&send_notEmpty, &send_mutex);
      }
      
      newMessage = getMessage(messageSendQueue, &messageCountSend);

      MPI_Send(newMessage.clock.times, BUFFER_SIZE, MPI_INT, newMessage.adress, 0, MPI_COMM_WORLD);
      pthread_mutex_unlock(&send_mutex);
      pthread_cond_signal(&send_notFull);
   }
   return NULL;
}

void event
(){
   processClock.times[my_rank]++;
}

void toSendQueue(int to)
{
   pthread_mutex_lock(&send_mutex);
   processClock.times[my_rank]++;

   while (messageCountSend == BUFFER_SIZE){
      pthread_cond_wait(&send_notFull, &send_mutex);
   }

   Message newMessage = {processClock, to};
   submitMessage(newMessage, &messageCountSend, messageSendQueue);

   pthread_mutex_unlock(&send_mutex);
   pthread_cond_signal(&send_notEmpty);
}

void receiveFromQueue(int from)
{
   pthread_mutex_lock(&receive_mutex);
   processClock.times[my_rank]++;
   Message newMessage;

   do 
   {
      while (messageCountReceive == 0){
         pthread_cond_wait(&receive_notEmpty, &receive_mutex);
      }

      newMessage = getMessage(messageReceiveQueue, &messageCountReceive);
   }while (newMessage.adress != from);
   
   for (int i = 0; i < 3; i++)
   {
      processClock.times[i] = processClock.times[i] > newMessage.clock.times[i] ? processClock.times[i] : newMessage.clock.times[i];
   }
   
   pthread_mutex_unlock(&receive_mutex);
   pthread_cond_signal(&receive_notFull);
}