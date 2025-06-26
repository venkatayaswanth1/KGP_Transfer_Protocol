#include "ksocket.h"
#include <errno.h>

KTPSocket *shared_memory;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
fd_set read_fds, write_fds;
int max_fd = 0;

int init_shared_memory() {
    key_t key = ftok("ksocket", 'R');
    int shmid = shmget(key, sizeof(KTPSocket) * MAX_KTP_SOCKETS, 0666 | IPC_CREAT);
    if (shmid == -1) {
        perror("shmget");
        exit(1);
    }
    shared_memory = (KTPSocket *)shmat(shmid, NULL, 0);
    if (shared_memory == (void *)-1) {
        perror("shmat");
        exit(1);
    }
    for (int i = 0; i < MAX_KTP_SOCKETS; i++) {
        shared_memory[i].is_free = 1;
    }
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    return 0;
}

void send_ack(int sockfd, unsigned char seq_num, int rwnd_size, int nospace) {
    KTPHeader header = {seq_num, (unsigned char)rwnd_size, 1, (unsigned char)nospace};
    char packet[sizeof(KTPHeader)];
    memcpy(packet, &header, sizeof(KTPHeader));
    sendto(shared_memory[sockfd].udp_socket, packet, sizeof(KTPHeader), 0, 
           (struct sockaddr *)&shared_memory[sockfd].remote_addr, sizeof(struct sockaddr_in));
}

