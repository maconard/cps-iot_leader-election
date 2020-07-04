/*
 * @author  Michael Conard <maconard@mtu.edu>
 *
 * Purpose: Contains the protocol code for leader election/neighbor discovery.
 */

#define ENABLE_LEADER (1)

// Standard C includes
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <msg.h>

// Standard RIOT includes
#include "thread.h"
#include "xtimer.h"

#define CHANNEL                 11

#define MAIN_QUEUE_SIZE         (32)
#define MAX_IPC_MESSAGE_SIZE    (128)
#define IPV6_ADDRESS_LEN        (46)
#define MAX_NEIGHBORS           (8)

#define DEBUG                   0

// Leader Election values
#define K     (5)
#define T1    (6*1000000)
#define T2    (4*1000000)

// External functions defs
extern int ipc_msg_send(char *message, kernel_pid_t destinationPID, bool blocking);
extern int ipc_msg_reply(char *message, msg_t incoming);
extern int ipc_msg_send_receive(char *message, kernel_pid_t destinationPID, msg_t *response, uint16_t type);
extern void substr(char *s, int a, int b, char *t);
extern int indexOfSemi(char *ipv6);
extern void extractIP(char **s, char *t);

// Forward declarations
kernel_pid_t leader_election(int argc, char **argv);
void *_leader_election(void *argv);
void substr(char *s, int a, int b, char *t);

// Data structures (i.e. stacks, queues, message structs, etc)
static char protocol_stack[THREAD_STACKSIZE_DEFAULT];
static msg_t _protocol_msg_queue[MAIN_QUEUE_SIZE];
static msg_t msg_p_in;//, msg_out;

kernel_pid_t udpServerPID = 0;

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

// ************************************
// START MY CUSTOM THREAD DEFS

// Purpose: launch the procotol thread
//
// argc int, argument count (should be 2)
// argv char**, list of arguments ("leader_election",<port>)
kernel_pid_t leader_election(int argc, char **argv) {
    if (argc != 2) {
        puts("Usage: leader_election <port>");
        return 0;
    }

    kernel_pid_t protocolPID = thread_create(protocol_stack, sizeof(protocol_stack), THREAD_PRIORITY_MAIN - 1, THREAD_CREATE_STACKTEST, _leader_election, argv[1], "Protocol_Thread");

    printf("MAIN: thread_create(..., protocol_thread) returned: %" PRIkernel_pid "\n", protocolPID);
    if (protocolPID <= KERNEL_PID_UNDEF) {
        (void) puts("MAIN: Error - failed to start leader election thread");
        return 0;
    }

    return protocolPID;
}

