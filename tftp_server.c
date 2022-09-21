#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include "tftp_server.h"

                /*  ------  funzioni di utilità  -------  */

int send_error_packet(int sd, char* buffer, struct sockaddr_in connecting_addr,
                       uint16_t error_n) {
  int ret, pos = 0;
  char error_msg[100];

  uint16_t opcode = htons(ERROR);
  uint16_t error_number = error_n;

  if (error_number == FILE_NOT_FOUND)
    strcpy(error_msg, "Il file richiesto non è presente sul server.\0");
  else if (error_number == ILLEGAL_TFTP_OP)
    strcpy(error_msg, "L'operazione richiesta non è valida.\0");

  error_number = htons(error_n);
  memset(buffer, 0, BUFFER_SIZE);

  memcpy(buffer+pos, &opcode, sizeof(opcode));
  pos += sizeof(opcode);

  memcpy(buffer+pos, &error_number, sizeof(error_number));
  pos += sizeof(error_number);

  printf("%s\n",error_msg);
  strcpy(buffer+pos, error_msg);
  pos += strlen(error_msg) + 1;

  ret = sendto(sd, buffer, sizeof(buffer), 0,
              (struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
  if (ret < 0)
    return 0;
  return 1;
}

int check_port(char* port) {
  int p;
  p = atoi(port);
  if (p < 1 || p > 65535)
    return -1;
  else
    return p;
}

                      /*  -------    main    -------  */

int main(int argc, char* argv[]) {
  int ret, sd, new_sd, port, blocks, to_read, i;
  int pos = 0;
  uint16_t opcode, block_number, received_block_num;
  char file_name[MAX_FILE_NAME];
  char dir_path[MAX_PATH_LEN], data_block[MAX_DIM_BLOCK];
  char mode[30], c;
  struct sockaddr_in my_addr, srv_data_addr, connecting_addr;
  int addrlen = sizeof(connecting_addr);
  char buffer[BUFFER_SIZE], *temp_buf;
  struct stat info;
  pid_t pid;
  FILE *fd;


  /* Controllo parametri */
  if (argc != 3 || check_port(argv[1]) == -1) {
    printf("Errore: formato non corretto\nUsare il formato: tftp_server <porta> <directory files>\n");
    exit(-1);
  }

  //  Estraggo numero di porta server e directory di riferimento  //
  port = atoi(argv[1]);
  strcpy(dir_path, argv[2]);

  /* Creazione socket */
  printf("Creazione socket... ");
  sd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sd < 0) {
    perror("Errore durante la creazione del socket");
    exit(-1);
  } else {
    printf(" OK!\n");
  }

  /* Associo socket all'indirizzo del server */
  printf("Creazione indirizzo di bind... ");
  memset(&my_addr, 0, sizeof(my_addr));   // Pulizia
  my_addr.sin_family = AF_INET;
  my_addr.sin_port = htons(port);
  my_addr.sin_addr.s_addr = INADDR_ANY;
  ret = bind(sd, (struct sockaddr*)&my_addr, sizeof(my_addr));
  if (ret < 0) {
    perror("bind()");
    exit(-1);
  } else {
    printf("OK!\nServer in ascolto sul socket %d.\n",sd);
  }

  while (1) {
    strcpy(dir_path, argv[2]);

    memset(file_name, 0, MAX_FILE_NAME);
    memset(buffer, 0, BUFFER_SIZE);
    memset(mode, 0, 30);
    memset(&opcode, 0, sizeof(opcode));

    //      attesa della richiesta       //
    do{
      ret = recvfrom(sd, buffer, BUFFER_SIZE, 0,
                    (struct sockaddr*)&connecting_addr, (socklen_t*)&addrlen);
      if (ret < 0)
        sleep(10);
    } while (ret < 0);

    printf("\nRichiesta ricevuta da %s (porta: %u)\n",
           inet_ntoa(connecting_addr.sin_addr), ntohs(connecting_addr.sin_port));


    //    inizio deserializzazione messaggio di richiesta   //
    pos = 0;

    memcpy(&opcode, buffer+pos, sizeof(opcode));
    pos += sizeof(opcode);
    opcode = ntohs(opcode);

    if (opcode != REQUEST) {
      perror("Errore");
      //    invio error packet  ---  ILLEGAL_TFTP_OP    //
      ret = send_error_packet(sd, buffer, connecting_addr, ILLEGAL_TFTP_OP);
      if (!ret) {
        perror("Errore");
        exit(-1);
      } continue;
    }

    strcpy(file_name, buffer+pos);
    pos += strlen(file_name)+1;

    strcpy(mode, buffer+pos);
    pos += strlen(mode)+1;
    //    fine deserializzazione messaggio di richiesta   //

    //   controllo presenza file richiesto    //
    if (strcmp(&dir_path[strlen(dir_path)-1], "/") != 0 &&
        file_name[0] != '/')
      strcat(dir_path, "/");

    strcat(dir_path, file_name);

    printf("Il percorso del file da aprire è: %s\n", dir_path);

    if (strcmp(mode, "bin") == 0)
      fd = fopen(dir_path, "rb");
    else if (strcmp(mode, "txt") == 0)
      fd = fopen(dir_path, "r");

    if (!fd) {      // file NON presente
      perror("Errore");
      //    invio error packet  ---  FILE NOT FOUND    //
      ret = send_error_packet(sd, buffer, connecting_addr, FILE_NOT_FOUND);
      if (!ret) {
        perror("Errore");
        exit(-1);
      } continue;
    } else {        // file presente
      stat(dir_path, &info);
      printf("Dimensione del file: %lld byte.\n", (long long int)info.st_size);
    }

    //    calcolo numero di blocchi da inviare    //
    blocks = ((int)info.st_size / 512);
    if (((int)info.st_size % 512) >= 0)                                           // se dim_file%512 == 0 --> blocco fittizio
      blocks++;
    printf("Numero di blocchi da inviare: %d\n", blocks);

    // Creo processo figlio per gestire il trasferimento dei pacchetti DATA
    pid = fork();
    if (pid == -1) {
      perror("Errore durante la creazione del processo figlio.\nfork()");
      exit(-1);
    }

    if (pid == 0) {
      //  ---  Processo figlio  ---   //

      // Chiudo socket di ascolto delle richieste
      close(sd);

      printf("Creazione socket per la gestione della richiesta...");
      // Creo un nuovo socket per ogni richiesta.
      new_sd = socket(AF_INET, SOCK_DGRAM, 0);
      if (!new_sd) {
        perror("Errore durante la creazione del socket");
        exit(-1);
      }
      printf(" OK!\n");

      // Associo una porta effimera al nuovo socket.
      // Uso la porta per l'invio dei pacchetti DATA e ACK
      memset(&srv_data_addr, 0, sizeof(srv_data_addr));   // Pulizia
      srv_data_addr.sin_family = AF_INET;
      srv_data_addr.sin_addr.s_addr = my_addr.sin_addr.s_addr;
      srv_data_addr.sin_port = htons(0);
      ret = bind(new_sd, (struct sockaddr*)&srv_data_addr, sizeof(srv_data_addr));
      if (ret < 0) {
        perror("Errore in fase di connessione.");
        exit(-1);
      }

      //   inizio invio blocchi    //
      printf("Inizio trasferimento blocchi...\n");
      for (i = 1; i <= blocks; i++) {
        pos = 0;
        temp_buf = (char *)malloc(DATA_PACKET_DIM);
        memset(temp_buf, 0, DATA_PACKET_DIM);

        opcode = htons(DATA);
        block_number = htons(i);

        memcpy(temp_buf+pos, &opcode, sizeof(opcode));
        pos += sizeof(opcode);

        memcpy(temp_buf+pos, &block_number, sizeof(block_number));
        pos += sizeof(block_number);

            // --- trasferimento binario --- //
        if (strcmp(mode, "bin") == 0) {
          if (i < blocks) {
            fread(temp_buf+pos, 512, 1, fd);
            pos += 512;
          } else if (i == blocks) {
            // se dim_file % 512 == 0  -->  invio blocco fittizio
            if ((int)info.st_size % 512 != 0) {
              fread(temp_buf+pos, (int)info.st_size % 512, 1, fd);
              pos += (int)info.st_size % 512;
            }
          }
           // --- trasferimento testuale --- //
        } else if (strcmp(mode, "txt") == 0) {
            if (i < blocks) {
            to_read = 0;
            while(to_read < 512) {
              fscanf(fd, "%c", &c);
              data_block[to_read] = c;
              to_read++;
            }
            memcpy(temp_buf+pos, data_block, 512);
            pos += 512;
          } else if (i == blocks) {
            if ((int)info.st_size % 512 == 0) {     // blocco fittizio
              memcpy(temp_buf+pos, data_block, sizeof(data_block));
              pos += sizeof(data_block);
            } else {
              to_read = 0;
              while(to_read < (int)info.st_size % 512) {
                fscanf(fd, "%c", &c);
                data_block[to_read] = c;
                to_read++;
              }
              memcpy(temp_buf+pos, data_block, (int)info.st_size % 512);
              pos += (int)info.st_size % 512;
            }
          }
        }

          // ---  invio dell' i-esimo blocco --- //
        ret = sendto(new_sd, temp_buf, pos, 0,
                    (struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
        if (ret < 0) {
          perror("Errore durante l'invio del blocco");
          exit(-1);
        }
        printf("Blocco %d del file %s inviato al client %s (porta: %u)\n", i,
                file_name, inet_ntoa(connecting_addr.sin_addr), ntohs(connecting_addr.sin_port));
        free(temp_buf);

          //    inizio ricezione ACK    //
        temp_buf = (char *)malloc(ACK_PACKET_DIM);
        memset(temp_buf, 0, ACK_PACKET_DIM);
        pos = 0;

        ret = recvfrom(new_sd, temp_buf, ACK_PACKET_DIM, 0,
                      (struct sockaddr*)&connecting_addr, (socklen_t*)&addrlen);
        if (ret < 0) {
          printf("Errore durante la ricezione dell' ACK relativo al blocco: %d.\n",
                  ntohs(block_number));
          exit(-1);
        }
          // Leggo pacchetto ACK
        memcpy(&opcode, temp_buf+pos, sizeof(opcode));
        pos += sizeof(opcode);
        opcode = ntohs(opcode);

        memcpy(&received_block_num, temp_buf+pos, sizeof(received_block_num));
        pos += sizeof(received_block_num);
        received_block_num = ntohs(received_block_num);

          // Controllo validità pacchetto ACK
        if (opcode == ACK && received_block_num == ntohs(block_number))
          printf("  --->  ACK numero %d ricevuto\n", i);
        else {
          printf("Errore durante la ricezione dell' ACK relativo al blocco: %d.\n",
                  ntohs(block_number));
          exit(-1);
        }
        free(temp_buf);
          //    fine ricezione ACK    //

      }
      fclose(fd);
      close(new_sd);
      printf("Trasferimento completato.\n");
      exit(0);
      //    fine invio blocchi    //
    }
  }

  close(sd);

}
