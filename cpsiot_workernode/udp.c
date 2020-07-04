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

#define CHANNEL                 11

#define SERVER_MSG_QUEUE_SIZE   (32)
#define SERVER_BUFFER_SIZE      (128)
#define IPV6_ADDRESS_LEN        (46)
#define MAX_IPC_MESSAGE_SIZE    (128)
#define MAX_NEIGHBORS           (8)

#define DEBUG                   0

// External functions defs
extern int ipc_msg_send(char *message, kernel_pid_t destinationPID, bool blocking);
extern int ipc_msg_reply(char *message, msg_t incoming);
extern int ipc_msg_send_receive(char *message, kernel_pid_t destinationPID, msg_t *response, uint16_t type);
extern void substr(char *s, int a, int b, char *t);
extern int indexOfSemi(char *ipv6);
extern void extractIP(char **s, char *t);

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
static sock_udp_t my_sock;
static msg_t msg_u_in, msg_u_out;
int messagesIn = 0;
int messagesOut = 0;
bool runningLE = false;

// State variables
static bool server_running = false;
const int SERVER_PORT = 3142;

// Purpose: if LE is running, count the incoming packet
void countMsgIn(void) {
    if (runningLE) messagesIn += 1;
}

// Purpose: if LE is running, count the outgoing packet
void countMsgOut(void) {
    if (runningLE) messagesOut += 1;
}

