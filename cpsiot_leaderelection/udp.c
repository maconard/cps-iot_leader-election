/*
 * Original sample file taken from: https://github.com/RIOT-OS/Tutorials/tree/master/task-06
 *
 * All changes and final product:
 * @author  Michael Conard <maconard@mtu.edu>
 *
 * Purpose: A UDP server designed to work with my leader election protocol.
 */

// Standard C includes
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <msg.h>

// Standard RIOT includes
#include "thread.h"
#include "xtimer.h"

// Networking includes
#include "net/sock/udp.h"
#include "net/ipv6/addr.h"

#define SERVER_MSG_QUEUE_SIZE   (128)
#define SERVER_BUFFER_SIZE      (64)
#define IPV6_ADDRESS_LEN        (46)
#define MAX_IPC_MESSAGE_SIZE    (256)

#define DEBUG	(0)

// External functions defs
extern int ipc_msg_send(char *message, kernel_pid_t destinationPID, bool blocking);
extern int ipc_msg_reply(char *message, msg_t incoming);
extern int ipc_msg_send_receive(char *message, kernel_pid_t destinationPID, msg_t *response, uint16_t type);
extern void substr(char *s, int a, int b, char *t);

// Forward declarations
void *_udp_server(void *args);
int udp_send(int argc, char **argv);
int udp_send_multi(int argc, char **argv);
int udp_server(int argc, char **argv);
void countMsgOut(void);
void countMsgIn(void);

// Data structures (i.e. stacks, queues, message structs, etc)
static char server_buffer[SERVER_BUFFER_SIZE];
static char server_stack[THREAD_STACKSIZE_DEFAULT];
static msg_t server_msg_queue[SERVER_MSG_QUEUE_SIZE];
static sock_udp_t sock;
static msg_t msg_u_in, msg_u_out;
int messagesIn = 0;
int messagesOut = 0;
bool runningLE = false;

// State variables
static bool server_running = false;
const int SERVER_PORT = 3142;

void countMsgIn(void) {
    if (runningLE) messagesIn += 1;
}

void countMsgOut(void) {
    if (runningLE) messagesOut += 1;
}

