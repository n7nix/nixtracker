#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fap.h>

#define EXIT_SUCCESS 0
char *readstdin(void);

int main()
{
	char* input;
	unsigned int input_len;
	fap_packet_t* packet;
	char fap_error_output[1024];

	fap_init();

	/* Get packet to parse from stdin */
	input = readstdin();
	input_len = strlen(input);

	/* Process the packet. */
	packet = fap_parseaprs(input, input_len, 0);
	if ( packet->error_code ) {
		fap_explain_error(*packet->error_code, fap_error_output);
		printf("Failed to parse packet (%s): %s\n",
		       input, fap_error_output);
	} else if ( packet->src_callsign ) {
		printf("Got packet from %s.\n", packet->src_callsign);
	}
	fap_free(packet);

	fap_cleanup();

	return EXIT_SUCCESS;
}

char *readstdin(void)
{
#define BUF_SIZE 1024
	char buffer[BUF_SIZE];
	size_t contentSize = 1; /* includes NULL */
/* Preallocate space.  We could just allocate one char here,
but that wouldn't be efficient. */
	char *content = malloc(sizeof(char) * BUF_SIZE);
	if(content == NULL) {
		perror("Failed to allocate content");
		exit(1);
	}
	content[0] = '\0'; /* null-terminated */
	while(fgets(buffer, BUF_SIZE, stdin)) {
		char *old = content;
		contentSize += strlen(buffer);
		content = realloc(content, contentSize);
		if(content == NULL) {
			perror("Failed to reallocate content");
			free(old);
			exit(2);
		}
		strcat(content, buffer);
	}

	if(ferror(stdin)) {
		free(content);
		perror("Error reading from stdin.");
		exit(3);
	}

	return(content);
}