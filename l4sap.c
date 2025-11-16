#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include "l4sap.h"
#include "l2sap.h"

/* Create an L4 client.
 * It returns a dynamically allocated struct L4SAP that contains the
 * data of this L4 entity (including the pointer to the L2 entity
 * used).
 */
L4SAP* l4sap_create( const char* server_ip, int server_port ) {
    // Log creation attempt
    fprintf(stderr, "%s: Creating L4SAP for %s:%d\n", __FUNCTION__, server_ip, server_port);

    // Validate input parameters
    if (!server_ip || server_port < 1024) {
        fprintf(stderr, "%s: ERROR: invalid server_ip or port\n", __FUNCTION__);
        return NULL;
    }

    // Allocate memory for L4SAP structure
    L4SAP* l4 = (L4SAP*)malloc(sizeof(L4SAP));
    if (l4 == NULL) {
        fprintf(stderr, "%s: ERROR: malloc failed\n", __FUNCTION__);
        return NULL;
    }

    // Create underlying L2SAP instance
    l4->l2 = l2sap_create(server_ip, server_port);
    if (!l4->l2) {
        fprintf(stderr, "%s: ERROR: l2sap_create failed\n", __FUNCTION__);
        free(l4);
        return NULL;
    }

    // Initialize L4SAP fields
    l4->send_seqno = 0;
    l4->expected_seqno = 0;
    l4->pending_data = 0;
    l4->pending_pl_len = 0;

    fprintf(stderr, "%s: L4SAP created successfully\n", __FUNCTION__);
    return l4;
}

/* The functions sends a packet to the network. The packet's payload
 * is copied from the buffer that it is passed as an argument from
 * the caller at L5.
 * If the length of that buffer, which is indicated by len, is larger
 * than L4Payloadsize, the function truncates the message to L4Payloadsize.
 *
 * The function does not return until the correct ACK from the peer entity
 * has been received.
 * When a suitable ACK arrives, the function returns the number of bytes
 * that were accepted for sending (the potentially truncated packet length).
 *
 * Waiting for a correct ACK may fail after a timeout of 1 second
 * (timeval.tv_sec = 1, timeval.tv_usec = 0). The function retransmits
 * the packet in that case.
 * The function attempts up to 4 retransmissions. If the last retransmission
 * fails with a timeout as well, the function returns L4_SEND_FAILED.
 *
 * The function may also return:
 * - L4_QUIT if the peer entity has sent an L4_RESET packet.
 * - another value < 0 if an error occurred.
 */