void *receiver_thread(void *arg) {
    while (1) {
        fd_set temp_read_fds = read_fds;
        struct timeval timeout = {T, 0};
        int ready = select(max_fd + 1, &temp_read_fds, NULL, NULL, &timeout);

        if (ready > 0) {
            for (int i = 0; i < MAX_KTP_SOCKETS; i++) {
                if (!shared_memory[i].is_free && FD_ISSET(shared_memory[i].udp_socket, &temp_read_fds)) {
                    char buffer[MESSAGE_SIZE + sizeof(KTPHeader)];
                    struct sockaddr_in src_addr;
                    socklen_t src_len = sizeof(src_addr);
                    ssize_t bytes_received = recvfrom(shared_memory[i].udp_socket, buffer, 
                                             MESSAGE_SIZE + sizeof(KTPHeader), 0, 
                                             (struct sockaddr *)&src_addr, &src_len);
                    if (bytes_received < 0) {
                        perror("recvfrom");
                        continue;
                    }

                    if (dropMessage(P)) {
                        printf("Dropping message \n");
                        continue; 
                    }

                    KTPHeader *header = (KTPHeader *)buffer;
                    
                    if (header->is_ack) {
                        pthread_mutex_lock(&mutex);
                        int found = 0;
                        for (int j = 0; j < shared_memory[i].swnd.size; j++) {
                            if (shared_memory[i].swnd.seq_nums[j] == header->seq_num) {
                                found = 1;
                                for (int k = j; k < shared_memory[i].swnd.size - 1; k++) {
                                    shared_memory[i].swnd.seq_nums[k] = shared_memory[i].swnd.seq_nums[k + 1];
                                    shared_memory[i].swnd.send_times[k] = shared_memory[i].swnd.send_times[k + 1];
                                    memcpy(shared_memory[i].send_buffer[k], shared_memory[i].send_buffer[k + 1], MESSAGE_SIZE);
                                }
                                shared_memory[i].swnd.size--;
                                printf("ACK received for seq %d, swnd size now %d\n", header->seq_num, shared_memory[i].swnd.size);
                                break;
                            }
                        }
                        if (!found) {
                            printf("Received ACK for unknown sequence number %d\n", header->seq_num);
                        }
                        shared_memory[i].rwnd.size = header->rwnd_size;
                        shared_memory[i].nospace_flag = header->is_nospace;
                        pthread_mutex_unlock(&mutex);
                    } else {
                        pthread_mutex_lock(&mutex);
                        unsigned char seq_num = header->seq_num;
                        
                        if (shared_memory[i].recv_buffer_size < BUFFER_SIZE) {
                            int duplicate = 0;
                            for (int j = 0; j < shared_memory[i].rwnd.size; j++) {
                                if (shared_memory[i].rwnd.seq_nums[j] == seq_num) {
                                    duplicate = 1;
                                    break;
                                }
                            }
                            
                            if (!duplicate) {
                                memcpy(shared_memory[i].recv_buffer[shared_memory[i].recv_buffer_size], 
                                       buffer + sizeof(KTPHeader), 
                                       bytes_received - sizeof(KTPHeader));
                                
                                shared_memory[i].rwnd.seq_nums[shared_memory[i].rwnd.size] = seq_num;
                                shared_memory[i].rwnd.size++;
                                shared_memory[i].recv_buffer_size++;
                                shared_memory[i].last_ack_seq = seq_num;
                                
                                printf("Received packet seq %d, recv_buffer_size now %d\n", 
                                      seq_num, shared_memory[i].recv_buffer_size);
                            } else {
                                printf("Duplicate packet seq %d ignored\n", seq_num);
                            }
                            
                            int available_space = BUFFER_SIZE - shared_memory[i].recv_buffer_size;
                            send_ack(i, seq_num, available_space, 0);
                        } else {
                            shared_memory[i].nospace_flag = 1;
                            send_ack(i, shared_memory[i].last_ack_seq, 0, 1);
                            printf("No space in receive buffer, sending NOSPACE ACK\n");
                        }
                        pthread_mutex_unlock(&mutex);
                    }
                }
            }
        } else if (ready == 0) {
            pthread_mutex_lock(&mutex);
            for (int i = 0; i < MAX_KTP_SOCKETS; i++) {
                if (!shared_memory[i].is_free && shared_memory[i].nospace_flag && 
                    shared_memory[i].recv_buffer_size < BUFFER_SIZE) {
                    int available_space = BUFFER_SIZE - shared_memory[i].recv_buffer_size;
                    shared_memory[i].nospace_flag = 0;
                    send_ack(i, shared_memory[i].last_ack_seq, available_space, 0);
                    printf("Space now available in receive buffer, sending ACK\n");
                }
            }
            pthread_mutex_unlock(&mutex);
        }
    }
    return NULL;
}
void *sender_thread(void *arg) {
    struct timespec ts;
    ts.tv_sec = T / 2;
    ts.tv_nsec = (T % 2) * 500000000; 

    while (1) {
        nanosleep(&ts, NULL);
        pthread_mutex_lock(&mutex);
        
        for (int i = 0; i < MAX_KTP_SOCKETS; i++) {
            if (!shared_memory[i].is_free) {
                time_t current_time = time(NULL);
                
                for (int j = 0; j < shared_memory[i].swnd.size; j++) {
                    if (current_time - shared_memory[i].swnd.send_times[j] >= T) {
                        char packet[MESSAGE_SIZE + sizeof(KTPHeader)];
                        KTPHeader header = {
                            shared_memory[i].swnd.seq_nums[j], 
                            0, 
                            0, 
                            0  
                        };
                        
                        memcpy(packet, &header, sizeof(KTPHeader));
                        memcpy(packet + sizeof(KTPHeader), shared_memory[i].send_buffer[j], MESSAGE_SIZE);
                        
                        if (shared_memory[i].udp_socket >= 0) {
                            sendto(shared_memory[i].udp_socket, packet, MESSAGE_SIZE + sizeof(KTPHeader), 0,
                                  (struct sockaddr *)&shared_memory[i].remote_addr, sizeof(struct sockaddr_in));
                            
                            shared_memory[i].swnd.send_times[j] = current_time;
                            printf("Retransmitting packet seq %d\n", header.seq_num);
                        } else {
                            printf("Error: Invalid UDP socket for socket %d\n", i);
                        }
                    }
                }
                
                while (shared_memory[i].swnd.size < BUFFER_SIZE && 
                       shared_memory[i].send_buffer_size > 0 &&
                       shared_memory[i].rwnd.size > 0) { 
                    
                    char packet[MESSAGE_SIZE + sizeof(KTPHeader)];
                    unsigned char next_seq_num = shared_memory[i].next_seq_num;
                    
                    KTPHeader header = {
                        next_seq_num, 
                        0,  
                        0,  
                        0   
                    };
                    
                    memcpy(packet, &header, sizeof(KTPHeader));
                    
                    memcpy(packet + sizeof(KTPHeader), 
                           shared_memory[i].send_buffer[0], 
                           MESSAGE_SIZE);
                    
                    if (shared_memory[i].udp_socket >= 0) {
                        sendto(shared_memory[i].udp_socket, packet, MESSAGE_SIZE + sizeof(KTPHeader), 0,
                              (struct sockaddr *)&shared_memory[i].remote_addr, sizeof(struct sockaddr_in));
                        
                        int idx = shared_memory[i].swnd.size;
                        shared_memory[i].swnd.seq_nums[idx] = next_seq_num;
                        shared_memory[i].swnd.send_times[idx] = current_time;
                        
                        memcpy(shared_memory[i].send_buffer[idx], 
                               shared_memory[i].send_buffer[0],
                               MESSAGE_SIZE);
                        
                        for (int j = 1; j < shared_memory[i].send_buffer_size; j++) {
                            memcpy(shared_memory[i].send_buffer[j-1], 
                                   shared_memory[i].send_buffer[j], 
                                   MESSAGE_SIZE);
                        }
                        
                        shared_memory[i].swnd.size++;
                        shared_memory[i].send_buffer_size--;
                        shared_memory[i].rwnd.size--; 
                        shared_memory[i].next_seq_num = (next_seq_num + 1) % 256; 
                        
                        printf("Sent new packet seq %d, swnd size now %d\n", next_seq_num, shared_memory[i].swnd.size);
                    } else {
                        printf("Error: Invalid UDP socket for socket %d\n", i);
                        break;
                    }
                }
            }
        }
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}


void garbage_collector(int signum) {
    pid_t pid = getpid();
    for (int i = 0; i < MAX_KTP_SOCKETS; i++) {
        if (!shared_memory[i].is_free && shared_memory[i].pid == pid) {
            shared_memory[i].is_free = 1;
            close(shared_memory[i].udp_socket);
            FD_CLR(shared_memory[i].udp_socket, &read_fds);
            if (shared_memory[i].udp_socket == max_fd) {
                max_fd = 0;
                for (int j = 0; j < MAX_KTP_SOCKETS; j++) {
                    if (!shared_memory[j].is_free && shared_memory[j].udp_socket > max_fd) {
                        max_fd = shared_memory[j].udp_socket;
                    }
                }
            }
            break;
        }
    }
}

int k_socket(int domain, int type, int protocol) {
    if (type != SOCK_KTP) {
        errno = EINVAL;
        return -1;
    }

    static pthread_t receiver, sender;
    static int initialized = 0;
    if (!initialized) {
        init_shared_memory();
        pthread_create(&receiver, NULL, receiver_thread, NULL);
        pthread_create(&sender, NULL, sender_thread, NULL);
        signal(SIGTERM, garbage_collector);
        initialized = 1;
    }

    pthread_mutex_lock(&mutex);
    for (int i = 0; i < MAX_KTP_SOCKETS; i++) {
        if (shared_memory[i].is_free) {
            int udp_socket = socket(domain, SOCK_DGRAM, protocol);
            if (udp_socket == -1) {
                pthread_mutex_unlock(&mutex);
                return -1;
            }
            shared_memory[i].is_free = 0;
            shared_memory[i].pid = getpid();
            shared_memory[i].udp_socket = udp_socket;
            shared_memory[i].send_buffer_size = 0;
            shared_memory[i].recv_buffer_size = 0;
            shared_memory[i].swnd.size = 0;
            shared_memory[i].rwnd.size = BUFFER_SIZE;
            shared_memory[i].last_ack_seq = 0;
            shared_memory[i].nospace_flag = 0;
            shared_memory[i].next_seq_num = 0; 
            FD_SET(udp_socket, &read_fds);
            if (udp_socket > max_fd) {
                max_fd = udp_socket;
            }
            pthread_mutex_unlock(&mutex);
            return i; 
        }
    }
    pthread_mutex_unlock(&mutex);
    errno = ENOSPACE;
    return -1;
}
int k_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen, 
           const struct sockaddr *remote_addr, socklen_t remote_addrlen) {
           
    if (sockfd < 0 || sockfd >= MAX_KTP_SOCKETS || shared_memory[sockfd].is_free) {
        errno = EBADF;
        return -1;
    }

    pthread_mutex_lock(&mutex);
    int udp_socket = shared_memory[sockfd].udp_socket;
    if (bind(udp_socket, addr, addrlen) == -1) {
        pthread_mutex_unlock(&mutex);
        return -1;
    }
    memcpy(&shared_memory[sockfd].local_addr, addr, sizeof(struct sockaddr_in));
    memcpy(&shared_memory[sockfd].remote_addr, remote_addr, sizeof(struct sockaddr_in));
    pthread_mutex_unlock(&mutex);
    return 0;
}

