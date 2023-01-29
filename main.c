#include <arpa/inet.h>
#include <netdb.h>
#include <openssl/evp.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

//Imported from Spark
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
#define TERM_CLEARLINE "\e\r[K"

#define MAX_MESSAGE_LENGTH 4096
#define PREFIX COLOR_CYAN "[Lacewing] "
#define SEPARATOR "~"
#define NEWLINE "\n"

int socketHandle;
pthread_t recThread;

//Whether the client is actively listening or already shutting down
int isActive = 1;

struct termios originalTermAttributes;

//Global for it to be able to be drawn in the recieve thread
char inputBuffer[MAX_MESSAGE_LENGTH] = { '\0' };

//Send packets
void sendMessage(char* msg, int type) {
	char sendBuffer[MAX_MESSAGE_LENGTH] = { '\0' };

	//Prefix the data with the packet id
	sendBuffer[0] = type;
	strcat(sendBuffer, SEPARATOR);

	strcat(sendBuffer, msg);
	strcat(sendBuffer, NEWLINE);

	write(socketHandle, sendBuffer, strlen(sendBuffer));
}

//Ctrl+C handler
void disconnectSignal() {
	//Start shutting down
	isActive = 0;

	//Restore original terminal attributes
	tcsetattr(fileno(stdin), TCSANOW, &originalTermAttributes);

	puts(TERM_CLEARLINE PREFIX "~<Quitting>~" COLOR_RESET);

	sendMessage("Client closed", PACKET_DISCONNECT);

	close(socketHandle);

	exit(0);
}

void hashMD5(char* src, unsigned char* dest) {
	unsigned int destSize = EVP_MD_size(EVP_md5());

	EVP_MD_CTX* context = EVP_MD_CTX_new();

	EVP_DigestInit_ex(context, EVP_md5(), NULL);

	EVP_DigestUpdate(context, src, strlen(src));

	EVP_DigestFinal_ex(context, dest, &destSize);

	EVP_MD_CTX_free(context);
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

		if (!isActive) {
			return 0;
		}

		//The packets could have been concatenated
		for (char *packetPtr, *packet = strtok_r(recieveBuffer, NEWLINE, &packetPtr); packet != NULL;
				 packet = strtok_r(NULL, NEWLINE, &packetPtr)) {
			switch (packet[0]) {
				case PACKET_DISCONNECT:
					printf(TERM_CLEARLINE PREFIX COLOR_RED "You have been disconnected. Reason: %s" COLOR_RESET, packet + 2);
					disconnectSignal();
					break;
				case PACKET_LOG:
					//The server sends separators instead of newlines in log packets
					for (int i = 2, len = strlen(packet); i < len; i++) {
						if (packet[i] == SEPARATOR[0]) {
							packet[i] = '\n';
						}
					}

					printf(TERM_CLEARLINE COLOR_ITALIC COLOR_GREY "<Log> %s" COLOR_RESET NEWLINE, packet + 2);

					printInputBuffer();
					break;
				case PACKET_MESSAGE:;
					printf(TERM_CLEARLINE);

					char *msgPart, *str = strtok_r(packet + 2, SEPARATOR, &msgPart);

					//Print the message
					printf(COLOR_GREEN "<Message> %s", str);

					//The author
					str = strtok_r(NULL, SEPARATOR, &msgPart);

					printf(" - %s", str);

					//And the timestamp
					str = strtok_r(NULL, SEPARATOR, &msgPart);

					//Timestamps are sent in the millisecond format
					time_t time = atol(str);
					time /= 1000;

					//Convert to local time
					struct tm localTime;
					localtime_r(&time, &localTime);

					//Format
					char formattedTime[64];
					strftime(formattedTime, sizeof(formattedTime), "%H:%M %d/%m/%y", &localTime);

					printf(" (%s)" COLOR_RESET NEWLINE, formattedTime);

					printInputBuffer();
					break;
				case '\0':
					puts(TERM_CLEARLINE PREFIX COLOR_RED "Connection lost" COLOR_RESET);
					disconnectSignal();
					break;
				case PACKET_NAME:
					//Noop
					break;
				default:
					printf(TERM_CLEARLINE COLOR_DARK_GREY "<Unknown packet> ID: %i | Data: %s" COLOR_RESET NEWLINE,
								 packet[0] - '0',
								 packet);
					break;
			}
		}
	}

	return 0;
}

