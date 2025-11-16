#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "l2sap.h"


/* compute_checksum is a helper function for l2_sendto and
 * l2_recvfrom_timeout to compute the 1-byte checksum both
 * on sending and receiving and L2 frame.
 */
static uint8_t compute_checksum( const uint8_t* frame, int len ) {
    uint8_t checksum = 0;
    for (int i = 0; i < len; i++) {
        checksum ^= frame[i]; // XOR all bytes except the checksum field
    }
    return checksum;
}

// Initializes and configures a UDP socket and stores it in a L2SAP structure
L2SAP* l2sap_create( const char* server_ip, int server_port ) {
    // Allocate memory for the L2SAP structure
    L2SAP* client = (L2SAP*)malloc(sizeof(L2SAP));
    if (!client) {
        fprintf(stderr, "%s ERROR: malloc failed\n", __FUNCTION__); 
        return NULL;
    }
    memset(client, 0, sizeof(L2SAP));

    // Create the UDP socket (IPv4)
    client->socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (client->socket < 0) {
        fprintf(stderr, "%s ERROR: socket failed\n", __FUNCTION__); 
        free(client);
        return NULL;
    }
    // Clear peer address memory
    memset(&client->peer_addr, 0, sizeof(client->peer_addr));
  
    // Set address family to IPv4
    client->peer_addr.sin_family = AF_INET;
    client->peer_addr.sin_port = htons(server_port);

    // Set the server IP address
    if (inet_pton(AF_INET, server_ip, &client->peer_addr.sin_addr) <= 0) {
        fprintf(stderr, "%s ERROR: inet_pton failed\n", __FUNCTION__);
        close(client->socket);
        free(client);
        return NULL;
    }
    fprintf(stderr, "%s: Created L2SAP with socket %d\n", __FUNCTION__, client->socket);
    return client;
}

// Closes socket and frees memory associated with L2SAP
void l2sap_destroy(L2SAP* client) {
    if (client != NULL){
        if (client-> socket >= 0){
            close(client->socket); // Properly close the socket
        }
        free(client); // Free the L2SAP structure
    }
}

/* l2sap_sendto sends data over UDP, using the given UDP socket
 * sock, to a remote UDP receiver that is identified by
 * peer_address.
 * The parameter data points to payload that L3 wants to send
 * to the remote L3 entity. This payload is len bytes long.
 * l2_sendto must add an L2 header in front of this payload.
 * When the payload length and the L2Header together exceed
 * the maximum frame size L2Framesize, l2_sendto fails.
 */
// Sends an L2 frame (header + payload) to a remote peer via UDP
int l2sap_sendto( L2SAP* client, const uint8_t* data, int len ) {
    // Parameter validation
    if (client == NULL || data == NULL || len < 0) {
        fprintf(stderr, "%s ERROR: invalid parameters\n", __FUNCTION__);
        return -1;
    }
    // Check if payload exceeds the maximum allowed L2 payload size
    if (len > L2Payloadsize) {
        fprintf(stderr, "%s ERROR: payload too large\n", __FUNCTION__);
        return -1;
    }
    // Check if the socket is valid
    if (client->socket < 0){
        fprintf(stderr, "%s ERROR: invalid socket\n", __FUNCTION__);
        return -1;
    }
    // Validate total frame size
    if ((len + L2Headersize) > L2Framesize) {
        fprintf(stderr, "%s ERROR: frame too large\n", __FUNCTION__);
        return -1;
    }

    // Allocate and zero the buffer that will hold the full frame
    uint8_t frame[L2Framesize];
    memset(frame, 0, L2Framesize);
   
    // Fill in the L2 header
    struct L2Header* header= (struct L2Header*)frame;
    header->dst_addr = htons(client->peer_addr.sin_addr.s_addr);
    header->len = htons(len + L2Headersize);  // Set total length in host byte order
    header->checksum = 0; 
    header->mbz = 0;  
    
    // Copy the payload data into the frame after the header
    memcpy(frame + L2Headersize, data, len);

    // Compute the checksum
    header->checksum = compute_checksum(frame, len + L2Headersize);
    
    // Send the frame to the remote peer
    int sent_udpbytes = sendto(client->socket, frame, len + L2Headersize, 0,
                    (struct sockaddr*)&client->peer_addr, sizeof(client->peer_addr));
    if (sent_udpbytes < 0){
        fprintf(stderr, "%s ERROR: sendto failed\n", __FUNCTION__);
        return -1;
    }

    fprintf(stderr, "%s successful sendto\n", __FUNCTION__);
    return len;
}