int l4sap_send( L4SAP* l4, const uint8_t* data, int len ) {
    // Log entry with data length
    fprintf(stderr, "%s: Entering with len=%d\n", __FUNCTION__, len);

    // Validate input parameters
    if (!l4 || !l4->l2 || !data || len <= 0) {
        fprintf(stderr, "%s: ERROR: invalid parameters\n", __FUNCTION__);
        return L4_SEND_FAILED;
    }

    // Truncate payload if it exceeds L4Payloadsize
    if (len > L4Payloadsize) len = L4Payloadsize;

    uint8_t packet[L4Framesize]; // Buffer for packet
    memset(packet, 0, L4Framesize);
    struct L4Header* header = (struct L4Header*)packet; // Packet header

    header->type = L4_DATA;          // Set packet type to data
    header->seqno = l4->send_seqno;  // Set sequence number
    header->ackno = 0;               // Acknowledgment number (not used for L4_DATA)
    header->mbz = 0;                 // Must-be-zero field
    memcpy(packet + sizeof(*header), data, len); // Copy payload

    // Set timeout for receiving ACK
    struct timeval timeout = {1, 0};
    uint8_t recv_buffer[L4Framesize];
    int max_retries = 5;

    // Retry sending packet up to max_retries times
    for (int attempt = 0; attempt < max_retries; attempt++) {
        // Send packet via L2SAP
        int sent = l2sap_sendto(l4->l2, packet, len + sizeof(*header));
        if (sent < 0) {
            fprintf(stderr, "%s: ERROR: l2sap_sendto failed\n", __FUNCTION__);
            return L4_SEND_FAILED;
        }
        fprintf(stderr, "%s: Sent %d bytes, attempt %d\n", __FUNCTION__, sent, attempt + 1);

        // Wait for ACK or other packets
        int recv_len = l2sap_recvfrom_timeout(l4->l2, recv_buffer, L4Framesize, &timeout);
        if (recv_len == L2_TIMEOUT) {
            // Handle timeout
            fprintf(stderr, "%s: Timeout on attempt %d\n", __FUNCTION__, attempt + 1);
            continue;
        }
        if (recv_len < 0) {
            // Handle unexpected errors
            fprintf(stderr, "%s: ERROR: l2sap_recvfrom_timeout returned error (%d)\n", __FUNCTION__, recv_len);
            continue;
        }
        if (recv_len < sizeof(*header)) {
            // Ignore invalid packets
            fprintf(stderr, "%s: Received invalid packet (%d bytes)\n", __FUNCTION__, recv_len);
            continue;
        }

        // Process received packet
        struct L4Header* recv_header = (struct L4Header*)recv_buffer;
        if (recv_header->type == L4_RESET) {
            // Handle reset packet
            fprintf(stderr, "%s: Received L4_RESET\n", __FUNCTION__);
            return L4_QUIT;
        }
        if (recv_header->type == L4_ACK) {
            // Check if ACK matches expected acknowledgment number
            if (recv_header->ackno == (1 - l4->send_seqno)) {
                fprintf(stderr, "%s: Success, GOOD ACK ackno=%d\n", __FUNCTION__, recv_header->ackno);
                l4->send_seqno = 1 - l4->send_seqno; // Toggle sequence number
                return len; // Return number of bytes sent
            } else {
                fprintf(stderr, "%s: BAD ACK ackno=%d, ignoring\n", __FUNCTION__, recv_header->ackno);
                continue;
            }
        }

        if (recv_header->type == L4_DATA) {
            // Unexpected DATA while waiting for ACK
            int payload_len = recv_len - sizeof(*header);
            if (payload_len > L4Payloadsize) payload_len = L4Payloadsize;

            char payload_str[L4Payloadsize + 1];
            memcpy(payload_str, recv_buffer + sizeof(*header), payload_len);
            payload_str[payload_len] = '\0';
            fprintf(stderr, "%s: ERROR: received unexpected data: '%s'\n", __FUNCTION__, payload_str);

            // Send ACK for unexpected DATA
            uint8_t ack_packet[sizeof(*header)];
            struct L4Header* ack_header = (struct L4Header*)ack_packet;
            ack_header->type = L4_ACK;
            ack_header->seqno = 0; 
            ack_header->ackno = 1 - recv_header->seqno;
            ack_header->mbz = 0;
            l2sap_sendto(l4->l2, ack_packet, sizeof(*header));

            // Store pending data for later processing
            if (!l4->pending_data) {
                l4->pending_data = 1;
                l4->pending_header = *recv_header;
                l4->pending_pl_len = payload_len;
                memcpy(l4->pending_pl_buffer, recv_buffer + sizeof(*header), payload_len);
            }
            continue;
        }
    }

    fprintf(stderr, "%s: ERROR: Max send retries reached\n", __FUNCTION__);
    return L4_SEND_FAILED; 
}

/* The functions receives a packet from the network. The packet's
 * payload is copy into the buffer that it is passed as an argument
 * from the caller at L5.
 * The function blocks endlessly, meaning that experiencing a timeout
 * does not terminate this function.
 * The function returns the number of bytes copied into the buffer
 * (only the payload of the L4 packet).
 * The function may also return:
 * - L4_QUIT if the peer entity has sent an L4_RESET packet.
 * - another value < 0 if an error occurred.
 */
