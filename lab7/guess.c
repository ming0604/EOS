#include <stdio.h>      // fprintf(), perror()
#include <stdlib.h>     // exit()
#include <string.h>
#include <stdbool.h>    // true false     
#include <signal.h>     // signal() sigaction()
#include <unistd.h>    
#include <sys/types.h> 

#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/time.h>

typedef struct {
    int guess;
    char result[8];
}data;

data* shm;
int shmid;
int lower_bound, upper_bound;
pid_t game_pid;
bool game_finish = false;

int binary_search_guess(int lower_bound,int upper_bound)
{   
    //use binary search to guess
    int mid;
    mid = (lower_bound + upper_bound) / 2;
    //write the guess number into shared memory
    shm->guess = mid;
    printf("[game] Guess: %d\n",shm->guess);
    //send SIGUSR1 to game process
    if(kill(game_pid, SIGUSR1) == -1) 
    {
        perror("kill failed");
        exit(EXIT_FAILURE);
    }
}

void timer_handler(int signum)
{   
    //use binary search to guess
    if(strcmp(shm->result, "smaller") == 0)
    {   
        upper_bound = shm->guess - 1; 
        binary_search_guess(lower_bound,upper_bound);
    }
    else if(strcmp(shm->result, "bigger") == 0)
    {
        lower_bound = shm->guess + 1;
        binary_search_guess(lower_bound,upper_bound);
    }
    else if(strcmp(shm->result, "bingo") == 0)      //bingo, game finishes
    {
        game_finish = true;
    }
    else  //first guess
    {
        binary_search_guess(lower_bound,upper_bound);
    }
}

int main(int argc, char* argv[])
{
    key_t key;

    if(argc != 4) 
    {
        fprintf(stderr, "Usage: ./guess <key> <upper_bound> <pid>");
        exit(EXIT_FAILURE);
    }
    //get the shm key, upper bound of the guess number and the game process's pid
    key = atoi(argv[1]);
    upper_bound = atoi(argv[2]);
    game_pid = atoi(argv[3]);
    //initialize lower bound as 1
    lower_bound = 1;

    //Locate the shm segment(its size is sizeof(data))
    if ((shmid = shmget(key, sizeof(data), 0666)) < 0) {
        perror("shmget");
        exit(1);
    }

    /* Now we attach the segment to our data space */
    if ((shm = shmat(shmid, NULL, 0)) == (data *) -1) {
        perror("shmat");
        exit(1);
    }

    struct sigaction sa;
    struct itimerval timer;
    /* Install timer_handler as the signal handler for SIGVTALRM */
    memset (&sa, 0, sizeof (sa));
    sa.sa_handler = timer_handler;
    sigaction (SIGVTALRM, &sa, NULL);
    /* Configure the timer to expire after 250 msec */
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 250000;
    /* Reset the timer back to 1 sec after expired */
    timer.it_interval.tv_sec = 1;
    timer.it_interval.tv_usec = 0;
    /* Start a virtual timer */
    setitimer (ITIMER_VIRTUAL, &timer, NULL);

    //infinite loop to wait for the game to finish
    while(!game_finish){}
    //detach the shared memory segment
    shmdt(shm);

    return 0;
}