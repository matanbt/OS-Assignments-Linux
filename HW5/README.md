# Count Printable Characters - Network Programming and Sockets

## Description
Implementation of basic TCP client-server communication, using linux's sockets API. 
While the client sends the server a stream of bytes, the server counts the amount of
printable characters (i.e. letters) and sends back the counter.
  
## Files
* **pcc_server.c:** Sets up a tcp-server, according to the given port (as an argument). Accepts a tcp connection, 
  and counts the amount of printable characters it receives and sends it to the client.
* **pcc_client.c:**  Creates a tcp-connection according to given server's IP and port, and sends the server the content of the user-supplied file.
  Then, reads and outputs the amount of PC as sent from the server.
