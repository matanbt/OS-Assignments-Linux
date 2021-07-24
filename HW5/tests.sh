kill -SIGUSR1 10907

gcc pcc_server.c -o server
gcc pcc_client.c -o client

gcc -O3 -D_POSIX_C_SOURCE=200809 -Wall -std=c11 pcc_server.c -o server
gcc -O3 -D_POSIX_C_SOURCE=200809 -Wall -std=c11 pcc_client.c -o client


./server 1500 &
./client 127.0.0.1 1500 ./testi.txt

kill -SIGUSR1 777