int main(int argc, char** argv) {
	if (argc != 5) {
		puts(PREFIX COLOR_RED "Command usage: ./lacewing <ip> <port> <username> <password>" COLOR_RESET);
		return 0;
	}

	//Create a socket
	socketHandle = socket(AF_INET, SOCK_STREAM, 0);

	if (socketHandle == -1) {
		puts(PREFIX COLOR_RED "Couldn't create a socket" COLOR_RESET);
		return 0;
	}

	//Server parameters
	struct sockaddr_in server = { .sin_family = AF_INET };

	{
		struct addrinfo* info;
		char resolvedIP[INET_ADDRSTRLEN];

		//Resolve the hostname
		if (getaddrinfo(argv[1], NULL, NULL, &info) != 0) {
			puts(PREFIX COLOR_RED "Invalid host" COLOR_RESET);
			return 0;
		}

		//Convert to string
		inet_ntop(AF_INET, &(((struct sockaddr_in*)info->ai_addr)->sin_addr), resolvedIP, sizeof(resolvedIP));

		//Set the ip and port
		server.sin_addr.s_addr = inet_addr(resolvedIP);
		server.sin_port = htons(atoi(argv[2]));
	}

	//Set the terminal title
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
		memcpy(&rawModeAttributes, &originalTermAttributes, sizeof(rawModeAttributes));
		rawModeAttributes.c_lflag &= ~(ECHO | ICANON);

		//Set the terminal to raw mode
		tcsetattr(fileno(stdin), TCSANOW, &rawModeAttributes);
	}

	//Start recieving packets
	pthread_create(&recThread, NULL, recieveThread, NULL);
	pthread_detach(recThread);

	//Craft the login packet

	//Username
	strcat(inputBuffer, argv[3]);
	strcat(inputBuffer, SEPARATOR);

	{
		//Password
		const int md5Size = EVP_MD_size(EVP_md5());

		//Store the raw representation
		unsigned char* temp = malloc(md5Size);
		hashMD5(argv[4], temp);

		//Allocate enough space for a hex md5 string + null terminator
		char* hashedPass = malloc(md5Size * 2 + 1);

		for (int i = 0; i < md5Size; i++) { sprintf(&hashedPass[i * 2], "%02x", (unsigned int)temp[i]); }

		free(temp);

		//And finally store the password in the packet
		strcat(inputBuffer, hashedPass);

		free(hashedPass);
	}

	sendMessage(inputBuffer, PACKET_CONNECT);

	while (1) {
		memset(inputBuffer, '\0', sizeof(inputBuffer));

		int charactersInBuff = 0;
		char character;

		printInputBuffer();

		//Wait for the user to type the message
		while (1) {
			while ((character = fgetc(stdin)) == EOF) {}

			if (character == '\n') {
				if (charactersInBuff > 0) {
					break;
				}
			} else {
				//The "delete" character
				if (character == 127) {
					if (charactersInBuff > 0) {
						inputBuffer[--charactersInBuff] = '\0';
						printf("\b \b");
					}
				} else if (character != '\e') {
					fputc(character, stdout);

					inputBuffer[charactersInBuff++] = character;
				}
			}
		}

		//The server currently only supports the "general" channel
		strcat(inputBuffer, SEPARATOR "general");

		//Send it off
		sendMessage(inputBuffer, PACKET_MESSAGE);

		printf(TERM_CLEARLINE);
	}

	disconnectSignal();
}
