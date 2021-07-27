#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>


// ---------------------- DECLARATIONS AND GLOBAL VARIABLES ----------------------
#define MIN_PRINTABLE_CHAR 32
#define MAX_PRINTABLE_CHAR 126

// Global PCC counter
uint32_t pcc_total[MAX_PRINTABLE_CHAR + 1];

int signal_raised_flag = 0;

// Raised when the connection to 'connfd' fails (in some sys-call)
int conn_broke_flag = 0;

/*
 * Handler will raise 'signal_raised_flag', so we know that
 * the signal was sent (useful for a code block that should be done without interrupting)
 */
void raise_flag_sig_handler(int signum);

/*
 * Handler will close the server and perform a clean exit
 */
void clean_exit_sig_handler(int signum);

/*
 * If given err is non-zero prints error-message and exits
 */
void error_handler(int err, char* msg);

/*
 * Handles connection error, won't exit in *connection* error.
 */
void conn_error_handler(int err, char* msg);

/*
 * Returns 1 IFF c is a printable char, corresponds to our definition
 */
int printable(int c);

/*
 * Adds a PCC-session-count to the Global PCC counter
 */
void add_pcc_to_total(uint32_t* pcc_session);

// ---------------------- SIGNAL HANDLERS ----------------------

void raise_flag_sig_handler(int signum)
{
    signal_raised_flag = 1;
}


void clean_exit_sig_handler(int signum)
{
    for (int i = MIN_PRINTABLE_CHAR; i < MAX_PRINTABLE_CHAR + 1; i++)
    {
        printf("char '%c' : %u times\n", i, pcc_total[i]);
    }
    exit(0);
}

// ---------------------- HELPERS ----------------------

void error_handler(int err, char* msg)
{
    if (0 != err)
    {
        fprintf(stderr, "(Server) ERROR in: %s : %s\n", msg, strerror(errno));
        exit(1);
    }
}


void conn_error_handler(int err, char* msg)
{
    if (0 != err)
    {
        if (errno == ETIMEDOUT || errno == ECONNRESET || errno == EPIPE)
        { // TCP Errors
            fprintf(stderr, "(Server) CONNECTION ERROR in: %s : %s\n",
                    msg, strerror(errno));
            conn_broke_flag = 1;
        } else
        {
            error_handler(err, msg);
        }
    }
}


int printable(int c)
{
    return (MIN_PRINTABLE_CHAR <= c && c <= MAX_PRINTABLE_CHAR);
}


void add_pcc_to_total(uint32_t* pcc_session)
{
    for (int i = MIN_PRINTABLE_CHAR; i < MAX_PRINTABLE_CHAR + 1; i++)
    {
        pcc_total[i] += pcc_session[i];
    }
}

// ---------------------- MAIN ----------------------
int main(int argc, char* argv[])
{
    // ------------- Process args -------------
    error_handler(argc != 2, "Arguments Count");
    int port = atoi(argv[1]);
    error_handler(port == 0, "Processing Arg");

    // ------------- Init -------------
    int ret_val;
    memset(pcc_total, 0, (MAX_PRINTABLE_CHAR + 1) * sizeof(*pcc_total));
    uint32_t pcc_session[MAX_PRINTABLE_CHAR + 1];
    int listenfd, connfd;
    uint32_t pcc_session_count = 0;
    struct sockaddr_in serv_addr;
    socklen_t addrsize = sizeof(struct sockaddr_in);

    // ------------- Define sigactions structs -------------
    struct sigaction clean_exit_action;
    memset(&clean_exit_action, 0, sizeof(clean_exit_action));
    clean_exit_action.sa_handler = clean_exit_sig_handler;
    clean_exit_action.sa_flags = SA_RESTART;
    struct sigaction raise_flag_action;
    memset(&raise_flag_action, 0, sizeof(raise_flag_action));
    raise_flag_action.sa_flags = SA_RESTART;
    raise_flag_action.sa_handler = raise_flag_sig_handler;

    // ------------- Sets the current handler of SIGUSR1 -------------
    ret_val = sigaction(SIGUSR1, &clean_exit_action, NULL);
    error_handler(0 != ret_val, "Setting Signal Handler");

    // ------------- Create socket, and listen to its -------------
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    error_handler(-1 == listenfd, "Creating (listen) Socket");

    memset(&serv_addr, 0, addrsize);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);
    error_handler(bind(listenfd, (struct sockaddr*) &serv_addr, addrsize),
                  "Bind Failed");

    error_handler(listen(listenfd, 10), "Listen Failed");

    // Allow port fast reuse
    // https://stackoverflow.com/questions/24194961/how-do-i-use-setsockoptso-reuseaddr/25193462
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int));

    // ------------- Connections Loop -------------
    while (1)
    {
        // ------- Accept a connection -------
        connfd = accept(listenfd, NULL, NULL);
        error_handler(connfd < 0, "Accept Failed");

        pcc_session_count = 0;
        memset(pcc_session, 0, (MAX_PRINTABLE_CHAR + 1) * sizeof(*pcc_session));
        conn_broke_flag = 0;

        // Change exit-handler, so it wouldn't exit during the following section
        ret_val = sigaction(SIGUSR1, &raise_flag_action, NULL);
        error_handler(0 != ret_val, "Changing Signal Handler");

        /*
         * Implementation Note: In all the read() calls I consider return value 0
         * to be an error. That is because:
         * bytes_read == -1 --> error occurred
         * bytes_read ==  0 --> EOF, but also we know (bytes_read_total < bytes_should_be_read) holds (loop condition),
         *                      so it MUST be error.
         */

        // ------- Read input-file size from client -------
        uint32_t input_size;
        int not_read = sizeof(input_size);
        int currently_read;
        int total_read = 0;
        while (conn_broke_flag != 1 && not_read > 0)
        {
            currently_read = read(connfd, &input_size + total_read, not_read);
            conn_error_handler(currently_read <= 0, "Reading (size) from client");
            not_read -= currently_read;
            total_read += currently_read;
        }
        input_size = ntohl(input_size);

        // ------- Read data from client char by char -------
        int bytes_read;
        int bytes_read_total = 0;
        while (conn_broke_flag != 1 && bytes_read_total < input_size)
        {
            char curr_ch;
            bytes_read = read(connfd, &curr_ch, sizeof(curr_ch));
            error_handler(bytes_read <= 0, "Reading from client");
            bytes_read_total++;
            if (printable(curr_ch))
            { // char is printable
                pcc_session[(int) (curr_ch)] += 1;
                pcc_session_count += 1;
            }
        }

        // ------ Write to client the pcc count ------
        pcc_session_count = htonl(pcc_session_count);
        int not_written = sizeof(int);
        int currently_sent;
        int total_sent = 0;
        while (not_written > 0 && conn_broke_flag != 1)
        { // Runs as long as there are bytes-to-write the connection is well
            currently_sent = write(connfd, &pcc_session_count + total_sent, sizeof(int));
            conn_error_handler(currently_sent == -1, "Writing to client");
            not_written -= currently_sent;
            total_sent += currently_sent;
        }

        // Close socket
        close(connfd);

        // Connection was not terminated during last session --> updates PCC
        if (conn_broke_flag == 0)
        {
            add_pcc_to_total(pcc_session);
        }
        // Signal was raised during the connection
        if (signal_raised_flag == 1)
        {
            clean_exit_sig_handler(0);
        }
        // Change exit-handler to regular
        ret_val = sigaction(SIGUSR1, &clean_exit_action, NULL);
        error_handler(0 != ret_val, "Changing Signal Handler");
    }


}
