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
#define SERVER_MSG_QUEUE_SIZE   (32)
#define SERVER_BUFFER_SIZE      (255)
#define IPV6_ADDRESS_LEN        (46)
#define MAX_NEIGHBORS           (6)

// Leader Election values
#ifndef LE_K
    #define LE_K    (5)
#endif
#ifndef LE_T
    #define LE_T    (3.00*1000000)
#endif

#define DEBUG       (0)

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
    char newLeaderIPv6[IPV6_ADDRESS_LEN] = "unknown";   // leader of the round

    // other string representation variables
    char startTime[10] = { 0 }; // string rep of algorithm start (microsec)
    char convTime[10] = { 0 };  // string rep of convergence time (microsec)
    char portBuf[6] = { 0 };    // string rep of server port
    char codeBuf[10] = { 0 };   // junk array for message headers
    char mStr[5] = { 0 };       // string rep of a 3 digit value
    char messages[10] = { 0 };  // string rep for number of messages

    // buffers
    char msg[SERVER_BUFFER_SIZE] = { 0 };                           // pre-allocated msg
    char *msgP = (char*)calloc(SERVER_BUFFER_SIZE, sizeof(char));   // dynamic msg

    // other component variables
    int i = 0;                  // a loop counter
    bool discovered = false;    // have we been discovered by master
    bool topoComplete = false;  // did we learn our neighbors
    int rconf = 0;              // did master confirm our results
    int res = 0;                // return value from socket
    bool polled = false;        // have missing nodes been polled yet
    int sendRes = 0;            // result send attempts

    // leader election variables
    uint32_t m = 257;               // my m value
    uint32_t local_min = 257;       // current local_min found
    uint32_t new_local_min = 257;   // local_min for the round
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
    char **neighborsLeaders = (char**)calloc(MAX_NEIGHBORS, sizeof(char*));    // list of neighbors leaders
    for(i = 0; i < MAX_NEIGHBORS; i++) {
        neighborsLeaders[i] = (char*)calloc(IPV6_ADDRESS_LEN, sizeof(char));   // leader addresses
    }
    uint32_t neighborsVal[MAX_NEIGHBORS] = { 257 };                     // neighbor m values

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
                    m = (uint32_t)atoi(mStr);       // convert to integer
                    local_min = m;                  // I am the starting local_min
                    extractMsgSegment(&mem,myIPv6); // extract my IP
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
                // *** message handling component of pseudocode lines 6, 7, and 8g
                strcpy(msgP, server_buffer);
                char *mem = msgP;
                uint32_t localM = 257;

                memset(IPv6_2, 0, IPV6_ADDRESS_LEN);
                memset(mStr, 0, 5);
                extractMsgSegment(&mem,codeBuf);    // remove header
                extractMsgSegment(&mem,mStr);       // get m value
                extractMsgSegment(&mem,IPv6_2);     // obtain owner ID
                i = getNeighborIndex(neighbors, IPv6_1);  // check the sender/neighbor

                if (i < 0) {
                    printf("ERROR: sender of message not found in neighbor list (%s)\n", IPv6_1);
                    continue;
                }

                localM = (uint32_t)atoi(mStr);
                if (localM <= 0 || localM >= 256) {
                    printf("ERROR: m value is out of range, %"PRIu32"\n", localM);
                    continue;
                }

                countedMs++;
                neighborsVal[i] = localM;
                memset(neighborsLeaders[i], 0, IPV6_ADDRESS_LEN);
                strcpy(neighborsLeaders[i], IPv6_2);

                printf("LE: m value %"PRIu32"//%s received from %s\n", neighborsVal[i], neighborsLeaders[i], IPv6_1);

            // someone wants my current local_min
            } else if (strncmp(server_buffer,"le_m?;",6) == 0) {
                // *** message handling component of line 7
                memset(mStr, 0, 5);
                sprintf(mStr, "%"PRIu32"", local_min);
                strcpy(msg, "le_ack;");     // le_ack command
                strcat(msg, mStr);          // add on local_min value
                strcat(msg, ";");
                strcat(msg, leaderIPv6);    // add on leader address
                strcat(msg, ";");

                // answer the m value request
                char *argsMsg[] = { "udp_send", IPv6_1, portBuf, msg, NULL };
                udp_send(4, argsMsg);
    
            // a node had a failure
            } else if (strncmp(server_buffer,"failure;",8) == 0) {
                printf("ERROR: a node failed and master told us to terminate\n");
                break; // terminate with error

            // master confirmed our results
            } else if (strncmp(server_buffer,"rconf",5) == 0) {
                rconf = 1;
                printf("UDP: master confirmed results, terminating\n");
                break; // terminate correctly
            }
        }

        // if running leader election currently
        if (runningLE && !hasElectedLeader) {

            // *** line 5 of pseudocode
            if (stateLE == 0) { 
                if (DEBUG == 1) {
                    printf("LE: case 0, leader=%s, local_min=%"PRIu32"\n", leaderIPv6, local_min);
                }
                //le_ack:m;leader;
                strcpy(msg, "le_ack;");
                sprintf(mStr, "%"PRIu32"",local_min);
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
                    
                    xtimer_usleep(1000); // wait 0.001 seconds
                }

                stateLE = 1;
                lastT = xtimer_now_usec();

            // *** lines 6-7
            } else if (stateLE == 1) {
                if (lastT < xtimer_now_usec() - LE_T) {     // line 6
                    if (!polled) {
                        strcpy(msg, "le_m?;");
                        for (i = 0; i < numNeighbors; i++) {    // line 7
                            if (neighborsVal[i] == 257) {
                                // poll this missing neighbor
                                char *argsMsg[] = { "udp_send", neighbors[i], portBuf, msg, NULL };
                                udp_send(4, argsMsg);
                                xtimer_usleep(1000); // wait 0.001 seconds
                            }
                        }
                        polled = true;
                        lastT = xtimer_now_usec() + LE_T; // wait for 2*LE_T
                    } else {
                        for (i = 0; i < numNeighbors; i++) {    // line 7a and 7ai
                            if (neighborsVal[i] == 257) {
                                // inform master of failure
                                //printf("ERROR: we have failed, informing the master\n");
                                //strcpy(msg, "failure;");
                                //char *argsMsg[] = { "udp_send", masterIPv6, portBuf, msg, NULL };
                                //udp_send(4, argsMsg);
                                //return NULL;

                                // for now, don't fail, try to continue on
                                printf("ERROR: we did not hear from a node, continuing anyways\n");
                            }
                        }
                        stateLE = 2;
                        lastT = xtimer_now_usec();
                    }
                    
                }

            // *** lines 8a to 8g
            } else if (stateLE == 2) { 
                if (lastT < xtimer_now_usec() - LE_T) {

                    // calculate round local_min, *** line 8a of pseudocode
                    new_local_min = local_min;
                    memset(newLeaderIPv6, 0, IPV6_ADDRESS_LEN);
                    strcpy(newLeaderIPv6, leaderIPv6);

                    for (i = 0; i < numNeighbors; i++) {
                        // check for bad m values
                        if (neighborsVal[i] <= 0 || neighborsVal[i] >= 256) {
                            printf("ERROR: m value is out of range, %"PRIu32"\n", neighborsVal[i]);
                            continue;
                        }

                        // find minimum of neighborhood
                        if (neighborsVal[i] < new_local_min) {
                            memset(newLeaderIPv6, 0, IPV6_ADDRESS_LEN);
                            strcpy(newLeaderIPv6, neighborsLeaders[i]); // new round leader
                            new_local_min = neighborsVal[i];            // new local_min round value
                            //printf("LE: new_local_min=%"PRIu32", newLeaderIPv6=%s\n", new_local_min, newLeaderIPv6);

                        // if a tie has occured
                        } else if (neighborsVal[i] == new_local_min) {
                            // break the tie
                            if (strcmp(newLeaderIPv6, neighborsLeaders[i]) > 0) {
                                // new guy won the tie
                                printf("LE: lost m value tie (%"PRIu32"), %s vs %s\n",new_local_min, newLeaderIPv6, neighborsLeaders[i]);
                                memset(newLeaderIPv6, 0, IPV6_ADDRESS_LEN);
                                strcpy(newLeaderIPv6, neighborsLeaders[i]);
                            }
                        }
                    }

                    counter -= 1;       // reduce counter, *** line 8b of pseudocode
                    updated = false;    // reset flag, *** line 8c of pseudocode
                    printf("LE: counter reduced to %d\n", counter);

                    // new leader found, either by m value or tie break
                    if (strcmp(leaderIPv6, newLeaderIPv6) != 0) { // *** line 8d of pseudocode
                        printf("LE: new leader, new_local_min %"PRIu32" < %"PRIu32", heard from %d nodes\n", new_local_min, local_min, countedMs);

                        updated = true;             // *** line 8di of pseudocode
                        local_min = new_local_min;  // *** line 8dii of pseudocode
                        memset(leaderIPv6, 0, IPV6_ADDRESS_LEN);
                        strcpy(leaderIPv6, newLeaderIPv6); // *** line 8diii of pseudocode                
                    }

                    // quit, *** lines 8e and 8ei
                    if (counter < 0 && updated == false) {
                        printf("LE: counter < 0 so quit\n");
                        lastT = 0;
                        stateLE = 3;

                    // *** lines 8f and 8fi
                    } else if (updated == true) {
                        //le_ack:m;leader;
                        strcpy(msg, "le_ack;");
                        sprintf(mStr, "%"PRIu32"",local_min);
                        strcat(msg,mStr);
                        strcat(msg,";");
                        strcat(msg,leaderIPv6);
                        strcat(msg,";");

                        if (DEBUG == 1) {
                            printf("LE: sending message %s to neighbors who need it\n", msg);
                        }

                        // send local_min value to neighbors that don't have it yet
                        for (i = 0; i < numNeighbors; i++) {
                            // if neighbor slot is blank, skip
                            if (strcmp(neighbors[i],"") == 0) {
                                continue;
                            }

                            // if this neighbor already has the leader, skip
                            if (strcmp(neighborsLeaders[i], leaderIPv6) == 0) {
                                continue;
                            }

                            if (DEBUG == 1) {
                                printf(" LE: sending to %s\n", neighbors[i]);
                            }

                            char *argsMsg[] = { "udp_send", neighbors[i], portBuf, msg, NULL };
                            udp_send(4, argsMsg);
                            
                            xtimer_usleep(1000); // wait 0.001 seconds
                        }
                    }

                    // *** go to next iteration of psuedocode while loop
                    if (stateLE == 2) {
                        countedMs = 0;
                        lastT = xtimer_now_usec(); 
                    }
                }

            // protocol complete, *** line 9
            } else if (stateLE == 3) {
                // send results every 5 seconds until confirmed
                if (rconf == 0 && lastT < xtimer_now_usec() - 5000000) {
                    int tMsgs = messagesIn + messagesOut;

                    // display election results
                    if (sendRes == 0) {
                        printf("\nLE: %s elected as the leader, via m=%"PRIu32"!\n", leaderIPv6, local_min);
                        if (strcmp(leaderIPv6, myIPv6) == 0) {
                            printf("LE: Hey, that's me! I'm the leader!\n");
                        }

                        endTimeLE = xtimer_now_usec();
                        convergenceTimeLE = (endTimeLE - startTimeLE);
                        printf("LE:    start=%"PRIu32"\n", startTimeLE);
                        printf("LE:      end=%"PRIu32"\n", endTimeLE);
                        printf("LE: converge=%"PRIu32"\n", convergenceTimeLE);
                        printf("LE: messages=%d\n\n", tMsgs);

                        runningLE = false;
                        hasElectedLeader = true;
                        countedMs = 0;
                    }
                    sendRes += 1;

                    // build results package
                    strcpy(msg, "results;");
                    strcat(msg, leaderIPv6);
                    strcat(msg, ";");

                    sprintf(startTime , "%"PRIu32"", startTimeLE);
                    strcat(msg, startTime);
                    strcat(msg, ";");

                    sprintf(convTime , "%"PRIu32"", convergenceTimeLE);
                    strcat(msg, convTime);
                    strcat(msg, ";");

                    sprintf(messages, "%d" , tMsgs);
                    strcat(msg, messages);
                    strcat(msg, ";");

                    printf("LE: attempt %d of sending results to master\n", sendRes);

                    // send results
                    char *argsMsg[] = { "udp_send", masterIPv6, portBuf, msg, NULL };
                    udp_send(4, argsMsg);

                    lastT = xtimer_now_usec(); 
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
        free(neighborsLeaders[i]);
    }
    free(neighbors);
    free(neighborsLeaders);
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
