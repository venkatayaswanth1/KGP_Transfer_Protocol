#ifndef KSOCKET_H
#define KSOCKET_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <signal.h>
#include <sys/select.h>
#include <time.h>

#define SOCK_KTP 10
#define MAX_KTP_SOCKETS 100
#define MESSAGE_SIZE 512
#define BUFFER_SIZE 10
#define ENOSPACE 1
#define ENOTBOUND 2
#define ENOMESSAGE 3
#define T 5 
#define P 0.05

typedef struct {
    unsigned char seq_num;
    unsigned char rwnd_size;
    unsigned char is_ack;
    unsigned char is_nospace;
} KTPHeader;

typedef struct {
    int is_free;
    pid_t pid;
    int udp_socket;
    struct sockaddr_in local_addr;
    struct sockaddr_in remote_addr;
    char send_buffer[BUFFER_SIZE][MESSAGE_SIZE];
    char recv_buffer[BUFFER_SIZE][MESSAGE_SIZE];
    int send_buffer_size;
    int recv_buffer_size;
    struct {
        int size;
        unsigned char seq_nums[BUFFER_SIZE];
        time_t send_times[BUFFER_SIZE];
    } swnd;
    struct {
        int size;
        unsigned char seq_nums[BUFFER_SIZE];
    } rwnd;
    unsigned char last_ack_seq;
    int nospace_flag;
    
    unsigned char received_seq[256];  
    unsigned char next_seq_num;       
} KTPSocket;

int k_socket(int domain, int type, int protocol);
int k_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen, const struct sockaddr *remote_addr, socklen_t remote_addrlen);
ssize_t k_sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t k_recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
int k_close(int sockfd);
int dropMessage(float p);

int init_shared_memory();
#endif 