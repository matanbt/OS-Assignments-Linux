#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>

// ---------------------- DECLARATIONS AND GLOBAL VARIABLES ----------------------
#define BUF_SIZE 1024

void error_handler(int err, char* msg);

// ---------------------- HELPERS ----------------------
void error_handler(int err, char* msg)
{
    if (0 != err)
    {
        fprintf(stderr, "(Client) ERROR in: %s : %s\n", msg, strerror(errno));
        exit(1);
    }
}

// ---------------------- MAIN ----------------------
int main(int argc, char* argv[])
{
    // ------------- Process Args -------------
    error_handler(argc != 4, "Arguments Count");
    char* server_ip = argv[1];
    int server_port = atoi(argv[2]);
    error_handler(server_port == 0, "Processing Arg");
    char* file_path = argv[3];

    // ------------- Init -------------
    int ret_val;
    char* buf[BUF_SIZE];
    int fd = open(file_path, O_RDONLY);
    error_handler(fd == -1, "Opening File");


    struct sockaddr_in serv_addr;

    // ------------- Create Socket -------------
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    error_handler(sockfd < 0, "Creating Socket");

    // ------------- Connect to server -------------
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);
    serv_addr.sin_addr.s_addr = inet_addr(server_ip);

    ret_val = connect(sockfd, (struct sockaddr*)
            &serv_addr, sizeof(serv_addr));
    error_handler(ret_val < 0, "Connection Failed");


    // Calc file size
    struct stat st;
    stat(file_path, &st);
    uint32_t input_size = st.st_size;

    // -------------Write file-size to server -------------
    uint32_t net_input_size = htonl(input_size);
    int not_written = sizeof(net_input_size); // amount left to write
    int currently_sent; // amount read per iteration
    int total_sent = 0; // amount sent so far (total)
    while (not_written > 0)
    {
        currently_sent = write(sockfd, &net_input_size + total_sent, not_written);
        error_handler(currently_sent == -1, "Writing (size) to server");
        not_written -= currently_sent;
        total_sent += currently_sent;
    }

    // ------------- Read from file & Write to Server -------------
    int bytes_read;
    while ((bytes_read = read(fd, buf, BUF_SIZE)) > 0)
    {
        // Write the read-buffer to Server
        not_written = bytes_read;
        currently_sent = 0;
        total_sent = 0;
        while (not_written > 0)
        {
            currently_sent = write(sockfd, buf + total_sent, not_written);
            error_handler(currently_sent == -1, "Writing (file) to server");
            not_written -= currently_sent;
            total_sent += currently_sent;
        }
    }
    error_handler(bytes_read == -1, "Reading File");

    // ------------- Read counter from server -------------
    uint32_t pcc_count;
    int not_read = sizeof(int);
    int currently_read;
    int total_read = 0;
    while (not_read > 0)
    {
        currently_read = read(sockfd, &pcc_count + total_read, not_read);
        error_handler(currently_read <= 0, "Reading (counter) from server");
        not_read -= currently_read;
        total_read += currently_read;
    }
    pcc_count = ntohl(pcc_count);

    printf("# of printable characters: %u\n", pcc_count);

    // ------------- Epilogue -------------
    close(fd);
    close(sockfd);

    exit(0);
}