void *_udp_server(void *args)
{
    //printf("UDP: Entered UDP server code\n");
    sock_udp_ep_t server = { .port = SERVER_PORT, .family = AF_INET6 };
    sock_udp_ep_t remote;
    msg_init_queue(server_msg_queue, SERVER_MSG_QUEUE_SIZE);
    kernel_pid_t leaderPID = (kernel_pid_t)atoi(args);
    int failCount = 0;
    char ipv6[IPV6_ADDRESS_LEN] = { 0 };
    char myIPv6[IPV6_ADDRESS_LEN] = { 0 };

    if(sock_udp_create(&sock, &server, NULL, 0) < 0) {
        return NULL;
    }

    server_running = true;
    printf("UDP: Success - started UDP server on port %u\n", server.port);

    char msg_content[MAX_IPC_MESSAGE_SIZE];
    //sprintf(msg_content, "%u", thread_getpid());
    kernel_pid_t myPid = thread_getpid();
    msg_u_out.type = 0;
    msg_u_out.content.ptr = &myPid;

    printf("UDP: Trying to communicate with process PID=%" PRIkernel_pid  "\n", leaderPID);
    while (1) {
        if (failCount == 10) {
            (void) puts("UDP: Error - timed out on communicating with protocol thread");
            return NULL;
        }

        // wait for protocol thread to initialize    
        int res = msg_try_send(&msg_u_out, leaderPID);
        if (res == -1) {
            // msg failed because protocol thread doesn't exist or we have the wrong PID
            (void) puts("UDP: Error - UDP server thread can't communicate with protocol thread");
            failCount++;
        } else if (res == 0) {
            // msg failed because protocol thread isn't ready to receive yet
            failCount++;
        } else if (res == 1) {
            // msg succeeded
            printf("UDP: thread successfully initiated communication with the PID=%" PRIkernel_pid  "\n", leaderPID);
            break;
        }

        xtimer_sleep(200000); // wait 0.2 seconds
    }

    while (1) {
        // incoming UDP
        int res;
        memset(msg_content, 0, MAX_IPC_MESSAGE_SIZE);
        memset(server_buffer, 0, SERVER_BUFFER_SIZE);

        if ((res = sock_udp_recv(&sock, server_buffer,
                                 sizeof(server_buffer) - 1, 0, //SOCK_NO_TIMEOUT,
                                 &remote)) < 0) {
            if(res != 0 && res != -ETIMEDOUT && res != -EAGAIN) 
                printf("UDP: Error - failed to receive UDP, %d\n", res);
        }
        else if (res == 0) {
            (void) puts("UDP: no UDP data received");
        }
        else {
            server_buffer[res] = '\0';
            res = 1;
			countMsgIn();
            ipv6_addr_to_str(ipv6, (ipv6_addr_t *)&remote.addr.ipv6, IPV6_ADDRESS_LEN);
            if (DEBUG == 1) 
				printf("UDP: recvd: %s from %s\n", server_buffer, ipv6);
        }

        // react to UDP message
        if (res == 1) {
            if (strncmp(server_buffer,"nd_init",7) == 0) {
				// respond to neighbor request
                char port[5];
                sprintf(port, "%d", SERVER_PORT);
                char msg[MAX_IPC_MESSAGE_SIZE] = "nd_ack:";
                char *argsMsg[] = { "udp_send", ipv6, port, msg, NULL };
                udp_send(4, argsMsg);
                if (DEBUG == 1) 
					printf("UDP: sent UDP message \"%s\" to %s\n", msg, ipv6);

				xtimer_usleep(20000); // wait 0.02 seconds

                char msg2[MAX_IPC_MESSAGE_SIZE] = "nd_hello:";
				strcat(msg2,ipv6);
                char *argsMsg2[] = { "udp_send", ipv6, port, msg2, NULL };
                udp_send(4, argsMsg2);
                if (DEBUG == 1) 
					printf("UDP: sent UDP message \"%s\" to %s\n", msg2, ipv6);

				// processes new neighbor
                //strcpy(msg,"nd_ack:");
                //strcat(msg, ipv6);
                //ipc_msg_send(msg, leaderPID, false);
                //printf("UDP: sent IPC message \"%s\" to %" PRIkernel_pid "\n", msg, leaderPID);
            } else if (strncmp(server_buffer,"nd_ack",6) == 0) {
                // processes new neighbor
                char msg[MAX_IPC_MESSAGE_SIZE] = "nd_ack:";
                strcat(msg, ipv6);
                ipc_msg_send(msg, leaderPID, false);
                if (DEBUG == 1) 
					printf("UDP: sent IPC message \"%s\" to %" PRIkernel_pid "\n", msg, leaderPID);
            } else if (strlen(myIPv6) == 0 && strncmp(server_buffer, "nd_hello:", 9) == 0){
                // ip address update
                substr(server_buffer, 9, IPV6_ADDRESS_LEN, myIPv6);
                printf("UDP: My IP is %s\n", myIPv6);
                msg_u_out.content.ptr = &myIPv6;
                msg_u_out.type = 1;
                msg_try_send(&msg_u_out, leaderPID);
                countMsgOut();
                //ipc_msg_send(myIPv6, leaderPID, false);

            } else if (strncmp(server_buffer,"le_ack",6) == 0 || strncmp(server_buffer,"le_m?",5) == 0) {
                // process m value things
                ipc_msg_send(server_buffer, leaderPID, false);
                if (DEBUG == 1) 
					printf("UDP: sent IPC message \"%s\" to %" PRIkernel_pid "\n", server_buffer, leaderPID);
                
            }
        }

        // incoming thread message
        memset(msg_content, 0, MAX_IPC_MESSAGE_SIZE);
        res = msg_try_receive(&msg_u_in);
        if (res == 1) {
            if (msg_u_in.type > 0 && msg_u_in.type < MAX_IPC_MESSAGE_SIZE) {
                // process string message of size msg_u_in.type
                strncpy(msg_content, (char*)msg_u_in.content.ptr, (uint16_t)msg_u_in.type+1);
                if (DEBUG == 1) 
					printf("UDP: received IPC message: %s from %" PRIkernel_pid ", type=%d\n", msg_content, msg_u_in.sender_pid, msg_u_in.type);
            } else {
                printf("UPD: received an illegal or too large IPC message, type=%u", msg_u_in.type);
            }
        }

        // react to thread message
        if (res == 1) {
            if (strncmp(msg_content,"nd_init",7) == 0) {
                // send multicast neighbor discovery
                char port[5];
                sprintf(port, "%d", SERVER_PORT);
                char *argsMsg[] = { "udp_send_multi", port, msg_content, NULL };
                udp_send_multi(3, argsMsg);
                if (DEBUG == 1) 
					printf("UDP: sent UDP message \"%s\" to multicast\n", msg_content);

            } else if (strncmp(msg_content,"nd_hello:",9) == 0) {
                // send targeted neighbor hello
                char port[5];
                sprintf(port, "%d", SERVER_PORT);
                substr(msg_content, 9, IPV6_ADDRESS_LEN, ipv6);
                
                char *argsMsg[] = { "udp_send", ipv6, port, msg_content, NULL };
                udp_send(4, argsMsg);
                if (DEBUG == 1) 
					printf("UDP: sent UDP message \"%s\" to %s\n", msg_content, ipv6);

            } else if (strncmp(msg_content,"le_init",7) == 0) {
                // send out m? queries
                runningLE = true;
                char port[5];
                sprintf(port, "%d", SERVER_PORT);
                char msg[7] = "le_m?:";
                char *argsMsg[] = { "udp_send_multi", port, msg, NULL };
                udp_send_multi(3, argsMsg);
                if (DEBUG == 1) 
					printf("UDP: sent UDP message \"%s\" to multicast\n", msg);
    
            } else if (strncmp(msg_content,"le_ack",6) == 0) {
                // send out m value
                char port[5];
                sprintf(port, "%d", SERVER_PORT);
                char *argsMsg[] = { "udp_send_multi", port, msg_content, NULL };
                udp_send_multi(3, argsMsg);
                if (DEBUG == 1) 
					printf("UDP: sent UDP message \"%s\" to multicast\n", msg_content);

            } else if (strncmp(msg_content,"le_done",7) == 0) {
                // leader election finished!
                printf("UDP: leader election complete, msgsIn: %d, msgsOut: %d, msgsTotal: %d\n", messagesIn, messagesOut, messagesIn + messagesOut);
            }
        }

        xtimer_usleep(50000); // wait 0.05 seconds
    }

    return NULL;
}