// Purpose: the actual protocol thread code
// Currently implements neighbor discovery and leader election
//
// argv void*, exists for RIOT semantics purposes (unused)
void *_leader_election(void *argv) {
    (void)argv;
    msg_init_queue(_protocol_msg_queue, MAIN_QUEUE_SIZE);

    char msg_content[MAX_IPC_MESSAGE_SIZE];

    // ipv6 address vars
    char ipv6[IPV6_ADDRESS_LEN] = { 0 };
    char ipv6_2[IPV6_ADDRESS_LEN] = { 0 };
    char myIPv6[IPV6_ADDRESS_LEN] = { 0 };
    char initLE[8] = "le_init";
    char neighborM[4] = { 0 };

    uint32_t startTimeLE = 0;
    uint32_t endTimeLE = 0;
    uint32_t convergenceTimeLE = 0;
    bool hasElectedLeader = false;
    bool runningLE = false;
    bool allowLE = false;
    int stateLE = 0;
    int countedMs = 0;
    bool quit = false;

    // Ali's LE variables
    int counter = K; //k
    uint32_t m; // my leader election value, range 1 to 255
    uint32_t min;                       // the min of my neighborhood 
    uint32_t tempMin = 257;
    char leader[IPV6_ADDRESS_LEN] = "unknown";    // the "leader so far"
    char tempLeader[IPV6_ADDRESS_LEN];    // temp leader for a round of communication
    uint32_t t1 = T1;
    uint32_t t2 = T2;
    uint32_t lastT1 = 0;
    uint32_t lastT2 = 0;
    bool topoComplete = false;

    int res = 0;

    // array of MAX neighbors
    int c = 0; // other counter
    int i = 0; // loop counter
    int numNeighbors = 0;
    uint32_t neighborsVal[MAX_NEIGHBORS] = { 0 }; 
    char **neighbors = (char**)calloc(MAX_NEIGHBORS, sizeof(char*));
    for(i = 0; i < MAX_NEIGHBORS; i++) {
        neighbors[i] = (char*)calloc(IPV6_ADDRESS_LEN, sizeof(char));
    }

    m = 257;
    min = m;

    printf("LE: Success - started protocol thread with m=%"PRIu32"\n", m);

    // main thread startup loop
    while (1) { 
        if (quit) break;
        // process messages
        memset(msg_content, 0, MAX_IPC_MESSAGE_SIZE);
        res = msg_try_receive(&msg_p_in);
        //printf("LE: after msg_try_receive, code=%d\n", res);
        if (res == 1) {
            if (msg_p_in.type == 0 && udpServerPID == (kernel_pid_t)0) { // process UDP server PID

                //(void) puts("LE: in type==0 block");
                udpServerPID = *(kernel_pid_t*)msg_p_in.content.ptr;
                if (DEBUG == 1) {
                    printf("LE: Protocol thread recorded %" PRIkernel_pid " as the UDP server thread's PID\n", udpServerPID);
                }
                continue;

            } else if (msg_p_in.type == 1) { // receive m value

                if (DEBUG == 1) {
                    printf("LE: in type==1 block, content=%s\n", (char*)msg_p_in.content.ptr);
                }
                m = atoi((char*)msg_p_in.content.ptr);
                min = m;
                continue;

            } else if (msg_p_in.type == 2) { // report about the leader

                if (DEBUG == 1) {
                    printf("LE: in type==2 block, content=%s\n", (char*)msg_p_in.content.ptr);
                    printf("LE: replying with msg=%s, size=%u\n", leader, strlen(leader));
                }
                ipc_msg_reply(leader, msg_p_in);
                continue;

            } else if (msg_p_in.type > 2 && msg_p_in.type < MAX_IPC_MESSAGE_SIZE) { // process string message of size msg_p_in.type

                //printf("LE: in type>2 block, type=%u\n", (uint16_t)msg_p_in.type);
                strncpy(msg_content, (char*)msg_p_in.content.ptr, (uint16_t)msg_p_in.type+1);
                if (DEBUG == 1) {
                    printf("LE: Protocol thread received IPC message: %s from PID=%" PRIkernel_pid " with type=%d\n", msg_content, msg_p_in.sender_pid, msg_p_in.type);
                }

            } else {

                (void) puts("LE: Protocol thread received an illegal or too large IPC message");
                continue;

            }
        }

        // react to input, allowed anytime
        if (res == 1) {
            if (strncmp(msg_content, "ips:", 4) == 0) {
                if (!topoComplete) {
                    char *msg = (char*)calloc(MAX_IPC_MESSAGE_SIZE, sizeof(char));
                    char *mem = msg;
                    substr(msg_content, 4, strlen(msg_content)-4, msg);

                    extractIP(&msg,neighborM);
                    m = atoi(neighborM);
                    min = m;
                    printf("LE: Protocol thread recorded %"PRIu32" as it's m value\n", m);

                    extractIP(&msg,myIPv6);
                    strcpy(leader, myIPv6);
                    printf("LE: Protocol thread recorded %s as it's IPv6\n", leader);
                    allowLE = true;

                    // extract neighbors IPs from message
                    while(strlen(msg) > 1) {
                        extractIP(&msg,neighbors[numNeighbors]);
                        printf("LE: Extracted neighbor %d: %s\n", numNeighbors+1, neighbors[numNeighbors]);
                        numNeighbors++;
                    }
                    
                    topoComplete = true;
                    free(mem);
                }

            } else if (strncmp(msg_content, "start:", 6) == 0) {
                quit = true;
                break;
            }
        }
        xtimer_usleep(50000); // wait 0.05 seconds
    }

    // thread startup complete
    printf("Topology assignment complete, %d neighbors:\n",numNeighbors);
    c = 1;
    for (i = 0; i < numNeighbors; i++) {
        if (strcmp(neighbors[i],"") == 0) {
            continue;
        }
        printf("%2d: %s\n", c, neighbors[i]);
        c += 1;
    }
    quit = false;

    // main thread execution loop
    while (1) {
        if (quit) break;
        // receive messages
        memset(msg_content, 0, MAX_IPC_MESSAGE_SIZE);
        res = msg_try_receive(&msg_p_in);

        if (res == 1) {
            // processing
            if (msg_p_in.type > 2 && msg_p_in.type < MAX_IPC_MESSAGE_SIZE) { // process string message of size msg_p_in.type

                //printf("LE: in type>2 block, type=%u\n", (uint16_t)msg_p_in.type);
                strncpy(msg_content, (char*)msg_p_in.content.ptr, (uint16_t)msg_p_in.type+1);
                if (DEBUG == 1) {
                    printf("LE: Protocol thread received IPC message: %s from PID=%" PRIkernel_pid " with type=%d\n", msg_content, msg_p_in.sender_pid, msg_p_in.type);
                }

            } else {

                (void) puts("LE: Protocol thread received an illegal or too large IPC message");
                continue;

            }

            // react
            if (DEBUG == 1) {
                printf("LE: message received: %s\n", msg_content);
            }
            if (strncmp(msg_content, "le_ack:", 7) == 0) {
                // a neighbor has responded
                // le_ack:mmm:ipv6_owner;ipv6_sender
                substr(msg_content, 7, 3, neighborM); // obtain m value
                //printf("LE: extracted m=%s\n", neighborM);
                int j = indexOfSemi(msg_content);
                substr(msg_content, 11, j-11-1, ipv6); // obtain ID
                substr(msg_content, j+1, IPV6_ADDRESS_LEN, ipv6_2); // obtain neighbor ID
                i = getNeighborIndex(neighbors, ipv6_2);

                if (atoi(neighborM) <= 0) continue;

                printf("LE: m value %d received from %s, owner %s\n", atoi(neighborM), ipv6_2, ipv6);
                if (neighborsVal[i] == 0) countedMs++;
                neighborsVal[i] = (uint32_t)atoi(neighborM);
                if (neighborsVal[i] < tempMin) {
                    strcpy(tempLeader, ipv6);
                    tempMin = neighborsVal[i];
                    printf("LE: new tempMin=%"PRIu32", tempLeader=%s\n", tempMin, tempLeader);
                } 
            } else if (strncmp(msg_content, "le_m?:", 6) == 0) {
                // someone wants my m              
                //char *msg = (char*)calloc(MAX_IPC_MESSAGE_SIZE, sizeof(char));
                //strcpy(msg, "le_ack:");                
                char msg[MAX_IPC_MESSAGE_SIZE] = "le_ack:";                 
                if(min < 10) {
                    sprintf(neighborM, "00%"PRIu32"",min);
                } else if (min < 100) {
                    sprintf(neighborM, "0%"PRIu32"",min);
                } else {
                    sprintf(neighborM, "%"PRIu32"",min);
                }
                strcat(msg,neighborM);
                strcat(msg,":");
                strcat(msg,leader);
                strcat(msg,";");
                strcat(msg,myIPv6);
                ipc_msg_send(msg, udpServerPID, false);
                //free(msg);
            }
        }

        // leader election
        if (!runningLE && !hasElectedLeader) {
            // check if it's time to run, then initialize
            if (numNeighbors > 0 && !hasElectedLeader && allowLE) {
                (void) puts("LE: Starting leader election...");
                runningLE = true;
                allowLE = false;
                startTimeLE = xtimer_now_usec();
                counter = K;
                stateLE = 0;
            }
        } else if (runningLE) {
            // perform leader election
            if (stateLE == 0) { // case 0: send out multicast ping
                    if (DEBUG == 1) {
                        printf("LE: case 0, leader=%s, min=%"PRIu32"\n", leader, min);
                    }
                    ipc_msg_send(initLE, udpServerPID, false);
                    stateLE = 1;
                    countedMs = 0;
                    lastT2 = xtimer_now_usec();
            } else if (stateLE == 1) { // case 1: line 4 of psuedocode
                    if (countedMs == numNeighbors || lastT2 < xtimer_now_usec() - t2) {
                        if (DEBUG == 1) {
                            printf("LE: case 1, tempMin=%"PRIu32", min=%"PRIu32", heard from %d neighbors\n", tempMin, min, countedMs);
                        }
                        stateLE = 2;
                        lastT2 = xtimer_now_usec();
                        tempMin = 257;
                        countedMs = 0;
                        for (i = 0; i < numNeighbors; i++) {
                            neighborsVal[i] = 0;
                        }
                    }
            } else if (stateLE == 2) { // case 2: line 5 of pseudocode
                    if (lastT1 < xtimer_now_usec() - t1) {
                        if (DEBUG == 1) {
                            printf("LE: case 2, tempMin=%"PRIu32", min=%"PRIu32", counter==%d\n", tempMin, min, counter);
                        }
                        stateLE = 3;
                        lastT2 = xtimer_now_usec();
                        lastT1 = xtimer_now_usec();
                    }
            } else if (stateLE == 3) { // case 3: lines 5a-f of pseudocode, some contained in response above
                    //countedMs == numNeighbors || 
                    if (lastT2 < xtimer_now_usec() - t2) {
                        //char *msg;

                        if (DEBUG == 1) {
                            printf("LE: case 3, tempMin=%"PRIu32", min=%"PRIu32", heard from %d neighbors\n", tempMin, min, countedMs);
                        }
                        
                        if (tempMin < min) {
                            printf("LE: case <, tempMin=%"PRIu32" < min=%"PRIu32", counter reset to %d\n", tempMin, min, K);
                            min = tempMin;
                            strcpy(leader, tempLeader);
                            counter = K;
                        } else if (tempMin == min && counter > 0) {
                            printf("LE: case ==, tempMin=%"PRIu32" == min=%"PRIu32", counter reduced to %d\n", tempMin, min, counter-1);
                            counter = counter - 1;
                            int tie = minIPv6(leader, tempLeader);
                            if (tie == 1) {
                                // new leader wins tie
                                printf("LE: tempLeader (%s) wins tie over (%s)\n", tempLeader, leader);
                                strcpy(leader, tempLeader);
                            } else {
                                // else the old leader won the tie, so no change
                                printf("LE: existing leader (%s) wins tie\n", leader);
                            }
                        } else if (counter == 0) {
                            printf("LE case finish, counter == 0 so quit\n");
                            stateLE = 5;
                        }

                        if (stateLE == 3) {
                            tempMin = 257;
                            countedMs = 0;
                            for (i = 0; i < numNeighbors; i++) {
                                neighborsVal[i] = 0;
                            }

                            // line 6 of pseudocode        
                            //msg = (char*)calloc(MAX_IPC_MESSAGE_SIZE, sizeof(char));
                            //strcpy(msg, "le_ack:");  
                            char msg[MAX_IPC_MESSAGE_SIZE] = "le_ack:";         
                            if(min < 10) {
                                sprintf(neighborM, "00%"PRIu32"",min);
                            } else if (min < 100) {
                                sprintf(neighborM, "0%"PRIu32"",min);
                            } else {
                                sprintf(neighborM, "%"PRIu32"",min);
                            }
                            strcat(msg,neighborM);
                            strcat(msg,":");
                            strcat(msg,leader);
                            strcat(msg,";");
                            strcat(msg,myIPv6);
                            ipc_msg_send(msg, udpServerPID, false);
                            stateLE = 2;
                            //free(msg);
                            }
                    }

                    // go back to line 5 of pseudocode
                    //if (lastT1 < xtimer_now_usec() - t1) {
                    //    stateLE = 2;
                    //}
            } else if (stateLE == 5) {
                    printf("LE: %s elected as the leader, via m=%"PRIu32"!\n", leader, min);
                    if (strcmp(leader, myIPv6) == 0) {
                        printf("LE: Hey, that's me! I'm the leader!\n");
                    }
                    //char* msg = (char*)calloc(MAX_IPC_MESSAGE_SIZE,sizeof(char));
                    //strcpy(msg, "le_done:");
                    //char msg[MAX_IPC_MESSAGE_SIZE] = "le_done:";                     
                    //ipc_msg_send(msg, udpServerPID, false);
                    //free(msg);
                    endTimeLE = xtimer_now_usec();
                    convergenceTimeLE = (endTimeLE - startTimeLE);
                    printf("LE:    start=%"PRIu32"\n", startTimeLE);
                    printf("LE:      end=%"PRIu32"\n", endTimeLE);
                    printf("LE: converge=%"PRIu32"\n", convergenceTimeLE);
                    //printf("LE: leader election took %.3f seconds to converge\n", convergenceTimeLE);
                    runningLE = false;
                    hasElectedLeader = true;
                    countedMs = 0;
                    stateLE = 0;
                    quit = true;
            } else {
                    printf("LE: leader election in invalid state %d\n", stateLE);
                    break;
            }
        }
        
        xtimer_usleep(50000); // wait 0.05 seconds
        //printf("LE: bottom\n");
    }
    if (DEBUG == 1) {
        printf("LE: quit main loop\n");
    }
    // if the master node needs information from this protocol thread
    // send IPC messages to the UDP thread to forward to the master node

    char msg[MAX_IPC_MESSAGE_SIZE] = "results;";
	char tempTime[10];
	strcat(msg, leader);
    strcat(msg, ";");
	sprintf(tempTime , "%"PRIu32 , convergenceTimeLE);
    strcat(msg, tempTime);
    strcat(msg, ";");
    if (DEBUG == 1) {
        printf("LE: sending results: %s\n", msg);
    }
    ipc_msg_send(msg, udpServerPID, false);

    for(int i = 0; i < MAX_NEIGHBORS; i++) {
        free(neighbors[i]);
    }
    free(neighbors);

    // mini loop that just stays up to report the leader
    while (1) {
        memset(msg_content, 0, MAX_IPC_MESSAGE_SIZE);
        res = msg_try_receive(&msg_p_in);
        if (res == 1) {
            if (msg_p_in.type > 2 && msg_p_in.type < MAX_IPC_MESSAGE_SIZE) { 
                // process string message of size msg_p_in.type
                //printf("LE: in type>2 block, type=%u\n", (uint16_t)msg_p_in.type);
                strncpy(msg_content, (char*)msg_p_in.content.ptr, (uint16_t)msg_p_in.type+1);
                if (DEBUG == 1) {
                    printf("LE: Protocol thread received IPC message: %s from PID=%" PRIkernel_pid " with type=%d\n", msg_content, msg_p_in.sender_pid, msg_p_in.type);
                }

            } 

            if (msg_p_in.type == 2) { // report about the leader
                if (DEBUG == 1) {
                    printf("LE: reporting that the leader is %s\n", leader);
                }
                ipc_msg_reply(leader, msg_p_in);
                continue;

            // other nodes might be one K value behind and still need confirmation
            } else if (strncmp(msg_content, "le_m?:", 6) == 0) {
                // someone wants my m              
                char msg[MAX_IPC_MESSAGE_SIZE] = "le_ack:";               
                if(min < 10) {
                    sprintf(neighborM, "00%"PRIu32"",min);
                } else if (min < 100) {
                    sprintf(neighborM, "0%"PRIu32"",min);
                } else {
                    sprintf(neighborM, "%"PRIu32"",min);
                }
                strcat(msg,neighborM);
                strcat(msg,":");
                strcat(msg,leader);
                strcat(msg,";");
                strcat(msg,myIPv6);
                ipc_msg_send(msg, udpServerPID, false);
            }
        }
        xtimer_usleep(50000); // wait 0.05 seconds
    }

    return 0;
}

// END MY CUSTOM THREAD DEFS
// ************************************
