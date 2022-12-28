#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_MESSAGE_LENGTH 256
#define PREFIX "\e[36m[Lacewing] "
#define SEPARATOR "~"

#define TYPE_PACKET 0
#define TYPE_LOGIN 1
#define TYPE_LOG 3
#define TYPE_MESSAGE 4

#define PACKET_DISCONNECT '2'
#define PACKET_LOG '3'
#define PACKET_MESSAGE '4'
#define PACKET_NAME '5'

int socketHandle;
char recieveBuffer[MAX_MESSAGE_LENGTH] = { 0 };
char sendBuffer[MAX_MESSAGE_LENGTH] = { 0 };

pthread_t recThread;

//Handle incoming packets
void* recieveThread(void*) {
	while (1) {
		memset(recieveBuffer, 0, sizeof(recieveBuffer));

		read(socketHandle, recieveBuffer, sizeof(recieveBuffer));

		switch (recieveBuffer[0]) {
			case PACKET_DISCONNECT:
				printf(PREFIX "\e[91mYou have been disconnected. Reason: %s\e[0m", recieveBuffer + 2);
				exit(0);
				break;
			case PACKET_LOG:
				printf("\e[3m\e[37m<Log> %s\e[0m\n", recieveBuffer + 2);
				break;
			case PACKET_MESSAGE:
				char* str = strtok(recieveBuffer + 2, "~");
				printf("\e[32m<Message> %s", str);

				str = strtok(NULL, SEPARATOR);
				printf(" - %s\e[0m\n", str);
				break;
			case '\0':
				puts(PREFIX "\e[91mConnection lost\e[0m");
				exit(0);
				break;
			case PACKET_NAME:
				break;
			default:
				printf("\e[90m<Unknown packet> ID: %i | Data: %s\e[0m\n", recieveBuffer[0] - '0', recieveBuffer);
				break;
		}
	}

	return 0;
}

//Send packets
void sendMessage(char* msg, int type) {
	memset(sendBuffer, 0, sizeof(sendBuffer));

	switch (type) {
		case TYPE_MESSAGE:
			strcat(sendBuffer, "4" SEPARATOR);
			break;
		case TYPE_LOG:
			strcat(sendBuffer, "3" SEPARATOR);
			break;
	}

	strcat(sendBuffer, msg);

	if (type == TYPE_LOGIN) {
		strcat(sendBuffer, "\n");
	}

	write(socketHandle, sendBuffer, strlen(sendBuffer));
}

//Ctrl+C handler
void disconnectSignal() {
	puts(PREFIX "~<Quitting>~\e[0m");

	sendMessage("2" SEPARATOR "Client closed", TYPE_PACKET);

	close(socketHandle);

	exit(0);
}

int main(int argc, char** argv) {
	if (argc < 4 || argc > 5) {
		puts(PREFIX "\e[91mCommand usage: ./lacewing <ip> <port> <username> <security code (default: 0)>\e[0m");
		return 0;
	}

	//Creatinga a socket
	socketHandle = socket(AF_INET, SOCK_STREAM, 0);

	if (socketHandle == -1) {
		puts(PREFIX "\e[91mCouldn't create a socket\e[0m");
		return 0;
	}

	struct sockaddr_in server;

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = inet_addr(argv[1]);
	server.sin_port = htons(atoi(argv[2]));

	puts(PREFIX "~<Connecting>~\e[0m");

	//Attempt to establish a connection
	if (connect(socketHandle, (struct sockaddr*)&server, sizeof(server)) != 0) {
		puts(PREFIX "Couldn't connect to the server\e[0m");
		return 0;
	}

	puts(PREFIX "~<Connected>~\e[0m");
	puts(PREFIX "~<Attempting to log in>~\e[0m");

	//Ctrl+C handler
	signal(SIGINT, disconnectSignal);

	//Start recieving packets
	pthread_create(&recThread, NULL, recieveThread, NULL);
	pthread_detach(recThread);

	char inputBuffer[MAX_MESSAGE_LENGTH] = { 0 };

	//Craft the login packet
	strcpy(inputBuffer, "1" SEPARATOR);
	strcat(inputBuffer, argv[3]);
	strcat(inputBuffer, SEPARATOR);
	strcat(inputBuffer, ((argc == 4) ? "0" : argv[4]));

	sendMessage(inputBuffer, TYPE_LOGIN);

	while (1) {
		//Recieve user input
		memset(inputBuffer, 0, sizeof(inputBuffer));

		for (int i = 0; (inputBuffer[i] = getchar()) != '\n'; i++)
			;

		//Send it off
		sendMessage(inputBuffer, TYPE_MESSAGE);

		//Move the terminal caret back a line
		printf("\r\033[A\033[K");
	}

	disconnectSignal();
}