ssize_t k_sendto(int sockfd, const void *buf, size_t len, int flags, 
    const struct sockaddr *dest_addr, socklen_t addrlen) {
    
if (sockfd < 0 || sockfd >= MAX_KTP_SOCKETS || shared_memory[sockfd].is_free) {
errno = EBADF;
return -1;
}

pthread_mutex_lock(&mutex);

if (memcmp(dest_addr, &shared_memory[sockfd].remote_addr, sizeof(struct sockaddr_in)) != 0) {
pthread_mutex_unlock(&mutex);
errno = ENOTBOUND;
return -1;
}

if (shared_memory[sockfd].send_buffer_size >= BUFFER_SIZE) {
pthread_mutex_unlock(&mutex);
errno = ENOSPACE;
return -1;
}

memcpy(shared_memory[sockfd].send_buffer[shared_memory[sockfd].send_buffer_size], buf, 
(len > MESSAGE_SIZE) ? MESSAGE_SIZE : len);
shared_memory[sockfd].send_buffer_size++;

pthread_mutex_unlock(&mutex);
return len;
}

ssize_t k_recvfrom(int sockfd, void *buf, size_t len, int flags, 
                   struct sockaddr *src_addr, socklen_t *addrlen) {
                   
    if (sockfd < 0 || sockfd >= MAX_KTP_SOCKETS || shared_memory[sockfd].is_free) {
        errno = EBADF;
        return -1;
    }

    pthread_mutex_lock(&mutex);
    
    if (shared_memory[sockfd].recv_buffer_size == 0) {
        pthread_mutex_unlock(&mutex);
        errno = ENOMESSAGE;
        return -1;
    }

    size_t copy_len = (len < MESSAGE_SIZE) ? len : MESSAGE_SIZE;
    memcpy(buf, shared_memory[sockfd].recv_buffer[0], copy_len);
    
    if (src_addr != NULL && addrlen != NULL) {
        memcpy(src_addr, &shared_memory[sockfd].remote_addr, sizeof(struct sockaddr_in));
        *addrlen = sizeof(struct sockaddr_in);
    }

    for (int i = 1; i < shared_memory[sockfd].recv_buffer_size; i++) {
        memcpy(shared_memory[sockfd].recv_buffer[i - 1], shared_memory[sockfd].recv_buffer[i], MESSAGE_SIZE);
    }
    
    shared_memory[sockfd].recv_buffer_size--;
    shared_memory[sockfd].rwnd.size = BUFFER_SIZE - shared_memory[sockfd].recv_buffer_size;
    
    pthread_mutex_unlock(&mutex);
    return copy_len;
}

int k_close(int sockfd) {
    if (sockfd < 0 || sockfd >= MAX_KTP_SOCKETS || shared_memory[sockfd].is_free) {
        errno = EBADF;
        return -1;
    }

    pthread_mutex_lock(&mutex);
    close(shared_memory[sockfd].udp_socket);
    shared_memory[sockfd].is_free = 1;
    FD_CLR(shared_memory[sockfd].udp_socket, &read_fds);
    
    if (shared_memory[sockfd].udp_socket == max_fd) {
        max_fd = 0;
        for (int i = 0; i < MAX_KTP_SOCKETS; i++) {
            if (!shared_memory[i].is_free && shared_memory[i].udp_socket > max_fd) {
                max_fd = shared_memory[i].udp_socket;
            }
        }
    }
    
    pthread_mutex_unlock(&mutex);
    return 0;
}

int dropMessage(float p) {
    float random = (float)rand() / RAND_MAX;
    return random < p ? 1 : 0;
}