int udp_send(int argc, char **argv)
{
    int res;
    sock_udp_ep_t remote = { .family = AF_INET6 };

    if (argc != 4) {
        (void) puts("UDP: Usage - udp <ipv6-addr> <port> <payload>");
        return -1;
    }

    if (ipv6_addr_from_str((ipv6_addr_t *)&remote.addr, argv[1]) == NULL) {
        (void) puts("UDP: Error - unable to parse destination address");
        return 1;
    }
    if (ipv6_addr_is_link_local((ipv6_addr_t *)&remote.addr)) {
        /* choose first interface when address is link local */
        gnrc_netif_t *netif = gnrc_netif_iter(NULL);
        remote.netif = (uint16_t)netif->pid;
    }
    remote.port = atoi(argv[2]);
    if((res = sock_udp_send(NULL, argv[3], strlen(argv[3]), &remote)) < 0) {
        printf("UDP: Error - could not send message \"%s\" to %s\n", argv[3], argv[1]);
    }
    else {
        if (DEBUG == 1) 
			printf("UDP: Success - sent %u bytes to %s\n", (unsigned) res, argv[1]);
		countMsgOut();
    }
    return 0;
}

int udp_send_multi(int argc, char **argv)
{
    //multicast: FF02::1
    int res;
    sock_udp_ep_t remote = { .family = AF_INET6 };
    char ipv6[IPV6_ADDRESS_LEN] = { 0 };

    if (argc != 3) {
        (void) puts("UDP: Usage - udp <port> <payload>");
        return -1;
    }

    ipv6_addr_set_all_nodes_multicast((ipv6_addr_t *)&remote.addr.ipv6, IPV6_ADDR_MCAST_SCP_LINK_LOCAL);

    if (ipv6_addr_is_link_local((ipv6_addr_t *)&remote.addr)) {
        /* choose first interface when address is link local */
        gnrc_netif_t *netif = gnrc_netif_iter(NULL);
        remote.netif = (uint16_t)netif->pid;
    }
    remote.port = atoi(argv[1]);
    ipv6_addr_to_str(ipv6, (ipv6_addr_t *)&remote.addr.ipv6, IPV6_ADDRESS_LEN);
    if((res = sock_udp_send(NULL, argv[2], strlen(argv[2]), &remote)) < 0) {
        printf("UDP: Error - could not send message \"%s\" to %s\n", argv[2], ipv6);
    }
    else {
        if (DEBUG == 1) 
			printf("UDP: Success - sent %u bytes to %s\n", (unsigned)res, ipv6);
		countMsgOut();
    }
    return 0;
}

int udp_server(int argc, char **argv)
{
    if (argc != 2) {
        puts("MAIN: Usage - udps <thread_pid>");
        return -1;
    }

    if ((server_running == false) &&
        thread_create(server_stack, sizeof(server_stack), THREAD_PRIORITY_MAIN - 1,
                      THREAD_CREATE_STACKTEST, _udp_server, argv[1], "UDP_Server_Thread")
        <= KERNEL_PID_UNDEF) {
        printf("MAIN: Error - failed to start UDP server thread\n");
        return -1;
    }

    return 0;
}