/* Convenience function. Calls l2sap_recvfrom_timeout with NULL timeout
 * to make it waits endlessly.
 */
// This function is a convenience wrapper around l2sap_recvfrom_timeout
int l2sap_recvfrom( L2SAP* client, uint8_t* data, int len ) {
    return l2sap_recvfrom_timeout( client, data, len, NULL );
}

/* l2sap_recvfrom_timeout waits for data from a remote UDP sender, but
 * waits at most timeout seconds.
 * It is possible to pass NULL as timeout, in which case
 * the function waits forever.
 *
 * If a frame arrives in the meantime, it stores the remote
 * peer's address in peer_address and its size in peer_addr_sz.
 * After removing the header, the data of the frame is stored
 * in data, up to len bytes.
 *
 * If data is received, it returns the number of bytes.
 * If no data is reveid before the timeout, it returns L2_TIMEOUT,
 * which has the value 0.
 * It returns -1 in case of error.
 */
// Recieves an L2 frame and extracts the payload from it
int l2sap_recvfrom_timeout( L2SAP* client, uint8_t* data, int len, struct timeval* timeout ) {
    // Parameter validation
    if (!client || !data || len <= 0) {
        fprintf(stderr, "%s ERROR: invalid parameters\n", __FUNCTION__);
        return -1;
    }

    // Set up a file descriptor set for select()
    fd_set readfds;
    FD_ZERO(&readfds);                  // Zero out the set
    FD_SET(client->socket, &readfds);   // Add socket to the set

    // Wait for data to be available (blocking or with timeout)
    int select_result;
    if ((select_result = select(client->socket + 1, &readfds, NULL, NULL, timeout)) < 0) {
        perror("Select failed");
        return -1;
    }
    if (select_result == 0) {
        return L2_TIMEOUT;
    }

    // Allocate a buffer to recieve the full frame
    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);
    uint8_t frame[L2Framesize];
    memset(frame, 0, L2Framesize);

    int received = recvfrom(client->socket, frame, L2Framesize, 0,
                              (struct sockaddr*)&sender_addr, &addr_len);
    
    if (received < 0) {
        fprintf( stderr, "%s: ERROR: recvfrom failed\n", __FUNCTION__ );
        return -1;
    }
    if(received < L2Headersize){
        fprintf( stderr, "%s: ERROR: frame too small\n", __FUNCTION__ );
        return -1;
    }

    // Check if the received frame is larger than the maximum allowed size
    struct L2Header* header = (struct L2Header*)frame;
    uint8_t received_checksum = header->checksum;
    header->checksum = 0;
    uint8_t calculated_checksum = compute_checksum(frame, received);
    
    // Check if the checksum is correct
    if (received_checksum != calculated_checksum) {
        fprintf(stderr, "%s: ERROR: L2 Frame received with incorrect checksum %d, expecting %d. Discarding frame.\n", __FUNCTION__, received_checksum, calculated_checksum);
        return -1;
    }

    // Copy the payload to the provided buffer
    uint16_t payload_len = ntohs(header->len) - L2Headersize;
    if (payload_len > len) {
        fprintf(stderr, "%s: ERROR: payload too large\n", __FUNCTION__);
        return -1;
    }
    memcpy(data, frame + L2Headersize, payload_len);
    return payload_len;
}