// Purpose: main code for the UDP serverS
void *_udp_server(void *args)
{
    msg_init_queue(server_msg_queue, SERVER_MSG_QUEUE_SIZE);

    // variable declarations
    char ipv6[IPV6_ADDRESS_LEN] = { 0 };
	char tempipv6[IPV6_ADDRESS_LEN] = { 0 };
    char masterIP[IPV6_ADDRESS_LEN] = { 0 };
    char myIPv6[IPV6_ADDRESS_LEN] = { 0 };
	char convTime[10] = { 0 };
    char mStr[4] = { 0 };
    int failCount = 0;
    bool discovered = false;
    int i;
    bool topoComplete = false;
    int m;
    int rconf = 0; // did master confirm results received

    char msg_content[MAX_IPC_MESSAGE_SIZE];
    char portBuf[6];

    int numNeighbors = 0;
    char **neighbors = (char**)calloc(MAX_NEIGHBORS, sizeof(char*));

    for(i = 0; i < MAX_NEIGHBORS; i++) {
        neighbors[i] = (char*)calloc(IPV6_ADDRESS_LEN, sizeof(char));
    }

    // socket server setup
    sock_udp_ep_t server = { .port = SERVER_PORT, .family = AF_INET6 };
    sock_udp_ep_t remote;
    kernel_pid_t leaderPID = (kernel_pid_t)atoi(args);

    sprintf(portBuf,"%d",SERVER_PORT);

    // create the socket
    if(sock_udp_create(&my_sock, &server, NULL, 0) < 0) {
        return NULL;
    }

    server_running = true;
    printf("UDP: Success - started UDP server on port %u\n", server.port);

    kernel_pid_t myPid = thread_getpid();
    msg_u_out.type = 0;
    msg_u_out.content.ptr = &myPid;

    if (DEBUG == 1) {
        printf("UDP: EADDRNOTAVAIL = %d\n", EADDRNOTAVAIL);
        printf("UDP: EAGAIN = %d\n", EAGAIN);
        printf("UDP: EINVAL = %d\n", EINVAL);
        printf("UDP: ENOBUFS = %d\n", ENOBUFS);
        printf("UDP: ENOMEM = %d\n", ENOMEM);
        printf("UDP: EPROTO = %d\n", EPROTO);
        printf("UDP: ETIMEDOUT = %d\n", ETIMEDOUT);
    }

    // establish thread communication
    printf("UDP: Trying to communicate with process PID=%" PRIkernel_pid  "\n", leaderPID);
    while (1) {
        int res;
        if (failCount == 10) {
            (void) puts("UDP: Error - timed out on communicating with protocol thread");
            return NULL;
        }

        // wait for protocol thread to initialize    
        res = msg_try_send(&msg_u_out, leaderPID);
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

        xtimer_sleep(50000); // wait 0.05 seconds
    }

    // main server loop
    while (1) {
        // incoming UDP
        int res;
        memset(msg_content, 0, MAX_IPC_MESSAGE_SIZE);
        memset(server_buffer, 0, SERVER_BUFFER_SIZE);

        if (server_buffer == NULL || sizeof(server_buffer) - 1 <= 0) {
            printf("ERROR: failed sock_udp_recv preconditions\n");
        }

        if ((res = sock_udp_recv(&my_sock, server_buffer,
                                 sizeof(server_buffer) - 1, 0.05 * US_PER_SEC, //SOCK_NO_TIMEOUT,
                                 &remote)) < 0) {
            if (res != 0 && res != -ETIMEDOUT && res != -EAGAIN) {
                printf("UDP: Error - failed to receive UDP, %d\n", res);
            }
        }
        else if (res == 0) {
            (void) puts("UDP: no UDP data received");
        }
        else {
            server_buffer[res] = '\0';
            res = 1;
            countMsgIn();
            ipv6_addr_to_str(ipv6, (ipv6_addr_t *)&remote.addr.ipv6, IPV6_ADDRESS_LEN);
            if (DEBUG == 1) {
                printf("UDP: recvd: %s from %s\n", server_buffer, ipv6);
            }
        }

        // react to UDP message
        if (res == 1) {
            // the master is discovering us
            if (strncmp(server_buffer,"ping",4) == 0) {
                // acknowledge them discovering us
                if (!discovered) { 
                    char msg[5] = "pong";
                    char *argsMsg[] = { "udp_send", ipv6, portBuf, msg, NULL };
                    udp_send(4, argsMsg);
                    strcpy(masterIP, ipv6);
                    printf("UDP: discovery attempt from master node (%s)\n", masterIP);
                    if (DEBUG == 1) {
                        printf("UDP: sent UDP message \"%s\" to %s\n", msg, ipv6);
                    }
                }

            // the master acknowledging our acknowledgement
            } else if (strncmp(server_buffer,"conf",4) == 0) {
                // processes confirmation
                discovered = true;
                strcpy(masterIP, ipv6);
                printf("UDP: master node (%s) confirmed us\n", masterIP);

            // information about our IP and neighbors
            } else if (strncmp(server_buffer,"ips:",4) == 0) {
                // process IP and neighbors
                ipc_msg_send(server_buffer, leaderPID, false);

                if (!topoComplete) {
                    char *msg = (char*)calloc(SERVER_BUFFER_SIZE, sizeof(char));
                    char *mem = msg;
                    substr(server_buffer, 4, strlen(server_buffer)-4, msg);

                    if (DEBUG == 1) {                    
                        printf("UDP: server_buffer = %s\n", server_buffer);
                        printf("UDP: msg = %s\n", msg);
                    }

                    extractIP(&msg,mStr);
                    m = atoi(mStr);

                    extractIP(&msg,myIPv6);
                    printf("UDP: My IPv6 is: %s, m=%d\n", myIPv6, m);

                    if (DEBUG == 1) {
                        printf("UDP: before extract while loop\n");
                    }
                    // extract neighbors IPs from message
                    while(strlen(msg) > 1) {
                        if (DEBUG == 1) {
                            printf("UDP: top of extract while, strlen(msg)=%d, msg=%s\n", strlen(msg), msg);
                        }

                        extractIP(&msg,neighbors[numNeighbors]);
                        numNeighbors++;

                        if (DEBUG == 1) {
                            printf("UDP: bottom of extract while, msg=%s, neighbor=%s\n", msg, neighbors[numNeighbors]);
                        }
                    }
                    
                    topoComplete = true;
                    free(mem);
                }

            // start leader election
            } else if (strncmp(server_buffer,"start:",6) == 0) {
                // start leader election
                runningLE = true;
                ipc_msg_send(server_buffer, leaderPID, false);

            // this neighbor is sending us leader election values
            } else if (strncmp(server_buffer,"le_ack",6) == 0 || strncmp(server_buffer,"le_m?",5) == 0) {
                // process m value things
                ipc_msg_send(server_buffer, leaderPID, false);
                if (DEBUG == 1) {
                    printf("UDP: sent IPC message \"%s\" to %" PRIkernel_pid "\n", server_buffer, leaderPID);
                }
            } else if (strncmp(server_buffer,"rconf",5) == 0) {
                // process m value things
                rconf = 1;
                if (DEBUG == 1) {
                    printf("UDP: master confirmed results");
                }
            }
        }

        // incoming thread message
        memset(msg_content, 0, MAX_IPC_MESSAGE_SIZE);
        res = msg_try_receive(&msg_u_in);
        if (res == 1) {
            if (msg_u_in.type > 0 && msg_u_in.type < MAX_IPC_MESSAGE_SIZE) {
                // process string message of size msg_u_in.type
                strncpy(msg_content, (char*)msg_u_in.content.ptr, (uint16_t)msg_u_in.type+1);
                if (DEBUG == 1) {
                    printf("UDP: received IPC message: %s from %" PRIkernel_pid ", type=%d\n", msg_content, msg_u_in.sender_pid, msg_u_in.type);
                }
            } else {
                printf("UPD: received an illegal or too large IPC message, type=%u", msg_u_in.type);
            }
        }

        // react to thread message
        if (res == 1) {
            // start a leader election run
            if (strncmp(msg_content,"le_init",7) == 0) {
                // send out m? queries
                char msg[7] = "le_m?:";
                runningLE = true;

                for(i = 0; i < numNeighbors; i++) {
                    char *argsMsg[] = { "udp_send", neighbors[i], portBuf, msg, NULL };
                    udp_send(4, argsMsg);
                    xtimer_usleep(10000); // wait 0.01 seconds
                }

                if (DEBUG == 1) {
                    printf("UDP: sent UDP message \"%s\" to %d neighbors\n", msg, numNeighbors);
                }
    
            // send out an m value acknowledgement
            } else if (strncmp(msg_content,"le_ack",6) == 0) {
                // send out m value
                for(i = 0; i < numNeighbors; i++) {
                    char *argsMsg[] = { "udp_send", neighbors[i], portBuf, msg_content, NULL };
                    udp_send(4, argsMsg);
                    xtimer_usleep(10000); // wait 0.01 seconds
                }

                if (DEBUG == 1) {
                    printf("UDP: sent UDP message \"%s\" to %d neighbors\n", msg_content, numNeighbors);
                }

            // leader election complete, print network stats
            } else if (strncmp(msg_content,"results",7) == 0 && rconf == 0) {
                // leader election finished!
                printf("UDP: leader election complete, msgsIn: %d, msgsOut: %d, msgsTotal: %d\n", messagesIn, messagesOut, messagesIn + messagesOut);

                // send information to the master node
				char *msg = (char*)malloc(SERVER_BUFFER_SIZE);
                memset(msg, 0, SERVER_BUFFER_SIZE);
                char *mem = msg;

                //chop off the results string
				substr(msg_content, 8, strlen(msg_content)-8, msg);
                if (DEBUG == 1) {
                    printf("UDP: results body: %s\n", msg);
                }

				//Extract the elected IP
				extractIP(&msg, tempipv6);
                if (DEBUG == 1) {
                    printf("UDP: extracted leader %s\n", tempipv6);
                }

				//Extract the convergance time
				extractIP(&msg, convTime);
                if (DEBUG == 1) {
                    printf("UDP: extracted convergence time: %s\n", convTime);
                }

				//Setup message to send to master node
				//Form is "results;<elected_leader_id>;<runtime>;<message_count>;"
                free(mem);
				char msg2[SERVER_BUFFER_SIZE] = "results:";
                
                strcat(msg2, tempipv6);
                strcat(msg2, ";");
                strcat(msg2, convTime);//convTime is already a string from the other thread
                strcat(msg2, ";");
				
				int totalMessages = messagesIn + messagesOut;
				//TODO convert totalMessages to char array
				char tempMessages[10];
				sprintf(tempMessages , "%d" , totalMessages);
                strcat(msg2, tempMessages);
                strcat(msg2, ";");
                if (DEBUG == 1) {
                    printf("UDP: sending results to master: %s\n", msg2);
                }
                char *argsMsg[] = { "udp_send", masterIP, portBuf, msg2, NULL };
                udp_send(4, argsMsg);
                xtimer_usleep(1500000); // wait 1.5 seconds
            }
        }

        xtimer_usleep(50000); // wait 0.05 seconds
    }

    return NULL;
}

// Purpose: send a message to a specific target
//
// argc int, number of arguments (should be 4)
// argv char**, list of arugments ("udp", <target-ipv6>, <port>, <message>)
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
        if (DEBUG == 1) {
            printf("UDP: Success - sent %u bytes to %s\n", (unsigned) res, argv[1]);
        }
        countMsgOut();
    }
    return 0;
}

// Purpose: send out a multicast message
//
// argc int, number of arguments (should be 3)
// argv char**, list of arugments ("udp", <port>, <message>)
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
        if (DEBUG == 1) {
            printf("UDP: Success - sent %u bytes to %s\n", (unsigned)res, ipv6);
        }
        countMsgOut();
    }
    return 0;
}

// Purpose: creates the UDP server thread
//
// argc int, number of arguments (should be 2)
// argv char**, list of arguments ("udps", <thread-pid>)
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
