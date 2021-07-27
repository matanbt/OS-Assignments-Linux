#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BUFF_SIZE 1

/*
 * #################### Declarations and Documentation ##################
 */

/*
 * Sets `signal_type`'s handler to be the default
 */
int reset_signal(int);

/*
 * Sets `signal_type`'s handling to be the ignoring
 */
int ignore_signal(int);


/*
 * Helpers for 'process_arglist' executions
 * Handles each case of special commands in the shell
 */
int run_foreground(char**);
int run_background(int, char**);
int run_piping(char**, int);
int run_redirect_output(char**, int);

/*
 * A wrapper for execvp, to be run by new child processes
 */
void execute_args(char ** arglist);

/*
 * #################### Implementation ##################
 * #################### MAIN FUNCTIONS ##################
*/
/*
 * Initializtion of the shell
 * returns 0 on success, -1 on error
 */
int prepare()
{
    // defines shell-process to ignore `CTRL+C`
    if (ignore_signal(SIGINT) == -1)
    {
        return -1;
    }

    // Prevents terminated children of becoming zombies
    // SOURCE: Section 2 in https://www.geeksforgeeks.org/zombie-processes-prevention/
    if (ignore_signal(SIGCHLD) == -1)
    {
        return -1;
    }
    return 0;
}

/*
 * Given array of strings 'arglist' with 'count' parameters
 * Executes the shell-command it represents
 * Returns 1 on success, 0 on error
 */
int process_arglist(int count, char ** arglist)
{
    int special_char = 0;
    int special_index = -1;
    // Iterate on the args,  looks for *the* special character
    for (int i = 0; i < count; i++)
    {
        if (strlen(arglist[i]) == 1 &&
            (*arglist[i] == '|' || *arglist[i] == '&' || *arglist[i] == '>'))
        {
            // found a special char, breaks loop
            special_char = *arglist[i];
            special_index = i;
            break;
        }
    }

    // Handles each case separately
    switch (special_char)
    {
        case 0:  // foreground (no special key)
            return run_foreground(arglist);
            break;

        case '&': // background
            return run_background(count, arglist);
            break;

        case '|': // pipe
            return run_piping(arglist, special_index);
            break;

        case '>': // redirect output
            return run_redirect_output(arglist, special_index);
            break;
    }
    return 1;
}

int finalize()
{
    return 0;
}


/*
#################### BEHAVIORS AND HELPERS ##################
*/

int run_foreground(char** arglist)
{
    int pid = fork();
    if (pid == -1)
    {
        perror("Forking Failed");
        return 0;
    }
    else if (pid == 0)
    { // child
        // Foreground process will have default behavior of SIGINT
        if(-1 == reset_signal(SIGINT))
        {
            exit(1);
        }
        execute_args(arglist);
    }

    // Parent
    // Waits foreground process to finish
    int status;
    if (-1 == waitpid(pid, &status, 0))
    {
        if(errno != EINTR && errno != ECHILD)
        {
            // Ignores ECHILD errors, because SIGCHLD was set to be ignored and
            // it's likely to cause this error, as specified in ERRORS --> ECHILD
            // IN: https://linux.die.net/man/2/waitpid
            perror("ERROR Waiting for child");
            return 0;
        }
    }
    return 1;
}

int run_background(int count, char** arglist)
{
    int pid = fork();
    if (-1 == pid)
    {
        perror("Forking Failed");
        return 0;
    }
    else if (pid == 0)
    { // Child
        arglist[count - 1] = NULL;
        // Background-processes are set to ignore SIGINT
        if (-1 == ignore_signal(SIGINT))
        {
            exit(1);
        }
        execute_args(arglist);
    }
    // Parent
    return 1;
}

