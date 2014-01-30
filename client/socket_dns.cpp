#include <assert.h>
#include <malloc.h>
#include <errno.h>
#include <unistd.h>
#include <map>
#include <math.h>
#include <set>
#include <string>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
extern "C" {
#include "base32.h"
}
#include "socket_dns.h"

#define DOTS_PER_NAME 4
#define MAX_LABEL_SIZE 63
#define MAX_NAME_SIZE 253

static char* SOCK_SUFFIX; // = "sock.a.a";
static char* SEND_SUFFIX; // = "snd.a.a";
static char* RECV_SUFFIX; // = "recv.a.a";
static char* CLS_SUFFIX; // = "cls.a.a";

std::map<int, int> file_descriptor_table;
std::map<int, int> fd_to_port_table;
std::map<int, std::string> remote_port_table;
std::set<int> used_ports;

extern "C" void init_dns(const char* root_domain) {
	srand(time(NULL));
	res_init();
	std::string tmp("sock.");
	tmp += root_domain;
	SOCK_SUFFIX = new char [tmp.length()+1];
	strcpy(SOCK_SUFFIX, tmp.c_str());
	tmp = "snd.";
	tmp += root_domain;
	SEND_SUFFIX = new char [tmp.length()+1];
	strcpy(SEND_SUFFIX, tmp.c_str());
	tmp = "recv.";
	tmp += root_domain;
	RECV_SUFFIX = new char [tmp.length()+1];
	strcpy(RECV_SUFFIX, tmp.c_str());
	tmp = "cls.";
	tmp += root_domain;
	CLS_SUFFIX = new char [tmp.length()+1];
	strcpy(CLS_SUFFIX, tmp.c_str());
}

// for(int i=0; i<100; i++) {
//     printf("I won't call perl scripts from 'system' functions!\n");
// }
unsigned char* txt_info_for_hostname(const char* domain, int *txt_len, bool decode) {
	assert(strlen(domain) < 256);
	FILE *fp;
	char path[100];
	std::string real_result;
	char command[325]; // Somewhat arbitrary limit, but txt_len must
	// be less than 256 characters in size
	if(decode) {
		sprintf(command, "nslookup %s | ./extract_and_decode.pl", domain);
	} else {
		sprintf(command, "nslookup %s | ./extract.pl", domain);
	}
	
	/* Open the command for reading. */
	fp = popen(command, "r");
	if (fp == NULL) {
		return NULL;
	}
	
	/* Read the output a line at a time - output it. */
	while (fgets(path, sizeof(path)-1, fp) != NULL) {
		real_result += path;
	}
	
	/* close */
	pclose(fp);
	
	char* result = (char *)malloc(sizeof(char) * (real_result.length() + 1));
	if(!result) {
		return NULL;
	}
	
	strcpy(result, real_result.c_str());
	*txt_len = real_result.length();
	
	return (unsigned char*)result;
}

extern "C" int socket_dns(int __domain, int __type, int __protocol) {
	assert(__domain == AF_INET);
	assert(__type == SOCK_DGRAM);
	int pipefds[2];
	pipe(pipefds);
	file_descriptor_table[pipefds[0]] = pipefds[1];
	int len;
	char* newsock = (char*)txt_info_for_hostname(SOCK_SUFFIX, &len, false);
	remote_port_table[pipefds[0]] = std::string(newsock);
	free(newsock);
	return pipefds[0];
}

int get_rand_free_port() {
	int prt;
	// Keep trying to find a random virtual port that is free
	while((prt = rand() % 65534 + 1)) {
		// Check if is not listed yet
		if(used_ports.find(prt) == used_ports.end()) {
			return prt;
		}
	}
    return -1;
}

extern "C" int bind_dns(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	const struct sockaddr_in* real_addr = (sockaddr_in*)addr;
	used_ports.insert(real_addr->sin_port);
	fd_to_port_table[sockfd] = real_addr->sin_port;
    return 0;
}

