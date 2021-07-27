#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>


//============= Macros ====================
#define SUCCESS 0 // must be 0, as defined by linux functions
#define MAIN_THREAD_ERR_EXIT_CODE 1 // exit code from the main thread in case of main-thread-errors

//============= Global Variables ====================

pthread_mutex_t qLock;
// Condition Variables: (q for Queue, f for Flag)
// Signals the queue is not empty
pthread_cond_t qNotEmpty;
// Signals all the threads were created (will be trigger all thread to start searching)
pthread_cond_t fAllThreadsCreated;
// Signals a creation of a thread
pthread_cond_t fThreadWasCreated;

// Search term received as argument
char* search_term;

// Amount of searching threads created
int threads_count;

// Amount of threads that are currently active (= didn't exit for an error / finished)
int active_count = 0;
// Amount of threads that are currently waiting (i.e. waiting for dequeue)
int waiting_count = 0;
// Amount of files matching the search term found on the run
int found_count = 0;
// All 'waiting_count' and 'active_count' and 'found_count' should be handled when 'qLock' acquired (!)

// 1 IFF some searching-thread has encountered an error while running
int thread_encountered_err_flag = 0;

// Struct that represents a queue element (=node)
typedef struct node_st
{
    char* path;
    struct node_st* prev;
} node_t;

// Head and Tail of our queue. [head is NULL] IFF [tail is NULL] IFF [queue is empty]
node_t* head = NULL;
node_t* tail = NULL;

//=========== Functions Declarations =======================
/*
 * --- Search Queue ---
 * Function to be run by the search-threads.
 * Dequeues a path from the queue as long as the queue is not completely exhausted.
 * Compares each file to the 'search_term' and counts the matches.
 */
_Noreturn void* search_queue(void*);
/*
 *  --- ENQUEUE ---
 * Gets a string 'path', and creates a node with its
 * Pushes the new node to the end of the queue.
 * returns some ERRNO on errors, otherwise SUCCESS
 * * Memory pointed by 'path'  argument was allocated by caller, and will be freed only when dequeued
 */
int enqueue(char*);

/*
 * --- DEQUEUE ---
 * Dequeues node from the queue when possible, and returns its value ('path')
 * if queue is empty and no threads is expected to enqueue, it kills all threads.
 * Freeing the returned pointer is *caller* responsibility
 */
char* dequeue();
/*
 * return SUCCESS IFF given dir-path is searchable
 */
int is_searchable(char*);
/*
 * --- Error Handling ---
 * @param err_cond: should be some defined err_no, or just a boolean condition that
 *                  hold IFF error has occurred.
 * @return: if (err_cond is non-zero) prints 'msg' to stderr with its strerror
 *          performs exit, with exit code 1
 */
void error_handler_main(int, char*);

/*
 * Same as 'error_handler', but performs *thread* exit instead
 */
void error_handler_search_thread(int, char*);

