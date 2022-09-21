#define SERVER_PORT       69

#define REQUEST           1
#define DATA              3
#define ACK               4
#define ERROR             5

#define MAX_FILE_NAME     255
#define MAX_PATH_LEN      32000

#define BUFFER_SIZE       1024

#define RRQ_PACKET_DIM    265
#define MAX_DIM_BLOCK     512
#define DATA_PACKET_DIM   516
#define ACK_PACKET_DIM    4

#define FILE_NOT_FOUND    1
#define ILLEGAL_TFTP_OP   2


int send_error_packet(int sd, char* buffer, struct sockaddr_in connecting_addr,
                       uint16_t error_n);

int check_port(char* port);
