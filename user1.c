#include "ksocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int main() {
    srand(time(NULL));

    int sockfd = k_socket(AF_INET, SOCK_KTP, 0);
    if (sockfd < 0) {
        perror("k_socket");
        exit(1);
    }

    struct sockaddr_in local_addr, remote_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); 
    local_addr.sin_port = htons(6001); 

    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); 
    remote_addr.sin_port = htons(6000); 

    if (k_bind(sockfd, (struct sockaddr *)&local_addr, sizeof(local_addr), 
               (struct sockaddr *)&remote_addr, sizeof(remote_addr)) < 0) {
        perror("k_bind");
        exit(1);
    }

    FILE *file = fopen("large_file.txt", "rb");
    if (file == NULL) {
        perror("fopen");
        k_close(sockfd);
        exit(1);
    }

    char buffer[MESSAGE_SIZE];
    size_t bytes_read;
    int total_transmissions = 0;
    int total_messages = 0;
    
    printf("Sending file...\n");

    while ((bytes_read = fread(buffer, 1, MESSAGE_SIZE, file)) > 0) {
        buffer[bytes_read] = '\0';
        
        int retry_count = 0;
        while (retry_count < 100) {
            ssize_t bytes_sent = k_sendto(sockfd, buffer, MESSAGE_SIZE, 0, 
                                 (struct sockaddr *)&remote_addr, sizeof(remote_addr));
            
            if (bytes_sent < 0) {
                if (errno == ENOSPACE) {
                    usleep(100000); 
                    retry_count++;
                } else {
                    perror("k_sendto");
                    k_close(sockfd);
                    fclose(file);
                    exit(1);
                }
            } else {
                total_transmissions++;
                total_messages++;
                break;
            }
        }
        
        if (retry_count >= 100) {
            fprintf(stderr, "Failed to send after multiple retries\n");
            k_close(sockfd);
            fclose(file);
            exit(1);
        }
        
        usleep(10000); 
    }

    strcpy(buffer, "##########");
    while (k_sendto(sockfd, buffer, strlen(buffer) + 1, 0, 
           (struct sockaddr *)&remote_addr, sizeof(remote_addr)) < 0) {
        if (errno == ENOSPACE) {
            usleep(100000); 
        } else {
            perror("k_sendto EOF marker");
            k_close(sockfd);
            fclose(file);
            exit(1);
        }
    }
    
    sleep(5);

    fclose(file);

    printf("Total transmissions: %d\n", total_transmissions);
    printf("Total messages: %d\n", total_messages);
    printf("Average transmissions per message: %.2f\n", (float)total_transmissions / total_messages);

    k_close(sockfd);
    return 0;
}