//============ Threads Search and Enqueueing Main Function =======================
_Noreturn void* search_queue(void* arg)
{
    int ret_val;

    // Waits for all threads to be created:
    pthread_mutex_lock(&qLock);
    active_count++;
    pthread_cond_signal(&fThreadWasCreated);
    pthread_cond_wait(&fAllThreadsCreated, &qLock);
    pthread_mutex_unlock(&qLock);

    while(1)
    {
        char* curr_path = dequeue();
        error_handler_search_thread(NULL == curr_path, "Got NULL pointer from dequeue()");
        // Creates a pointer to the directory entries stream
        DIR* dirp = opendir(curr_path);
        // error in opendir() shouldn't happen, as far as permissions
        error_handler_search_thread(NULL == dirp, "opendir()");
        struct dirent* entry;

        // Iterates on each entry of the current directory
        while(NULL != (entry = readdir(dirp)))
        {
            char* entry_name = entry->d_name;

            // CASE IGNORE : current entry is "." / ".." / symlink, so skips it
            if(strcmp(".", entry_name) == 0 || strcmp("..", entry_name) == 0)
            {
                continue;
            }

            // prepares to check it's DIR and it's executable and readable by owner
            struct stat sb;
            char* new_path = (char*) malloc(PATH_MAX * sizeof(*new_path));
            error_handler_search_thread(new_path == NULL, "malloc failed");
            strcpy(new_path, curr_path);
            strcat(new_path, "/");
            strcat(new_path, entry_name);
            ret_val = lstat(new_path, &sb);
            error_handler_search_thread(ret_val, "lstat()");

            // CASE ENQUEUE : entry is a directory
            if (S_ISDIR(sb.st_mode))
            {
                // checks if new_path is searchable directory
                if (is_searchable(new_path) == SUCCESS)
                {
                    ret_val = enqueue(new_path);
                    error_handler_search_thread(ret_val, "enqueue()");
                }
                else
                { // not searchable
                    printf("Directory %s: Permission denied.\n", new_path);
                    free(new_path);
                }
            }

            // CASE SEARCH: entry is a file of any other type
            else
            {
                free(new_path);
                char* ret_pointer = strstr(entry_name, search_term);
                if (NULL != ret_pointer)
                {
                    printf("%s/%s\n", curr_path, entry_name);
                    pthread_mutex_lock(&qLock);
                    found_count++;
                    pthread_mutex_unlock(&qLock);
                }
            }

        }
        free(curr_path);
        closedir(dirp);
    }

}

//=================== QUEUE FUNCTIONS ====================

int enqueue(char* path)
{
    node_t* new_node = (node_t*) malloc(sizeof(*new_node));
    if (NULL == new_node)
    {
        return ENOMEM;
    }
    new_node->path = path;
    new_node->prev = NULL;
    pthread_mutex_lock(&qLock);
    if (head == NULL)
    {
        // Queue is empty, adds node as new head & tail
        head = new_node;

    }
    else
    {
        // Queue is not empty, so performs regular enqueuing
        tail->prev = new_node;
    }
    tail = new_node;

    pthread_cond_broadcast(&qNotEmpty);
    // Why broadcast and not signal? : In order to prevent a lost-wakeup (as seen in lect. 10)
    pthread_mutex_unlock(&qLock);

    return SUCCESS;
}

char* dequeue()
{
    pthread_mutex_lock(&qLock);
    waiting_count++;
    while (NULL == head)
    {
        // the queue is empty AND all the threads are waiting
        if (waiting_count == active_count)
        {
            // Prepares the thread for an exit
            active_count--;
            waiting_count--;
            pthread_mutex_unlock(&qLock);
            // Will trigger all the waiting-threads to exit themselves
            pthread_cond_broadcast(&qNotEmpty);
            pthread_exit(NULL);
        }
        pthread_cond_wait(&qNotEmpty, &qLock);
    }
    waiting_count--;

    char* path_dequeued = head->path;
    if (tail == head)
    { // Queue has one node, and head & tail points at its
        tail = NULL;

    }
    node_t* deleted_node = head;
    head = head->prev;
    pthread_mutex_unlock(&qLock);

    free(deleted_node);
    // path_dequeued's memory will be freed by caller, as documented
    return path_dequeued;
}

// ================ HELPERS ================================
int is_searchable(char* dir_path)
{
    if ((access(dir_path, R_OK) == 0) && (access(dir_path, X_OK) == 0))
    {
        return SUCCESS;
    }
    return -1;
}
//================= Error Handlers =========================

void error_handler_main(int err_cond, char* msg)
{
    if (err_cond != SUCCESS)
    {
        if(err_cond == -1)
        {
            err_cond = errno;
        }
        fprintf(stderr, "ERROR in: %s : %s\n", msg, strerror(err_cond));
        exit(MAIN_THREAD_ERR_EXIT_CODE);
    }
}

