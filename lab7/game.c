#include <stdio.h>      // fprintf(), perror()
#include <stdlib.h>     // exit()
#include <string.h>  
#include <signal.h>     // signal() sigaction()
#include <unistd.h>    
#include <sys/types.h> 

#include <sys/shm.h>
#include <sys/ipc.h>

typedef struct {
    int guess;
    char result[8];
}data;

data* shm;
int shmid;
int guess_ans;

void sigint_handler(int signum)
{   
    //detach the shared memory segment
    shmdt(shm);
    //remove shared memory segment
    if (shmctl(shmid, IPC_RMID, NULL) < 0) {
        perror("shmctl failed");
        exit(1);
    }
    exit(0);
}

void sigusr1_handler(int signo, siginfo_t *info, void *context)
{   
    //get the guess from shared memory then compare it with the answer
    if(shm->guess > guess_ans)
    {
        strcpy(shm->result, "smaller");
        printf("[game] Guess %d, %s\n", shm->guess, "smaller");
    }
    else if(shm->guess < guess_ans)
    {   
        strcpy(shm->result, "bigger");
        printf("[game] Guess %d, %s\n", shm->guess, "bigger");
    }
    else   //correct guess, game finishes
    {
        strcpy(shm->result, "bingo");
        printf("[game] Guess %d, %s\n", shm->guess, "bingo");
    }
}


int main(int argc, char* argv[])
{   
    key_t key;
    if(argc != 3) 
    {
        fprintf(stderr, "Usage: ./game <key> <guess>");
        exit(EXIT_FAILURE);
    }
    //get the shm key and guess number
    key = atoi(argv[1]);
    guess_ans = atoi(argv[2]);

    //create shared memory(its size is sizeof(data))
    if ((shmid = shmget(key, sizeof(data), IPC_CREAT | 0666)) < 0) {
        perror("shmget");
        exit(1);
    }

    //attach the shared memory segment to our data space
    if ((shm = shmat(shmid, NULL, 0)) == (data *) -1) {
        perror("shmat");
        exit(1);
    }

    //initialize result to empty string
    memset(shm->result, 0, sizeof(shm->result));

    //show the pid of this process
    printf("[game] Game PID: %d\n",getpid());

    //when catching SIGINT signal (ctrl+c)
    signal(SIGINT, sigint_handler);

    struct sigaction my_action;
    /* register handler to SIGUSR1 */
    memset(&my_action, 0, sizeof (struct sigaction));
    my_action.sa_flags = SA_SIGINFO;
    my_action.sa_sigaction = sigusr1_handler;
    sigaction(SIGUSR1, &my_action, NULL);
    
    //infinite loop to wait for the game to finish
    while(1){}

    //detach the shared memory segment
    shmdt(shm);
    //remove shared memory segment
    if (shmctl(shmid, IPC_RMID, NULL) == -1) {
        perror("shmctl failed");
        exit(1);
    }
    return 0;
}