int l4sap_recv( L4SAP* l4, uint8_t* data, int len ) {
    // Log entry with buffer length
    fprintf(stderr, "%s: Entering with len=%d\n", __FUNCTION__, len);

    // Validate input parameters
    if (!l4 || !l4->l2 || !data || len <= 0) {
        fprintf(stderr, "%s: ERROR: invalid parameters\n", __FUNCTION__);
        return -1;
    }

    // Check if there is pending data from previous reception
    if (l4->pending_data) {
        struct L4Header* hdr = &l4->pending_header;
        if (hdr->type == L4_DATA && hdr->seqno == l4->expected_seqno) {
            int copy_len = (l4->pending_pl_len < len) ? l4->pending_pl_len : len;
            memcpy(data, l4->pending_pl_buffer, copy_len);

            uint8_t ack_packet[sizeof(*hdr)];
            struct L4Header* ack_header = (struct L4Header*)ack_packet;
            ack_header->type = L4_ACK;
            ack_header->seqno = 0;
            ack_header->mbz = 0;
            ack_header->ackno = 1 - hdr->seqno;
            l2sap_sendto(l4->l2, ack_packet, sizeof(*hdr));

            l4->expected_seqno = 1 - l4->expected_seqno;
            l4->pending_data = 0;
            return copy_len;
        }

        // If pending data is not valid, send ACK for the last received packet
        uint8_t ack_packet[sizeof(*hdr)];
        struct L4Header* ack_header = (struct L4Header*)ack_packet;
        ack_header->type = L4_ACK;
        ack_header->seqno = 0;
        ack_header->mbz = 0;
        ack_header->ackno = 1 - hdr->seqno;
        l2sap_sendto(l4->l2, ack_packet, sizeof(*hdr));
        l4->pending_data = 0;
    }

    // Retry receiving until data is accepted or max retries reached
    uint8_t packet[L4Framesize];
    memset(packet, 0, L4Framesize);
    while (1) {
        int recv_len = l2sap_recvfrom_timeout(l4->l2, packet, L2Framesize, NULL);
        if (recv_len == L2_TIMEOUT) {
            continue;
        }
        if (recv_len < sizeof(struct L4Header)) {
            // Ignore invalid packets
            fprintf(stderr, "%s: Received invalid packet (%d bytes)\n", __FUNCTION__, recv_len);
            continue;
        }

        if (recv_len < sizeof(struct L4Header)) {
            fprintf(stderr, "%s: Received too short packet (%d bytes)\n", __FUNCTION__, recv_len);
            continue;  
        }

        // Process received packet
        struct L4Header* header = (struct L4Header*)packet;
        int payload_len = recv_len - sizeof(*header);

        if (header->mbz != 0) {
            // Ignore packets with non-zero mbz
            fprintf(stderr, "%s: Ignoring packet with non-zero mbz\n", __FUNCTION__);
            continue;
        }

        if (header->type == L4_RESET) {
            // Handle reset packet
            fprintf(stderr, "%s: Received L4_RESET\n", __FUNCTION__);
            return L4_QUIT;
        }
        if (header->type == L4_DATA) {
            // Send ACK for L4_DATA
            uint8_t ack_packet[sizeof(*header)];
            struct L4Header* ack_header = (struct L4Header*)ack_packet;
            ack_header->type = L4_ACK;
            ack_header->seqno = 0;
            ack_header->mbz = 0;

            // Check if L4_DATA has expected sequence number
            if (header->seqno == l4->expected_seqno) {
                int copy_len = payload_len;
                memcpy(data, packet + sizeof(*header), copy_len); // Copy payload

                ack_header->ackno = 1 - header->seqno;
                l2sap_sendto(l4->l2, ack_packet, sizeof(*header));

                l4->expected_seqno = 1 - l4->expected_seqno;
                return copy_len; // Return number of bytes received
            } else {
                ack_header->ackno = 1 - header->seqno;
                l2sap_sendto(l4->l2, ack_packet, sizeof(*header));
                continue;
            }
        }
    }

    fprintf(stderr, "%s: ERROR: Max receive retries reached\n", __FUNCTION__);
    return L4_QUIT;
}

/** This function is called to terminate the L4 entity and
 *  free all of its resources.
 *  We recommend that you send several L4_RESET packets from
 *  this function to ensure that the peer entity is also
 *  terminating correctly.
 */
void l4sap_destroy( L4SAP* l4 ) {
    // Log destruction attempt
    fprintf(stderr, "%s: Destroying L4SAP\n", __FUNCTION__);

    // Check for NULL pointers
    if (!l4 || !l4->l2) {
        fprintf(stderr, "%s: WARNING: l4 or l4->l2 is NULL\n", __FUNCTION__);
        return;
    }

    // Clean up L2SAP and L4SAP
    l2sap_destroy(l4->l2);
    l4->l2 = NULL; // Prevent double-free
    free(l4);
    fprintf(stderr, "%s: L4SAP destroyed\n", __FUNCTION__);
}