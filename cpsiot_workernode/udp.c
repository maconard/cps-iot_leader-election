/*
 * @author  Michael Conard <maconard@mtu.edu>
 *
 * Purpose: A UDP socket server based leader election protocol.
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

// Size definitions
#define CHANNEL                 11
#define SERVER_MSG_QUEUE_SIZE   (16)
#define SERVER_BUFFER_SIZE      (128)
#define IPV6_ADDRESS_LEN        (46)
#define MAX_NEIGHBORS           (6)

// Leader Election values
#ifndef LE_K
    #define LE_K    (5)
#endif
#ifndef LE_T
    #define LE_T    (3.00*1000000)
#endif

#define DEBUG       (1)

// External functions defs
extern int ipc_msg_send(char *message, kernel_pid_t destinationPID, bool blocking);
extern int ipc_msg_reply(char *message, msg_t incoming);
extern int ipc_msg_send_receive(char *message, kernel_pid_t destinationPID, msg_t *response, uint16_t type);
extern void substr(char *s, int a, int b, char *t);
extern int indexOfSemi(char *ipv6);
extern void extractMsgSegment(char **s, char *t);

// Forward declarations
void *_udp_server(void *args);
int udp_send(int argc, char **argv);
int udp_send_multi(int argc, char **argv);
int udp_server(int argc, char **argv);
void countMsgOut(void);
void countMsgIn(void);
int alreadyANeighbor(char **neighbors, char *ipv6);
int getNeighborIndex(char **neighbors, char *ipv6);
int minIPv6(char *ipv6_a, char *ipv6_b);

// Data structures (i.e. stacks, queues, message structs, etc)
static char server_buffer[SERVER_BUFFER_SIZE];
static char server_stack[THREAD_STACKSIZE_DEFAULT];
static msg_t server_msg_queue[SERVER_MSG_QUEUE_SIZE];
static sock_udp_t my_sock;
//static msg_t msg_u_in, msg_u_out;
int messagesIn = 0;
int messagesOut = 0;
bool runningLE = false;

// State variables
static bool server_running = false;
const int SERVER_PORT = 3142;

// Purpose: if LE is running, count the incoming packet
void countMsgIn(void) {
    if (runningLE) {
        messagesIn += 1;
    }
}

// Purpose: if LE is running, count the outgoing packet
void countMsgOut(void) {
    if (runningLE) {
        messagesOut += 1;
    }
}

// Purpose: determine if an ipv6 address is already registered
//
// neighbors char**, list of registered neighbors
// ipv6 char*, the address to check for
int alreadyANeighbor(char **neighbors, char *ipv6) {
    for(int i = 0; i < MAX_NEIGHBORS; i++) {
        if(strcmp(neighbors[i], ipv6) == 0) return 1;
    }
    return 0;
}

// Purpose: retrieve the internal index of an address
//
// neighbors char**, list of registered neighbors
// ipv6 char*, ipv6 to check for
int getNeighborIndex(char **neighbors, char *ipv6) {
    for(int i = 0; i < MAX_NEIGHBORS; i++) {
        if(strcmp(neighbors[i], ipv6) == 0) return i;
    }
    return -1;
}

// Purpose: use ipv6 addresses to break ties 
//
// ipv6_a char*, the first ipv6 address
// ipv6_b char*, the second ipv6 address
// return -1 if a<b, 1 if a>b, 0 if a==b
int minIPv6(char *ipv6_a, char *ipv6_b) {
    uint32_t minLength = strlen(ipv6_a);
    if (strlen(ipv6_b) < minLength) minLength = strlen(ipv6_b);
    
    for (uint32_t i = 0; i < minLength; i++) {
        if (ipv6_a[i] < ipv6_b[i]) {
            return -1;
        } else if (ipv6_b[i] < ipv6_a[i]) {
            return 1;
        }
    }
    return 0;
}

// Purpose: main code for the UDP serverS
void *_udp_server(void *args)
{
    (void)args;
    msg_init_queue(server_msg_queue, SERVER_MSG_QUEUE_SIZE);

    // IPv6 address variables
    char IPv6_1[IPV6_ADDRESS_LEN] = { 0 };              // holder for an address
    char IPv6_2[IPV6_ADDRESS_LEN] = { 0 };              // holder for an address
    char masterIPv6[IPV6_ADDRESS_LEN] = "unknown";      // address of master node
    char myIPv6[IPV6_ADDRESS_LEN] = "unknown";          // my address
    char leaderIPv6[IPV6_ADDRESS_LEN] = "unknown";      // the "leader so far"
    char tempLeaderIPv6[IPV6_ADDRESS_LEN] = "unknown";  // leader of the round

    // other string representation variables
    char startTime[10] = { 0 }; // string rep of algorithm start (microsec)
    char convTime[10] = { 0 };  // string rep of convergence time (microsec)
    char portBuf[6] = { 0 };    // string rep of server port
    char codeBuf[10] = { 0 };   // junk array for message headers
    char mStr[4] = { 0 };       // string rep of a 3 digit value
    char messages[10] = { 0 };  // string rep for number of messages

    // buffers
    char msg[SERVER_BUFFER_SIZE] = { 0 };                           // pre-allocated msg
    char *msgP = (char*)calloc(SERVER_BUFFER_SIZE, sizeof(char));   // dynamic msg

    // other component variables
    int i = 0;                  // a loop counter
    bool discovered = false;    // have we been discovered by master
    bool topoComplete = false;  // did we learn our neighbors
    //int rconf = 0;              // did master confirm our results
    int res = 0;                // return value from socket

    // leader election variables
    uint32_t m = 257;               // my m value
    uint32_t min = 257;             // current min found
    uint32_t tempMin = 257;         // min for the round
    int counter = LE_K;             // K value for our algorithm
    int stateLE = 0;                // current leader election state
    int countedMs = 0;              // m values received this round
    uint32_t lastT = 0;             // the last T time recorded
    uint32_t startTimeLE = 0;       // when leader election started
    uint32_t endTimeLE = 0;         // when leader election ended
    uint32_t convergenceTimeLE = 0; // leader election convergence time
    bool hasElectedLeader = false;  // has a leader been elected
    bool updated = false;           // flag for line 12 of pseudocode

    // neighbor variables
    int numNeighbors = 0;   // number of neighbors
    char **neighbors = (char**)calloc(MAX_NEIGHBORS, sizeof(char*));    // list of neighbors
    for(i = 0; i < MAX_NEIGHBORS; i++) {
        neighbors[i] = (char*)calloc(IPV6_ADDRESS_LEN, sizeof(char));   // neighbor addresses
    }
    uint32_t neighborsVal[MAX_NEIGHBORS] = { 0 };                       // neighbor m values

    // socket server setup
    sock_udp_ep_t server = { .port = SERVER_PORT, .family = AF_INET6 };
    sock_udp_ep_t remote;
    kernel_pid_t myPid = thread_getpid();
    (void)myPid;
    sprintf(portBuf,"%d",SERVER_PORT);

    // create the socket
    if(sock_udp_create(&my_sock, &server, NULL, 0) < 0) {
        return NULL;
    }

    server_running = true;
    printf("UDP: Success - started UDP server on port %u\n", server.port);

    // main server loop
    while (1) {
        // incoming UDP
        memset(server_buffer, 0, SERVER_BUFFER_SIZE);
        memset(msg, 0, SERVER_BUFFER_SIZE);
        memset(msgP, 0, SERVER_BUFFER_SIZE);
        memset(mStr, 0, 4);

        if (server_buffer == NULL || sizeof(server_buffer) - 1 <= 0) {
            printf("ERROR: failed sock_udp_recv preconditions\n");
            return NULL;
        }

        if ((res = sock_udp_recv(&my_sock, server_buffer,
                 sizeof(server_buffer) - 1, 0.03 * US_PER_SEC, //SOCK_NO_TIMEOUT,
                 &remote)) < 0) {
            if (res != 0 && res != -ETIMEDOUT && res != -EAGAIN) {
                printf("WARN: failed to receive UDP, %d\n", res);
            }
        }
        else if (res == 0) {
            printf("WARN: no UDP data associated with message\n");
        }
        else {
            server_buffer[res] = '\0';
            res = 1;
            countMsgIn();
            ipv6_addr_to_str(IPv6_1, (ipv6_addr_t *)&remote.addr.ipv6, IPV6_ADDRESS_LEN);
            if (DEBUG == 1) {
                printf("UDP: recvd: %s from %s\n", server_buffer, IPv6_1);
            }
        }

        // react to UDP message
        if (res == 1) {
            // the master is discovering us
            if (strncmp(server_buffer,"ping;",5) == 0) {
                if (!discovered) { 
                    strcpy(masterIPv6, IPv6_1);
                    strcpy(msg, "pong;");
                    char *argsMsg[] = { "udp_send", masterIPv6, portBuf, msg, NULL };
                    udp_send(4, argsMsg);

                    printf("UDP: discovery attempt from master node (%s)\n", masterIPv6);
                }

            // the master acknowledging our acknowledgement
            } else if (strncmp(server_buffer,"conf;",5) == 0) {
                strcpy(masterIPv6, IPv6_1); // redundant
                discovered = true;

                printf("UDP: master node (%s) confirmed us\n", masterIPv6);

            // information about our IP and neighbors
            } else if (strncmp(server_buffer,"ips;",4) == 0) {
                // process IP and neighbors
                if (!topoComplete) {
                    strcpy(msgP, server_buffer);
                    char *mem = msgP;

                    if (DEBUG == 1) {                    
                        printf("UDP: ips = %s\n", mem);
                    }

                    extractMsgSegment(&mem,codeBuf);
                    extractMsgSegment(&mem,mStr);   // extract my m value
                    m = atoi(mStr);         // convert to integer
                    min = m;                // I am the starting min

                    extractMsgSegment(&mem,myIPv6);     // extract my IP
                    strcpy(leaderIPv6, myIPv6); // I am the starting leader

                    // extract neighbors IPs from message
                    while(strlen(mem) > 1) {
                        extractMsgSegment(&mem,neighbors[numNeighbors]);
                        numNeighbors++;
                    }
                    
                    topoComplete = true;
                }

            // start leader election
            } else if (strncmp(server_buffer,"start;",6) == 0) {
                // start leader election
                printf("UDP: My IPv6 is: %s, m=%"PRIu32"\n", myIPv6, m);
                printf("LE: Topology assignment complete, %d neighbors:\n",numNeighbors);

                // print neighbors for convenience
                for (i = 0; i < numNeighbors; i++) {
                    if (strcmp(neighbors[i],"") == 0) {
                        continue;
                    }
                    printf("%2d: %s\n", i+1, neighbors[i]);
                }

                if (numNeighbors <= 0) {
                    printf("ERROR: trying to start leader election with no neighbors\n");
                    return NULL;
                }

                // set some initial values
                printf("LE: Initiating leader election...\n");
                runningLE = true;
                startTimeLE = xtimer_now_usec();
                counter = LE_K;
                stateLE = 0;

            // this neighbor is sending us leader election values
            } else if (strncmp(server_buffer,"le_ack;",7) == 0) {
                // *** message handling component of pseudocode line 6 and 14
                strcpy(msgP, server_buffer);
                char *mem = msgP;
                uint32_t localM;

                extractMsgSegment(&mem,codeBuf);    // remove header
                extractMsgSegment(&mem,mStr);       // get m value
                extractMsgSegment(&mem,IPv6_2);     // obtain owner ID
                i = getNeighborIndex(neighbors, IPv6_1);  // check the sender/neighbor

                if (i < 0) {
                    printf("ERROR: sender of message not found in neighbor list (%s)\n", IPv6_1);
                    continue;
                }

                localM = atoi(mStr);
                if (localM <= 0 || localM >= 256) {
                    printf("ERROR: m value is out of range, %"PRIu32"\n", localM);
                    continue;
                }

                if (neighborsVal[i] == 0) countedMs++;
                neighborsVal[i] = (uint32_t)localM;
                printf("LE: m value %"PRIu32" received from %s, owner %s\n", localM, IPv6_1, IPv6_2);

                // if this message introduces a new extrema
                if (neighborsVal[i] < tempMin) {
                    strcpy(tempLeaderIPv6, IPv6_2); // new round leader
                    tempMin = neighborsVal[i];      // new min round value
                    printf("LE: new tempMin=%"PRIu32", tempLeader=%s\n", tempMin, tempLeaderIPv6);

                // if a tie has occured
                } else if (neighborsVal[i] == tempMin) {
                    // break the tie
                    if (strcmp(tempLeaderIPv6, IPv6_2) > 0) {
                        // new guy won the tie
                        strcpy(tempLeaderIPv6, IPv6_2);
                        printf("LE: tied tempMin, new message won the tie (%s)\n", tempLeaderIPv6);
                    }
                }

            // someone wants my current min
            } else if (strncmp(server_buffer,"le_m?;",6) == 0) {
                // *** message handling component of reacting to polls from pseudocode line 7
                sprintf(mStr, "%"PRIu32"", min);
                strcpy(msg, "le_ack;");      // le_ack command
                strcat(msg, mStr);          // add on min value
                strcat(msg, ";");
                strcat(msg, leaderIPv6);    // add on leader address
                strcat(msg, ";");

                // answer the m value request
                char *argsMsg[] = { "udp_send", IPv6_1, portBuf, msg, NULL };
                udp_send(4, argsMsg);

            // master confirmed our results
            } else if (strncmp(server_buffer,"rconf",5) == 0) {
                //rconf = 1;
                if (DEBUG == 1) {
                    printf("UDP: master confirmed results\n");
                }
            }
        }

        // if running leader election currently
        if (runningLE && !hasElectedLeader) {
// perform leader election
            if (stateLE == 0) { // *** line 5 of pseudocode
                if (DEBUG == 1) {
                    printf("LE: case 0, leader=%s, min=%"PRIu32"\n", leaderIPv6, min);
                }
                //le_ack:m;leader;
                strcpy(msg, "le_ack;");
                sprintf(mStr, "%"PRIu32"",min);
                strcat(msg,mStr);
                strcat(msg,";");
                strcat(msg,leaderIPv6);
                strcat(msg,";");

                if (DEBUG == 1) {
                    printf("LE: sending message %s to all neighbors\n", msg);
                }

                // send initial value to all neighbors
                for (i = 0; i < numNeighbors; i++) {
                    if (strcmp(neighbors[i],"") == 0) {
                        continue;
                    }

                    if (DEBUG == 1) {
                        printf(" LE: sending to %s\n", neighbors[i]);
                    }

                    char *argsMsg[] = { "udp_send", neighbors[i], portBuf, msg, NULL };
                    udp_send(4, argsMsg);
                    
                    xtimer_usleep(10000); // wait 0.01 seconds
                }

                stateLE = 1;
                lastT = xtimer_now_usec();

            } else if (stateLE == 1) { // *** lines 6-14 of pseudocode
                if (countedMs == numNeighbors || lastT < xtimer_now_usec() - LE_T) {
                    counter -= 1;       // reduce counter, *** line 9 of pseudocode
                    updated = false;    // reset flag, *** line 10 of pseudocode

                    printf("LE: counter reduced to %d\n", counter);

                    // new leader found
                    if (tempMin < min) { // *** line 11 of pseudocode
                        printf("LE: case <, tempMin=%"PRIu32" < min=%"PRIu32", heardFrom=%d\n", tempMin, min, countedMs);

                        updated = true; // *** line 11a of pseudocode
                        min = tempMin; // *** line 11b of pseudocode
                        strcpy(leaderIPv6, tempLeaderIPv6); // *** line 11c of pseudocode

                    // new leader possibly found, check for tie
                    } else if (tempMin == min) { // *** handles ties, not represented in pseudocode
                        // break the tie
                        if (strcmp(leaderIPv6, tempLeaderIPv6) > 0) {
                            // new guy won the tie
                            strcpy(leaderIPv6, tempLeaderIPv6);
                            updated = true;
                            printf("LE: tied min, new guy won the tie (%s)\n", leaderIPv6);
                        }
                    }

                    // quit, *** line 12 of pseudocode
                    if (counter < 0 && updated == false) {
                        printf("LE: counter == 0 so quit\n");
                        stateLE = 2;

                    // send information to all neighbors, *** lines 13/13a of pseudocode
                    } else if (updated == true) {
                        //le_ack:m;leader;
                        strcpy(msg, "le_ack;");
                        sprintf(mStr, "%"PRIu32"",min);
                        strcat(msg,mStr);
                        strcat(msg,";");
                        strcat(msg,leaderIPv6);
                        strcat(msg,";");

                        if (DEBUG == 1) {
                            printf("LE: sending message %s to all neighbors\n", msg);
                        }

                        // send min value to all neighbors
                        for (i = 0; i < numNeighbors; i++) {
                            if (strcmp(neighbors[i],"") == 0) {
                                continue;
                            }

                            if (DEBUG == 1) {
                                printf(" LE: sending to %s\n", neighbors[i]);
                            }

                            char *argsMsg[] = { "udp_send", neighbors[i], portBuf, msg, NULL };
                            udp_send(4, argsMsg);
                            
                            xtimer_usleep(20000); // wait 0.02 seconds
                        }
                    }

                    // reset variables for the next round
                    if (stateLE == 1) {
                        tempMin = 257;
                        countedMs = 0;
                        for (i = 0; i < numNeighbors; i++) {
                            neighborsVal[i] = 0;
                        }
                        lastT = xtimer_now_usec(); // *** timer reset symbolizes pseudocode lines 14/15
                    }
                }

            // algorithm terminated
            } else if (stateLE == 2) {
                printf("LE: %s elected as the leader, via m=%"PRIu32"!\n", leaderIPv6, min);
                if (strcmp(leaderIPv6, myIPv6) == 0) {
                    printf("LE: Hey, that's me! I'm the leader!\n");
                }

                endTimeLE = xtimer_now_usec();
                convergenceTimeLE = (endTimeLE - startTimeLE);
                printf("LE:    start=%"PRIu32"\n", startTimeLE);
                printf("LE:      end=%"PRIu32"\n", endTimeLE);
                printf("LE: converge=%"PRIu32"\n", convergenceTimeLE);

                runningLE = false;
                hasElectedLeader = true;
                countedMs = 0;
                stateLE = 0;

                strcpy(msg, "results;");
                strcat(msg, leaderIPv6);
                strcat(msg, ";");
                sprintf(startTime , "%"PRIu32 , startTimeLE);
                strcat(msg, startTime);
                strcat(msg, ";");
                sprintf(convTime , "%"PRIu32 , convergenceTimeLE);
                strcat(msg, convTime);
                strcat(msg, ";");

                int tMsgs = messagesIn + messagesOut;
                sprintf(messages, "%d" , tMsgs);
                strcat(msg, messages);
                strcat(msg, ";");

                char *argsMsg[] = { "udp_send", masterIPv6, portBuf, msg, NULL };
                udp_send(4, argsMsg);

                if (DEBUG == 1) {
                    printf("LE: sending results to master (%s): %s\n", masterIPv6, msg);
                }
            } else {
                printf("ERROR: leader election in invalid state %d\n", stateLE);
                break;
            }
        }

        xtimer_usleep(20000); // wait 0.02 seconds
    }

    // free memory
    for(int i = 0; i < MAX_NEIGHBORS; i++) {
        free(neighbors[i]);
    }
    free(neighbors);
    free(msgP);

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
    (void)argv;
    if (argc != 1) {
        puts("MAIN: Usage - udps");
        return -1;
    }

    if ((server_running == false) &&
        thread_create(server_stack, sizeof(server_stack), THREAD_PRIORITY_MAIN - 1,
                      THREAD_CREATE_STACKTEST, _udp_server, NULL, "UDP_Server_Thread")
        <= KERNEL_PID_UNDEF) {
        printf("MAIN: Error - failed to start UDP server thread\n");
        return -1;
    }

    return 0;
}