int run_piping(char** arglist, int pipe_key_index)
{
    arglist[pipe_key_index] = NULL;
    int fds[2];
    if (-1 == pipe(fds))
    {
        perror("Pipe Creation Failed");
        return 0;
    }
    int readerfd = fds[0];
    int writerfd = fds[1];
    int writer_pid = fork();
    if (-1 == writer_pid)
    {
        perror("Forking Failed");
        return 0;
    }
    else if (0 == writer_pid)
    { // (Child) Writer process:
        if(-1 == reset_signal(SIGINT))
        {
            return 0;
        }
        // Output will now be written to writerfd (= writer end of our pipe)
        // STDOUT-file will silently be closed by dup2 (as written in doc)
        dup2(writerfd, STDOUT_FILENO);
        close(writerfd);
        close(readerfd);
        execute_args(arglist);
    }
    int reader_pid = fork();
    if (-1 == reader_pid)
    {
        perror("Forking Failed");
        return 0;
    }
    else if (reader_pid == 0)
    { //(Child) Reader process:
        if(-1 == reset_signal(SIGINT))
        {
            return 0;
        }
        // STDIN will now be written from readerfd (= reader end of our pipe)
        dup2(readerfd, STDIN_FILENO);
        close(readerfd);
        close(writerfd);
        execute_args(arglist + pipe_key_index + 1);
    }
    // Parent:
    close(readerfd);
    close(writerfd);
    // Waits for both process to finish
    int status, wait1, wait2;
    wait1 = waitpid(writer_pid, &status, 0);
    wait2 = waitpid(reader_pid, &status, 0);
    if (-1 == wait1 || -1 == wait2)
    {
        if(errno != EINTR && errno != ECHILD)
        {
            perror("ERROR Waiting for children");
            return 0;
        }
    }
    return 1;
}

int run_redirect_output(char** arglist, int arrow_key_index)
{
    arglist[arrow_key_index] = NULL;
    char * output_file = arglist[arrow_key_index + 1];
    int pid = fork();
    if (-1 == pid)
    {
        perror("Forking Failed");
        return 0;
    }
    if (pid == 0)
    { // (Child) Writer process
        if(-1 == reset_signal(SIGINT))
        {
            return 0;
        }
        // Flags taken from code example named 'Opening a File for Writing'
        // IN: 'https://man7.org/linux/man-pages/man3/open.3p.html
        int out_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (-1 == out_fd)
        {
            perror("ERROR opening output file (>)");
            exit(1);
        }
        // dup2 - Silently closes STDOUT_FILENO, and now STDOUT will point to 'output_file'
        if (-1 == dup2(out_fd, STDOUT_FILENO))
        {
            perror("ERROR duplication output (>)");
            exit(1);
        }
        close(out_fd);
        execute_args(arglist);
    }
    // Parent:
    // Waits for process to end
    int status;
    if (-1 == waitpid(pid, &status, 0))
    {
        if(errno != EINTR && errno != ECHILD)
        {
            perror("ERROR Waiting");
            return 0;
        }
    }
    return 1;
}

int reset_signal(int signal_type)
{
    struct sigaction new_action;
    memset(&new_action, 0, sizeof(new_action));
    // Sets handler to be the default handler of the signal
    new_action.sa_handler = SIG_DFL;
    new_action.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (0 != sigaction(signal_type, &new_action, NULL))
    {
        perror("ERROR: Handler (SIG_DFL) registration failed");
        return -1;
    }
    return 1;
}


int ignore_signal(int signal_type)
{
    struct sigaction new_action;
    memset(&new_action, 0, sizeof(new_action));
    // Sets handler to ignore the signal when being sent
    new_action.sa_handler = SIG_IGN;
    new_action.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (0 != sigaction(signal_type, &new_action, NULL))
    {
        perror("ERROR: Handler (SIG_IGN) registration failed");
        return -1;
    }
    return 1;

}

void execute_args(char ** arglist)
{
    execvp(arglist[0], arglist);
    // If process is still here --> execvp failed:
    perror("Error executing command");
    exit(1);
}