extern "C" ssize_t sendto_dns(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen) {
	ssize_t result_written = 0; // Return value = number of bytes sent
	int port; // Number of the virtual port on the server-side
	char static_vars[23]; // String containing static data for all requests
	int unused_length; // Number of symbols from the requested hostname
					   // required for the port, snd and suffix
	int payload_length; // Number of symbols left for useful payload
						// (dots not included here)
	char current_hostname[MAX_NAME_SIZE + 2]; // Hostname that the
						// request will contain (move this to heap?)
	int encoded_input_cap; // Capacity for the encoded input buffer
						   // (safe upper bound)
	char *full_payload; // Buffer for the hostname we're about to send
	int encoded_input_size; // Actual size of encoded input
	int left_to_send; // Bytes left to send in payload
	char *payload_left; // Pointer to what's left of the full_payload
	int current_to_send_length; // Data to be sent with current request
	int current_not_written_data_length; // Data not yet written to the rq
	int i; // iteration variable (duh!)
	int written_data_pt; // Data written to current request
						 // ( <= current_to_send_length)
	int label_size; // Size of the current label in the hostname
	char* answer_received; // Answer from the server
	int answer_size; // Size of the server answer
	sockaddr_in* real_addr; // Casted dest_addr to an in_addr
	
	assert(dest_addr->sa_family == AF_INET); // Only AF_INET supported
	real_addr = (sockaddr_in*)dest_addr;
	
	if(fd_to_port_table.find(sockfd) == fd_to_port_table.end()) {
		struct sockaddr_in addr;
		bzero(&addr, sizeof(sockaddr_in));
		addr.sin_family = AF_INET;
		addr.sin_port = get_rand_free_port();
		bind_dns(sockfd, (const struct sockaddr*)&addr, sizeof(sockaddr_in));
	}
	// Get virtual "port" number responding to this file descriptor
	port = fd_to_port_table[sockfd];
	sprintf(static_vars, "%d.%d.%d.", real_addr->sin_addr.s_addr, real_addr->sin_port, port);
	
	// Calculate useful and unused parts of the hostname
	unused_length = strlen(static_vars) + strlen(SEND_SUFFIX);
	payload_length = MAX_NAME_SIZE - unused_length - DOTS_PER_NAME;
	
	// Calculate proper capacity for encoded input buffer and allocate
	encoded_input_cap = (int)ceil(1.6 * len) + 9;
	full_payload = (char *)malloc(sizeof(char) * encoded_input_cap);
	if(!full_payload) goto payload_free;
	
	// Get actual size required for the buffer
	encoded_input_size = base32_encode((uint8_t *)buf, len, (uint8_t *)full_payload, encoded_input_cap);
	full_payload[encoded_input_size++] = '='; // Indicate end-of-packet
	
	// Nothing sent yet => everything is left_to_send;
	left_to_send = encoded_input_size;
	// payload_left points to the beginning of full_payload now
	payload_left = full_payload;
	
	// Until we send all info
	while(left_to_send > 0) {
		// Calculate size to write to this packet
		current_to_send_length = std::min(left_to_send, payload_length);
		written_data_pt=0;
		current_not_written_data_length = current_to_send_length;
		
		// Write the corresponding data part to all labels; append '.'
		for(i=0; i<4 && current_not_written_data_length > 0; i++) {
			// Calculate how much data we can fit in the current label
			label_size = std::min(MAX_LABEL_SIZE, current_not_written_data_length);
			memcpy(current_hostname + written_data_pt, payload_left, label_size);
			written_data_pt += label_size;
			payload_left += label_size;
			current_not_written_data_length -= label_size;
			current_hostname[written_data_pt++] = '.';
		}
		
		// Append the "unused data" part
		memcpy(current_hostname + written_data_pt, static_vars, strlen(static_vars));
		written_data_pt += strlen(static_vars);
		memcpy(current_hostname + written_data_pt, SEND_SUFFIX, strlen(SEND_SUFFIX));
		written_data_pt += strlen(SEND_SUFFIX);
		
		//TODO: Remove this
		current_hostname[written_data_pt] = '\0';
		printf("%s\n", current_hostname);
		
		// Send data
		answer_received = (char* )txt_info_for_hostname(current_hostname, &answer_size, false);
		// TODO: Remove this
		printf("%s\n", answer_received);
		if(strcmp(answer_received, "PARTOK") != 0) {
			// TODO: Set an error code or something...
			printf("Error!\n");
			break;
		}
		
		// Update what's left to send and what's written
		left_to_send -= current_to_send_length;
		result_written += current_to_send_length;
	}
payload_free:
	free(full_payload);
	return result_written * (5.0 / 8);
}

extern "C" ssize_t recvfrom_dns(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen) {
	assert(src_addr == NULL);
	assert(addrlen == NULL);
	int port = fd_to_port_table[sockfd];
	char prt[7];
	sprintf(prt, "%d.", port);
	std::string full_address = prt;
	full_address += RECV_SUFFIX;
	int data_read;
	printf("%s\n", full_address.c_str());
	unsigned char* result = txt_info_for_hostname(full_address.c_str(), &data_read, true);
	printf("%s\n", result);
	memcpy(buf, result, std::min(len, (size_t)data_read));
	free(result);
	return data_read;
}

/*
int main() {
	init_dns("a.a");
	int length = 0;
	char buffer[1000];
	int last = recvfrom_dns(12, buffer, 1000, 0, NULL, NULL);
	buffer[last] = '\0';
	printf("Text was: %s\n", buffer);
	printf("Size was: %d\n", last);
	for(int i=0; i<1000; i++) {
		buffer[i] = (i % 26) + 'A';
	}
	sockaddr_in address;
	address.sin_port = 2131;
	address.sin_family = AF_INET;
	in_addr ipaddr;
	inet_pton(AF_INET, "192.168.5.4", &ipaddr);
	address.sin_addr = ipaddr;
	sendto_dns(12, buffer, 1000, 0, (sockaddr*)&address, sizeof(address));
	return 0;
}
*/
