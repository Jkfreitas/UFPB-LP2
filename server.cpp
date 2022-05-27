#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>
#include <iostream>

#define MAX_CLIENTS 20 //20 conexões
#define BUFFER_SZ 2048

static _Atomic unsigned int cli_count = 0;
static int uid = 10;

//Estrutura do cliente
typedef struct{
	struct sockaddr_in address;
	int sockfd;
	int uid;
	char name[32];
} client_t;

client_t *clients[MAX_CLIENTS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void str_overwrite_stdout() {
    printf("\r%s", "> ");
    fflush(stdout);
}

void str_trim_lf (char* arr, int length) {
  int i;
  for (i = 0; i < length; i++) { // trim \n
    if (arr[i] == '\n') {
      arr[i] = '\0';
      break;
    }
  }
}

void print_client_addr(struct sockaddr_in addr){
    std::cout
			<<(addr.sin_addr.s_addr & 0xff)					<<std::endl
			<<((addr.sin_addr.s_addr & 0xff00) >> 8 )		<<std::endl
			<<((addr.sin_addr.s_addr & 0xff0000) >> 16)		<<std::endl
			<<((addr.sin_addr.s_addr & 0xff000000) >> 24)	<<std::endl;
}

    //add clientes a fila
void queue_add(client_t &cl){
	pthread_mutex_lock(&clients_mutex);

	for(int i=0; i < MAX_CLIENTS; ++i){
		if(!clients[i]){
			clients[i] = cl;
			break;
		}
	}

	pthread_mutex_unlock(&clients_mutex);
}

//clear clientes da fila
void queue_remove(int uid){
	pthread_mutex_lock(&clients_mutex);

	for(int i=0; i < MAX_CLIENTS; ++i){
		if(clients[i]){
			if(clients[i]->uid == uid){
				clients[i] = NULL;
				break;
			}
		}
	}

	pthread_mutex_unlock(&clients_mutex);
}
//salvando no log
void sendTolog(char* mens){
	pthread_mutex_lock(&clients_mutex);

	FILE *esc;
    esc = fopen("log.txt", "a");
    fprintf(esc, "%s", mens);
    fclose(esc);

	pthread_mutex_unlock(&clients_mutex);
}

    //enviar para todos, menos para o remetente
void send_message(char *s, int uid){
	pthread_mutex_lock(&clients_mutex);

	for(int i=0; i<MAX_CLIENTS; ++i){
		if(clients[i]){
			if(clients[i]->uid != uid){
				if(write(clients[i]->sockfd, s, strlen(s)) < 0){
					perror("ERROR: Falha na gravação");
					break;
				}
			}
		}
	}

	pthread_mutex_unlock(&clients_mutex);
}

    // comunicação com o cliente
void *handle_client(void *arg){
	char buff_out[BUFFER_SZ];
	char name[32];
	int leave_flag = 0;

	cli_count++;
	client_t *cli = (client_t *)arg;

	// nome
	if(recv(cli->sockfd, name, 32, 0) <= 0 || strlen(name) <  2 || strlen(name) >= 32-1){
		std::cout<<"Não digitou o nome.\n"<<std::endl;
		leave_flag = 1;
	} else{
		strcpy(cli->name, name);
		sprintf(buff_out, "%s se juntou a\n", cli->name);
		std:cout<<buff_out<<endl;
		sendTolog(buff_out);
		send_message(buff_out, cli->uid);
	}

	bzero(buff_out, BUFFER_SZ);

	while(1){
		if (leave_flag) {
			break;
		}

		int receive = recv(cli->sockfd, buff_out, BUFFER_SZ, 0);
		if (receive > 0){
			if(strlen(buff_out) > 0){
				send_message(buff_out, cli->uid);
				sendTolog(buff_out);
				str_trim_lf(buff_out, strlen(buff_out));
				std::cout<<buff_out<<" -> "<< cli->name<<std::endl;
			}
		} else if (receive == 0 || strcmp(buff_out, "exit") == 0){
			sprintf(buff_out, "%s saiu\n", cli->name);
			sendTolog(buff_out);
			std::cout<<buff_out<<std::endl;
			send_message(buff_out, cli->uid);
			leave_flag = 1;
		} else {
			std::cout<<"ERROR: -1\n"<<std::endl;
			leave_flag = 1;
		}

		bzero(buff_out, BUFFER_SZ);
	}

  	//exclui cliente da fila e gera a thread
	close(cli->sockfd);
  	queue_remove(cli->uid);
  	free(cli);
  	cli_count--;
  	pthread_detach(pthread_self());

	return NULL;
}

int main(int argc, char **argv){
	if(argc != 3){
		std::cout<<"Uso: "<<argv[2]<<" <port>\n"<<std::endl;
		return EXIT_FAILURE;
	}

	char *ip = "127.0.0.1"; // deve ser o ip do pc que vai rodar o servidor
	int port = atoi(argv[2]);
	int option = 1;
	int listenfd = 0, connfd = 0;
	struct sockaddr_in serv_addr;
	struct sockaddr_in cli_addr;
	pthread_t tid;

	//configuração do socket
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr(ip);
	serv_addr.sin_port = htons(port);


	signal(SIGPIPE, SIG_IGN);

	if(setsockopt(listenfd, SOL_SOCKET,(SO_REUSEPORT | SO_REUSEADDR),(char*)&option,sizeof(option)) < 0){
		perror("ERROR: setsockopt falhou");
    	return EXIT_FAILURE;
	}


 	if(bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
		perror("ERROR: Socket binding falhou");
		return EXIT_FAILURE;
	}


	if (listen(listenfd, 10) < 0) {
		perror("ERROR: falha na escuta");
		return EXIT_FAILURE;
	}

	std::cout<<"BEM VINDO AO CHAT DO CI!\n"<<std::endl;

	while(1){
		socklen_t clilen = sizeof(cli_addr);
		connfd = accept(listenfd, (struct sockaddr*)&cli_addr, &clilen);

		//Capacidade
		if((cli_count + 1) == MAX_CLIENTS){
			std::cout<<"Servidor lotado. Rejeitados: "<<std::endl;
			print_client_addr(cli_addr);
			std::cout<<cli_addr.sin_port<<std:endl;
			close(connfd);
			continue;
		}

		//configuração do cliente
		client_t *cli = (client_t *)malloc(sizeof(client_t));
		cli->address = cli_addr;
		cli->sockfd = connfd;
		cli->uid = uid++;


		queue_add(*cli);
		pthread_create(&tid, NULL, &handle_client, (void*)cli);


		sleep(1);
	}

	return EXIT_SUCCESS;
}
