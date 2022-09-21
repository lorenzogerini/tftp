#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include "tftp_client.h"

            /*  ------  funzioni di utilità  -------  */

/* Verifica se ip è valido (pton di prova) */
int check_ip(char* ip) {
  struct sockaddr_in s;
  int ret;
  ret = inet_pton(AF_INET, ip, &s.sin_addr.s_addr);
  if (ret)
    return 1;
  return -1;
}

/* Controlla la validità porta server */
int check_port(char* port) {
  int p;
  p = atoi(port);
  if (p < 1 || p > 65535)
    return -1;
  else
    return p;
}

int check_path(char* path) {
  FILE *fd;
  fd = fopen(path, "w+");
  if (!fd)
    return 0;
  return 1;
}

/* Comando !help */
void help() {
  printf("Sono disponibili i seguenti comandi: \n");
  printf("!help --> mostra l'elenco dei comandi disponibili \n");
  printf("!mode {txt|bin} --> imposta il modo di trasferimento dei files (testo o binario) \n");
  printf("!get filename nome_locale --> richiede al server il file di nome <file_name> e lo salva localmente con il nome <nome_locale> \n");
  printf("!quit --> termina il client\n\n");
}

                  /*  -------    main    -------  */

int main(int argc, char* argv[]) {

  int ret, sd, server_port, block_dim, bn;
  int pos = 0, received_blocks = 0;
  int running = 1;
  struct sockaddr_in srv_addr, data_srv_addr, my_addr;
  int data_addrlen = sizeof(data_srv_addr);
  uint16_t opcode = htons(REQUEST);
  uint16_t block_number, error_number;
  char buffer[BUFFER_SIZE], first_arg[BUFFER_SIZE], second_arg[BUFFER_SIZE],
       error_msg[BUFFER_SIZE];
  char *server_ip, *data_block, *temp_buf;
  char cmd[30], mode[30] = "bin";
  char shell = '>';
  FILE *fd;


  /*    Controllo validità parametri    */
  if(argc != 3 || check_ip(argv[1]) == -1 || check_port(argv[2]) == -1) {
		printf("Errore: formato non corretto o argomenti non validi.\n Usare il formato: tftp_client <ip server> <port server>\n");
		exit(-1);
	}

  server_ip = argv[1];
  server_port = atoi(argv[2]);

  /* Creazione socket */
  sd = socket(AF_INET,SOCK_DGRAM,0);
  if (sd < 0) {
    printf("Errore nella creazione del socket del server.\n");
    exit(-1);
  }

  /* Associo socket all'inidirizzo del client */
  memset(&my_addr, 0, sizeof(my_addr)); // Pulizia
  my_addr.sin_family = AF_INET;
  my_addr.sin_addr.s_addr = INADDR_ANY;
  my_addr.sin_port = htons(0);  // porta scelta dal so
  ret = bind(sd, (struct sockaddr*)&my_addr, sizeof(my_addr) );
  if (ret < 0) {
    perror("Errore in fase di connessione.\n");
    exit(-1);
  }

  /* Creazione indirizzo del server */
  memset(&srv_addr, 0, sizeof(srv_addr)); // Pulizia
  srv_addr.sin_family = AF_INET;
  srv_addr.sin_port = htons(server_port);
  ret = inet_pton(AF_INET, server_ip, &srv_addr.sin_addr);
  if (ret < 0)
    perror("ERRORE: L'indirizzo del server specificato non è valido");

  printf("\n\n");
  help();

  /* Interfaccia linea di comando */
  while(1) {
    printf("%c", shell);

    memset(buffer, 0, BUFFER_SIZE);
    memset(first_arg, 0, BUFFER_SIZE);
    memset(second_arg, 0, BUFFER_SIZE);
    memset(cmd, 0, 30);

    fgets(buffer, BUFFER_SIZE, stdin);
    sscanf(buffer, "%s %s %s", cmd, first_arg, second_arg);
    fflush(stdin);

            /* comando !help */
    if (strcmp(cmd, "!help") == 0) {
      help();
      fflush(stdout);

            /*  comando !mode   */
    } else if (strcmp(cmd, "!mode") == 0) {
      if (strlen(first_arg) == 0 || (strcmp(first_arg, "bin") != 0 &&
          strcmp(first_arg, "txt") != 0)) {
            perror("Argomento non specificato o non valido.");
            exit(1);
      }
      if (strcmp(first_arg, "bin") == 0) {
        strcpy(mode, "bin");
        printf("Modo di trasferimento binario configurato.\n");
      } else {
        strcpy(mode, "txt");
        printf("Modo di trasferimento testuale configurato.\n");
      }

            /*    comando !get    */
    } else if (strcmp(cmd, "!get") == 0) {

      if (strlen(first_arg) == 0 || strlen(second_arg) == 0) {
          perror("Argomenti non specificati o non validi");
          exit(1);
      }

      if (!check_path(second_arg)) {
        printf("La destinazione scelta per salvare il file non è valida.\nScegliere un'altra destinazione.\n");
        continue;
      }

      printf("Richiesta file %s al server in corso.\n", first_arg);

      /*    inizio serializzazione messaggio di richiesta  */
      pos = 0;
      memset(buffer, 0, BUFFER_SIZE);

      opcode = htons(REQUEST);
      memcpy(buffer+pos, &opcode, sizeof(opcode));             // opcode
      pos += sizeof(opcode);

      strcpy(buffer+pos, first_arg);                           // filename
      pos += strlen(first_arg) + 1;

      strcpy(buffer+pos, mode);                                // mode
      pos += strlen(mode) + 1;
      /*    fine serializzazione messaggio di richiesta  */

      /*    invio messaggio di richiesta    */
      ret = sendto(sd, buffer, sizeof(buffer), 0,
                  (struct sockaddr*)&srv_addr, sizeof(srv_addr));
      if (ret < 0) {
        perror("Errore durante l'invio della richiesta.");
        exit(-1);
      }

      running = 1;

      /*    inizio ricezione blocchi    */
      while(running) {
        memset(buffer, 0, BUFFER_SIZE);
        pos = 0;

        ret = recvfrom(sd, buffer, BUFFER_SIZE, 0,
                    (struct sockaddr*)&data_srv_addr, (socklen_t*)&data_addrlen);
        if (ret < 0) {
          perror("Errore durante la ricezione di un blocco.");
          exit(-1);
        }

        received_blocks++;

        //    deserializzazione blocco    //
        memcpy(&opcode, buffer+pos, sizeof(opcode));
        pos += sizeof(opcode);
        opcode = ntohs(opcode);

          //    gestione errore   //
        if (opcode == ERROR) {
          memcpy(&error_number, buffer+pos, sizeof(error_number));
          pos += sizeof(error_number);
          error_number = ntohs(error_number);

          if (error_number == FILE_NOT_FOUND)
            strcpy(error_msg, "File non trovato.\0");
          else if (error_number == ILLEGAL_TFTP_OP)
            strcpy(error_msg, "Operazione non valida.\0");

          printf("%s\n", error_msg);
          received_blocks = 0;
          break;
        } else if (received_blocks == 1){
            printf("Trasferimento file in corso.\n");
        }
          //    fine gestione errore    //

        memcpy(&block_number, buffer+pos, sizeof(block_number));
        pos += sizeof(block_number);
        block_number = ntohs(block_number);

        if (strcmp(mode, "txt") == 0) {
          data_block = malloc(strlen(buffer+pos)+1);
          strcpy(data_block, buffer+pos);
          pos += strlen(data_block)+1;
          block_dim = strlen(data_block);
        } else if (strcmp(mode, "bin") == 0) {
          block_dim = ret - pos;
          data_block = malloc(block_dim);
          memcpy(data_block, buffer+pos, block_dim);
          pos += block_dim;
        }
        //    fine deserializzazione blocco   //

        // Scrivo i dati nel file...

        /*    scrittura binaria    */
        if (strcmp(mode, "bin") == 0) {
          fd = fopen(second_arg, "ab");
          if (!fd)
            perror("Errore creazione file");
          fwrite(data_block, block_dim, 1, fd);
          fclose(fd);
        /*    scrittura testuale    */
        } else if (strcmp(mode, "txt") == 0) {
          fd = fopen(second_arg, "a");
          if (!fd)
            perror("Errore creazione file");
          fprintf(fd, "%s", data_block);
          fclose(fd);
        }
        free(data_block);

        //          invio ack        //
        temp_buf = (char *)malloc(ACK_PACKET_DIM);
        memset(temp_buf, 0, ACK_PACKET_DIM);
        pos = 0;

        opcode = htons(ACK);
        block_number = htons(block_number);

        memcpy(temp_buf+pos, &opcode, sizeof(opcode));
        pos += sizeof(opcode);

        memcpy(temp_buf+pos, &block_number, sizeof(block_number));
        pos += sizeof(block_number);

        ret = sendto(sd, temp_buf, sizeof(temp_buf), 0,
                    (struct sockaddr*)&data_srv_addr, sizeof(data_srv_addr));
        if (ret < 0) {
          printf("Errore durante l'invio dell' ACK relativo al blocco: %d.\n",
                  ntohs(block_number));
          exit(-1);
        };
        free(temp_buf);
        //      fine invio ACK      //

        /*     controllo fine trasferimento     */
        if (block_dim < 512) {
          // per avere un riscontro tra i blocchi contati dal client e il numero di 
          // blocco inviato dal server, che essendo su 16 bit può essere al max 65535
          block_number = ntohs(block_number);
          bn = (int) block_number;											
          bn = bn + (received_blocks/65535)*65535 + received_blocks/65535;
          printf("Trasferimento completato. (%d/%d blocchi)\n",received_blocks,
                 bn);
          printf("Salvataggio %s completato.\n", second_arg);
          received_blocks = 0;
          running = 0;
        }
      }
      /*    fine ricezione blocchi      */

      fflush(stdout);

          /*    comando !quit    */
    } else if (strcmp(cmd, "!quit") == 0) {
      printf("Terminazione client... \n");
      close(sd);
      exit(0);
    } else {
      printf("Comando sconosciuto. Per visualizzare la lista dei comandi usa !help\n");
    }

  }

}
