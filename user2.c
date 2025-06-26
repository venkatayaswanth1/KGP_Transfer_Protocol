#include<stdio.h>
#include "ksocket.h"
#include<errno.h>
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
    local_addr.sin_port = htons(6000);

    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); 
    remote_addr.sin_port = htons(6001);

    if (k_bind(sockfd, (struct sockaddr *)&local_addr, sizeof(local_addr),
              (struct sockaddr *)&remote_addr, sizeof(remote_addr)) < 0) {
        perror("k_bind");
        k_close(sockfd);
        exit(1);
    }

    FILE *file = fopen("received_file.txt", "wb");
    if (file == NULL) {
        perror("fopen");
        k_close(sockfd);
        exit(1);
    }

    char buffer[MESSAGE_SIZE];
    struct sockaddr_in src_addr;
    socklen_t src_len = sizeof(src_addr);
    ssize_t bytes_received;
    int total_packets = 0;
    unsigned char expected_seq = 0;


    printf("Waiting to receive file...\n");

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        errno = 0;
    
        bytes_received = k_recvfrom(sockfd, buffer, MESSAGE_SIZE, 0,
                                   (struct sockaddr *)&src_addr, &src_len);
    
        if (bytes_received < 0) {
            if (errno == ENOMESSAGE) {
                usleep(50000); 
                continue;
            } else {
                perror("k_recvfrom");
                printf("Errno: %d\n", errno);
                continue;
            }
        }
        
        if (bytes_received >= 10 && strcmp(buffer, "##########") == 0) {
            printf("End of file marker received\n");
            break;
        }
    
        size_t written = fwrite(buffer, 1, bytes_received, file);
        if (written != bytes_received) {
            perror("fwrite");
            printf("Written: %zu, Expected: %zd\n", written, bytes_received);
            break;
        }
        fflush(file);  
        total_packets++;
    
        if (total_packets % 10 == 0) {
            printf("Received %d packets so far\n", total_packets);
        }
    }
    fclose(file);
    printf("File transfer complete. Received %d packets.\n", total_packets);
    k_close(sockfd);
    return 0;
}