void error_handler_search_thread(int err_cond, char* msg)
{

    if (err_cond != SUCCESS)
    {
        if(err_cond == -1)
        {
            err_cond = errno;
        }

        fprintf(stderr, "ERROR in: %s : %s\n", msg, strerror(err_cond));
        pthread_mutex_lock(&qLock);
        // current thread has encountered error, so it marks the flag
        thread_encountered_err_flag = 1;
        // decrements active_count, current thread is being exited
        active_count--;
        pthread_mutex_unlock(&qLock);
        pthread_exit(NULL);
    }
}

//======================= MAIN =======================
int main(int argc, char* argv[])
{
    pthread_t* threads;
    int ret_val;

    // --- Process args -----------------------------------------
    error_handler_main(argc != 4, "arguments count");
    char* search_path = (char*) malloc(PATH_MAX * sizeof(*search_path));
    error_handler_main(search_path == NULL, "'search_path' malloc failed");
    strcpy(search_path,argv[1]);
    search_term = argv[2];
    threads_count = atoi(argv[3]);
    error_handler_main(threads_count <= 0, "argument thread-count");


    // --- Alloc Threads -----------------------------------------------
    threads = (pthread_t*) malloc(threads_count * sizeof(*threads));
    error_handler_main(NULL == threads, "malloc failed");

    // --- Initialize Mutex Lock & Conditions---------------------------
    ret_val = pthread_mutex_init(&qLock, NULL);
    error_handler_main(ret_val, "pthread_mutex_init()");
    ret_val = pthread_cond_init(&qNotEmpty, NULL);
    error_handler_main(ret_val, "pthread_cond_init() - qNotEmpty");
    ret_val = pthread_cond_init(&fThreadWasCreated, NULL);
    error_handler_main(ret_val, "pthread_cond_init() - fThreadWasCreated");
    ret_val = pthread_cond_init(&fAllThreadsCreated, NULL);
    error_handler_main(ret_val, "pthread_cond_init() - fAllThreadsCreated");

    // --- Create Queue ---------------------------
    // Creates queue if the path is searchable (otherwise queue remains empty)
    struct stat sb;
    ret_val = lstat(search_path, &sb);
    error_handler_main(ret_val, "lstat()");
    if (is_searchable(search_path) == SUCCESS)
    {
        ret_val = enqueue(search_path);
        error_handler_main(ret_val, "Enqueuing initial path");
    }
    else
    { // given dir is not searchable
        printf("Directory %s: Permission denied.\n", search_path);
        // Due to ambiguity in the instructions, I chose to consider this case as an error
        exit(MAIN_THREAD_ERR_EXIT_CODE);
    }

    // --- Launch threads ------------------------------
    for (long t = 0; t < threads_count; ++t)
    {
        ret_val = pthread_create(&threads[t], NULL, search_queue, NULL);
        error_handler_main(ret_val, "pthread_create()");
    }
    pthread_mutex_lock(&qLock);
    while(active_count < threads_count)
    {
        // wakes up everytime a thread is created (it's the only listener for this signal)
        pthread_cond_wait(&fThreadWasCreated, &qLock);
    }
    // will exit loop only when (active_count == threads_count),
    // which implies all threads have been created
    pthread_mutex_unlock(&qLock);
    pthread_cond_broadcast(&fAllThreadsCreated);

    // --- Wait for threads to finish ------------------
    for (long t = 0; t < threads_count; ++t)
    {
        ret_val = pthread_join(threads[t], NULL);
        error_handler_main(ret_val, "pthread_join()");
    }

    // ---  Epilogue -----------------------------------
    free(threads);
    pthread_mutex_destroy(&qLock);

    printf("Done searching, found %d files\n", found_count);

    if(thread_encountered_err_flag)
    {
        // finished BUT encountered an error along the way
        exit(MAIN_THREAD_ERR_EXIT_CODE);
    }
    exit(SUCCESS);
}