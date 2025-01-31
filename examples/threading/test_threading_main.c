/**
 * Small test bed main to test the homework in an ad-hoc manner.
 * Not part of the assignment, but helped me work through the pthread
 * interface.
 * gcc ./threading.c ./test_threading_main.c -lpthread && ./a.out
 */

#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#include "./threading.h"


int main(int argc, char* argv[]) {
    printf("Setting up thread 1\n");
    printf("Start a thread obtaining a locked mutex, sleeping 1 millisecond before locking and waiting to return\n");
    printf("until 1 millisecond after locking.\n");
    pthread_t thread1;
    pthread_mutex_t mutex1;
    int sleep_before_lock = 10000;
    int sleep_after_lock = 10000;
    bool thread_started = start_thread_obtaining_mutex(&thread1, &mutex1, sleep_before_lock,
                                                                    sleep_after_lock);

    printf("joining thread...\n");
    int join_result = pthread_join(thread1, NULL);
    if (join_result == 0) {
        printf("thread joined!\n");
    }

    return 0;
}
