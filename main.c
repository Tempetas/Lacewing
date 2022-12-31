#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define PACKET_UNKNOWN '0'
#define PACKET_CONNECT '1'
#define PACKET_DISCONNECT '2'
#define PACKET_LOG '3'
#define PACKET_MESSAGE '4'
#define PACKET_NAME '5'

#define COLOR_RESET "\e[0m"
#define COLOR_RED "\e[91m"
#define COLOR_CYAN "\e[36m"
#define COLOR_ITALIC "\e[3m"
#define COLOR_GREY "\e[37m"
#define COLOR_GREEN "\e[32m"
#define COLOR_DARK_GREY "\e[90m"
#define COLOR_CLEARLINE "\e\r[K"

#define MAX_MESSAGE_LENGTH 4096
#define PREFIX COLOR_CYAN "[Lacewing] "
#define SEPARATOR "~"

int socketHandle;
pthread_t recThread;

struct termios originalTermAttributes;

//Global for it to be able to be drawn in the recieve thread
char inputBuffer[MAX_MESSAGE_LENGTH] = { '\0' };

void resetTerminal() {
	//Restore original terminal attributes
	tcsetattr(fileno(stdin), TCSANOW, &originalTermAttributes);
}

//Send packets
void sendMessage(char* msg, int type) {
	char sendBuffer[MAX_MESSAGE_LENGTH] = { '\0' };

	//Prefix the data with the packet id
	if (type != PACKET_UNKNOWN) {
		sendBuffer[0] = type;
		strcat(sendBuffer, SEPARATOR);
	}

	strcat(sendBuffer, msg);
	strcat(sendBuffer, "\n");

	write(socketHandle, sendBuffer, strlen(sendBuffer));
}

//Ctrl+C handler
void disconnectSignal() {
	resetTerminal();

	puts(COLOR_CLEARLINE PREFIX "~<Quitting>~" COLOR_RESET);

	sendMessage("Client closed", PACKET_DISCONNECT);

	close(socketHandle);

	exit(0);
}

void printInputBuffer() {
	printf("> %s", inputBuffer);
	fflush(stdout);
}

//Handle incoming packets
void* recieveThread(void* ptr) {
	char recieveBuffer[MAX_MESSAGE_LENGTH] = { '\0' };

	while (1) {
		memset(recieveBuffer, '\0', sizeof(recieveBuffer));

		read(socketHandle, recieveBuffer, sizeof(recieveBuffer));

		switch (recieveBuffer[0]) {
			case PACKET_DISCONNECT:
				printf(PREFIX COLOR_RED "You have been disconnected. Reason: %s" COLOR_RESET, recieveBuffer + 2);
				disconnectSignal();
				break;
			case PACKET_LOG:
				printf(COLOR_CLEARLINE);

				printf(COLOR_ITALIC COLOR_GREY "<Log> %s" COLOR_RESET, recieveBuffer + 2);

				printInputBuffer();
				break;
			case PACKET_MESSAGE:;
				char* str = strtok(recieveBuffer + 2, "~");

				//Erase current line to overwrite user input
				printf(COLOR_CLEARLINE);

				//Print the message
				printf(COLOR_GREEN "<Message> %s", str);

				//The author
				str = strtok(NULL, SEPARATOR);
				printf(" - %s", str);

				//And the timestamp
				str = strtok(NULL, SEPARATOR);

				//Workaround for the weird way timestamps are formatted by the server
				str[strlen(str) - 4] = '\0';

				time_t time = atol(str);

				//Convert to local time
				struct tm localTime;
				localtime_r(&time, &localTime);

				//Format
				char formattedTime[64];
				strftime(formattedTime, sizeof(formattedTime), "%H:%M %d/%m/%y", &localTime);

				printf(" (%s)" COLOR_RESET "\n", formattedTime);

				printInputBuffer();
				break;
			case '\0':
				puts(PREFIX COLOR_RED "Connection lost" COLOR_RESET);
				resetTerminal();
				exit(0);
				break;
			case PACKET_NAME:
				break;
			default:
				printf(
					COLOR_DARK_GREY "<Unknown packet> ID: %i | Data: %s" COLOR_RESET "\n", recieveBuffer[0] - '0', recieveBuffer);
				break;
		}
	}

	return 0;
}

int main(int argc, char** argv) {
	if (argc < 4 || argc > 5) {
		puts(PREFIX COLOR_RED "Command usage: ./lacewing <ip> <port> <username> <security code (default: 0)>" COLOR_RESET);
		return 0;
	}

	//Creating a socket
	socketHandle = socket(AF_INET, SOCK_STREAM, 0);

	if (socketHandle == -1) {
		puts(PREFIX COLOR_RED "Couldn't create a socket" COLOR_RESET);
		return 0;
	}

	//Server parameters
	struct sockaddr_in server;

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = inet_addr(argv[1]);
	server.sin_port = htons(atoi(argv[2]));

	//Set terminal title
	printf("\033]0;Lacewing\007");

	puts(PREFIX "~<Connecting>~" COLOR_RESET);

	//Attempt to establish a connection
	if (connect(socketHandle, (struct sockaddr*)&server, sizeof(server)) != 0) {
		puts(PREFIX COLOR_RED "Couldn't connect to the server" COLOR_RESET);
		return 0;
	}

	puts(PREFIX "~<Connected>~" COLOR_RESET);
	puts(PREFIX "~<Attempting to log in>~" COLOR_RESET);

	//Ctrl+C handler
	signal(SIGINT, disconnectSignal);

	{
		struct termios rawModeAttributes;

		//Store original terminal attributes
		tcgetattr(fileno(stdin), &originalTermAttributes);

		//Prepare raw mode attributes
		memcpy(&rawModeAttributes, &originalTermAttributes, sizeof(struct termios));
		rawModeAttributes.c_lflag &= ~(ECHO | ICANON);
		rawModeAttributes.c_cc[VTIME] = 0;
		rawModeAttributes.c_cc[VMIN] = 1;

		//Set the terminal to raw mode
		tcsetattr(fileno(stdin), TCSANOW, &rawModeAttributes);
	}

	//Start recieving packets
	pthread_create(&recThread, NULL, recieveThread, NULL);
	pthread_detach(recThread);

	//Craft the login packet
	strcat(inputBuffer, argv[3]);
	strcat(inputBuffer, SEPARATOR);
	strcat(inputBuffer, ((argc == 4) ? "0" : argv[4]));

	//Workaround for the server's current socket implementation
	strcat(inputBuffer, "\n");

	sendMessage(inputBuffer, PACKET_CONNECT);

	while (1) {
		memset(inputBuffer, '\0', sizeof(inputBuffer));

		int charactersInBuff = 0;
		char character;

		printInputBuffer();

		//Wait for the user to type the message
		while (1) {
			while ((character = fgetc(stdin)) == EOF)
				;
			if (character == '\n') {
				if (charactersInBuff > 0) {
					break;
				}
			} else {
				//The "delete" character
				if (character == 127 && charactersInBuff > 0) {
					inputBuffer[--charactersInBuff] = '\0';

					printf("\b \b");
				} else {
					fputc(character, stdout);

					inputBuffer[charactersInBuff++] = character;
				}
			}
		}

		//Send it off
		sendMessage(inputBuffer, PACKET_MESSAGE);

		//Move the terminal caret back a line
		printf("\r\e[K");
	}

	disconnectSignal();
}
