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

// Inlcude leader election parameters
#include "leaderElectionParams.h"

// Size definitions
#define CHANNEL                 11
#define SERVER_MSG_QUEUE_SIZE   (32)
#define SERVER_BUFFER_SIZE      (128)
#define IPV6_ADDRESS_LEN        (22)
#define MAX_NEIGHBORS           (40)

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

/*
int getIndexOfSuffix(char* ip) {
    int j;
    int c = 0;
    for (j = 0; j < (int)strlen(ip); j++)
    {
        if (ip[j] == ':')
            c++;
        if (c == 4)
            return j;
    }
    return -1;
}*/

// Purpose: main code for the UDP serverS
void *_udp_server(void *args)
{
    (void)args;
    msg_init_queue(server_msg_queue, SERVER_MSG_QUEUE_SIZE);

    // IPv6 address variables
    char IPv6_1[46] = { 0 };              // holder for an address
    char IPv6_2[46] = { 0 };              // holder for an address
    char masterIPv6[46] = "unknown";      // address of master node
    char myIPv6[IPV6_ADDRESS_LEN] = "unknown";          // my address
    char leaderIPv6[IPV6_ADDRESS_LEN] = "unknown";      // the "leader so far"
    char newLeaderIPv6[IPV6_ADDRESS_LEN] = "unknown";   // leader of the round
    char ipv6_unique[IPV6_ADDRESS_LEN] = { 0 };
    char ipv6_prefix[7] = "fe80::";
    //char ipv6_suffix[12] = { 0 };

    // other string representation variables
    char portBuf[6] = { 0 };    // string rep of server port
    char codeBuf[10] = { 0 };   // junk array for message headers
    char mStr[5] = { 0 };       // string rep of a 3 digit value
    char messages[10] = { 0 };  // string rep for number of messages
    char offset[15] = { 0 };
    char seconds[15] = { 0 };
    char decimal[15] = { 0 };

    // buffers
    char msg[SERVER_BUFFER_SIZE] = { 0 };                           // pre-allocated msg
    char *msgP = (char*)calloc(SERVER_BUFFER_SIZE, sizeof(char));   // dynamic msg

    // other component variables
    int i = 0;                  // a loop counter
    bool discovered = false;    // have we been discovered by master
    bool topoComplete = false;  // did we learn our neighbors
    bool identComplete = false;
    int rconf = 0;              // did master confirm our results
    int res = 0;                // return value from socket
    bool polled = false;        // have missing nodes been polled yet
    int sendRes = 0;            // result send attempts
    int tMsgs = 0;

    bool discovering = false;
    uint32_t lastDiscover = 0;
    uint32_t wait = 2*1000000;
    int resetDiscoverLoops = 15;//LE_K/2 + 1;
    int discoverLoops = resetDiscoverLoops;

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
    uint32_t convergenceTimeLE = 0; // protocol runtime
    bool gen = false;
    //bool hasElectedLeader = false;  // has a leader been elected
    //int loopCount = 0;

    // neighbor variables
    int numNeighbors = 0;   // number of neighbors
    //int tempNumNeighbors = 0;   // number of neighbors
    char **neighbors = (char**)calloc(MAX_NEIGHBORS, sizeof(char*));    // list of neighbors
    for(i = 0; i < MAX_NEIGHBORS; i++) {
        neighbors[i] = (char*)calloc(IPV6_ADDRESS_LEN, sizeof(char));   // neighbor addresses
    }
    /*
    char **tempNeighbors = (char**)calloc(MAX_NEIGHBORS, sizeof(char*));    // list of neighbors
    for(i = 0; i < MAX_NEIGHBORS; i++) {
        tempNeighbors[i] = (char*)calloc(IPV6_ADDRESS_LEN, sizeof(char));   // neighbor addresses
    }
    */
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
    printf("UPD: K = %d\n", counter);

    int expNum = 1;
    while (true) { // loop forever, so long as master keeps starting
        printf("UDP: starting experiment %d\n", expNum);

        // main server loop
        while (1) {
            //loopCount += 1;
            //if (loopCount % 10 == 0)
                //printf("TEST: top, runningLE=%s, myMin=%"PRIu32", myIPv6=%s\n", runningLE ? "yes" : "no", local_min, myIPv6);
            // incoming UDP
            memset(server_buffer, 0, SERVER_BUFFER_SIZE);
            memset(msg, 0, SERVER_BUFFER_SIZE);
            memset(msgP, 0, SERVER_BUFFER_SIZE);
            memset(mStr, 0, 5);
            memset(IPv6_1, 0, 46);
            memset(IPv6_2, 0, 46);

            if (server_buffer == NULL || SERVER_BUFFER_SIZE - 1 <= 0) {
                printf("ERROR: failed sock_udp_recv preconditions\n");
                return NULL;
            }

            // discover nodes
            if (discovering && lastDiscover + wait < xtimer_now_usec()) {
                // multicast to find nodes
                if (discoverLoops == 0) {
                    topoComplete = true;
                    discovering = false;
                    lastDiscover = 0;
                    discoverLoops = resetDiscoverLoops;
                } else {
                    strcpy(msg, "disc;");
                    /*for (i = 0; i < tempNumNeighbors; i++) {
                        memset(IPv6_2, 0, 46);
                        strcat(IPv6_2, ipv6_prefix);
                        strcat(IPv6_2, tempNeighbors[i]);

                        char *argsMsg[] = { "udp_send", IPv6_2, portBuf, msg, NULL };
                        udp_send(4, argsMsg);
                    }*/
                    char *argsMsg[] = { "udp_send_multi", portBuf, msg, NULL };
                    udp_send_multi(3, argsMsg);
                    discoverLoops--;
                    lastDiscover = xtimer_now_usec();
                    memset(msg, 0, SERVER_BUFFER_SIZE);
                }
            }

            if ((res = sock_udp_recv(&my_sock, server_buffer,
                     SERVER_BUFFER_SIZE - 1, 0.005 * US_PER_SEC, //SOCK_NO_TIMEOUT,
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
                countMsgIn();
                ipv6_addr_to_str(IPv6_1, (ipv6_addr_t *)&remote.addr.ipv6, 46);

                //int len = strlen(IPv6_1)-6;
                memset(ipv6_unique, 0, IPV6_ADDRESS_LEN);
                //strncpy(ipv6_unique, IPv6_1+6, len);
                strcpy(ipv6_unique, IPv6_1+6);
                //ipv6_unique[len] = '\0';

                printf("IP: %s\n", ipv6_unique);

                if (DEBUG == 1) {
                    printf("UDP: recvd size=%d, %s from %s\n", res, server_buffer, IPv6_1);
                }
            }

            // react to UDP message
            if (res >= 1) {
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
                    if (!identComplete) {
                        strcpy(masterIPv6, IPv6_1); // redundant
                        discovered = true;

                        //strcpy(msgP, server_buffer);
                        char *mem = server_buffer;

                        extractMsgSegment(&mem, codeBuf);

                        memset(mStr, 0, 5);
                        extractMsgSegment(&mem,mStr);   // extract my m value
                        m = (uint32_t)atoi(mStr);       // convert to integer
                        local_min = m;                  // I am the starting local_min

                        memset(leaderIPv6, 0, IPV6_ADDRESS_LEN);
                        memset(myIPv6, 0, IPV6_ADDRESS_LEN);
                        extractMsgSegment(&mem, myIPv6);
                        strcpy(leaderIPv6, myIPv6); // I am the starting leader
                        //extractMsgSegment(&mem, ipv6_suffix);

                        printf("UDP: my m/IP = %"PRIu32"/%s\n", m,myIPv6);

                        identComplete = true;
                    }

                    printf("UDP: master node (%s) confirmed us\n", masterIPv6);

                // information about our IP and neighbors
                } else if (strncmp(server_buffer,"ips;",4) == 0) {
                    // process IP and neighbors
                    if (!topoComplete) {
                        strcpy(msgP, server_buffer);
                        char *mem = msgP;

                        extractMsgSegment(&mem,codeBuf);

                        if (DEBUG == 1) {                    
                            printf("UDP: ips = %s\n", mem);
                        }

                        // extract neighbors IPs from message
                        while(strlen(mem) > 1) {
                            extractMsgSegment(&mem,neighbors[numNeighbors]);
                            numNeighbors++;
                        }
                        
                        topoComplete = true;
                        gen = false;
                    }

                // information about our IP and neighbors for discovery
                } else if (strncmp(server_buffer,"ipsd;",5) == 0) {
                    // process IP and neighbors
                    if (!topoComplete) {
                        strcpy(msgP, server_buffer);
                        char *mem = msgP;

                        extractMsgSegment(&mem,codeBuf);

                        if (DEBUG == 1) {                    
                            printf("UDP: ips = %s\n", mem);
                        }

                        // extract neighbors IPs from message
                        /*
                        while(strlen(mem) > 1) {
                            extractMsgSegment(&mem,tempNeighbors[tempNumNeighbors]);
                            tempNumNeighbors++;
                        }
                        printf("LE: Received network info, %d possible neighbors:\n",tempNumNeighbors);
                        for (i = 0; i < tempNumNeighbors; i++) {
                            if (strcmp(tempNeighbors[i],"") == 0) {
                                continue;
                            }
                            printf("%2d: %s\n", i+1, tempNeighbors[i]);
                        }
                        */
                        
                        topoComplete = true;
                        discovering = true;
                        lastDiscover = 0;
                        gen = true;
                    }
                
                // start discovery
                } else if (strncmp(server_buffer,"discover;",9) == 0) {
                    discovering = true;
                    lastDiscover = 0;

                    numNeighbors = 0;
                    //tempNumNeighbors = 0;
                    for(i = 0; i < MAX_NEIGHBORS; i++) {
                        memset(neighbors[i], 0, IPV6_ADDRESS_LEN);
                        //memset(tempNeighbors[i], 0, IPV6_ADDRESS_LEN);
                        memset(neighborsLeaders[i], 0, IPV6_ADDRESS_LEN);
                        neighborsVal[i] = 257;
                    } 
                    gen = true;
                // start leader election
                } else if (strncmp(server_buffer,"start;",6) == 0) {
                    if (runningLE) {
                        messagesIn -= 1;
                        //continue;
                    }
                    else {
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
                            xtimer_usleep(5000000); // wait 5 seconds and continue
                            break;
                        }

                        // set some initial values
                        printf("LE: Initiating leader election...\n");
                        runningLE = true;
                        startTimeLE = xtimer_now_usec();
                        counter = LE_K;
                        stateLE = 0;
                    }

                } else if (strncmp(server_buffer,"disc;",5) == 0) {
                    //printf("UDP: discovering %s\n", ipv6_unique);
                    // if node with this ipv6 is already found, ignore
                    // otherwise record them
                    int found = alreadyANeighbor(neighbors, ipv6_unique);
                    //printf("For IP=%s, found=%d\n", ipv6, found);
                    if (found == 0 && numNeighbors < MAX_NEIGHBORS) {
                        strcpy(neighbors[numNeighbors], ipv6_unique);
                        if (DEBUG == 1) {
                            printf("UDP: recorded new node, %s\n", neighbors[numNeighbors]);
                        }

                        //memset(IPv6_2, 0, 46);
                        //strcat(IPv6_2, ipv6_prefix);
                        //strcat(IPv6_2, neighbors[numNeighbors]);
                        
                        numNeighbors++;

                        //strcpy(msg, "disc;");
                        //char *argsMsg[] = { "udp_send", IPv6_2, portBuf, msg, NULL };
                        //udp_send(4, argsMsg);
                        //memset(msg, 0, SERVER_BUFFER_SIZE);
                    }
                    //printf("UDP: bottom discovering\n");
                } else if (strncmp(server_buffer,"le_ack;",7) == 0) {
                    // *** message handling component of pseudocode lines 6, 7, and 8g
                    if (runningLE) {
                        strcpy(msgP, server_buffer);
                        char *mem = msgP;
                        uint32_t localM = 257;
                        memset(IPv6_2, 0, IPV6_ADDRESS_LEN);
                        memset(mStr, 0, 5);

                        extractMsgSegment(&mem,codeBuf);    // remove header

                        if (DEBUG == 1) {
                            printf("LE: m_msg = %s\n", server_buffer);
                        }

                        extractMsgSegment(&mem,mStr);       // get m value
                        extractMsgSegment(&mem,IPv6_2);     // obtain owner ID
                        i = getNeighborIndex(neighbors, ipv6_unique);  // check the sender/neighbor

                        if (i < 0) {
                            printf("ERROR: sender of message not found in neighbor list (%s)\n", IPv6_1);
                            //continue;
                        }
                        else {
                            localM = (uint32_t)atoi(mStr);
                            if (localM <= 0 || localM >= 256) {
                                printf("ERROR: le_ack, m value is out of range, %"PRIu32"\n", localM);
                                //continue;
                            }
                            else {
                                countedMs++;
                                neighborsVal[i] = localM;
                                memset(neighborsLeaders[i], 0, IPV6_ADDRESS_LEN);
                                strcpy(neighborsLeaders[i], IPv6_2);

                                printf("LE: m value %"PRIu32"//%s received from %s\n", neighborsVal[i], neighborsLeaders[i], IPv6_1);
                            }
                        }
                    }

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
            if (runningLE) {

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

                        memset(IPv6_2, 0, 46);
                        strcat(IPv6_2, ipv6_prefix);
                        strcat(IPv6_2, neighbors[i]);

                        char *argsMsg[] = { "udp_send", IPv6_2, portBuf, msg, NULL };
                        udp_send(4, argsMsg);
                        
                        xtimer_usleep(1000); // wait 0.001 seconds
                    }

                    stateLE = 1;
                    lastT = xtimer_now_usec();

                // *** lines 6-7
                } else if (stateLE == 1) {
                    if (lastT < xtimer_now_usec() - LE_T) {     // line 6
                        if (!polled) {
                            if (!gen) {
                                strcpy(msg, "le_m?;");
                                for (i = 0; i < numNeighbors; i++) {    // line 7
                                    if (neighborsVal[i] == 257) {
                                        // poll this missing neighbor

                                        memset(IPv6_2, 0, 46);
                                        strcat(IPv6_2, ipv6_prefix);
                                        strcat(IPv6_2, neighbors[i]);

                                        char *argsMsg[] = { "udp_send", IPv6_2, portBuf, msg, NULL };
                                        udp_send(4, argsMsg);
                                        xtimer_usleep(1000); // wait 0.001 seconds
                                    }
                                }
                            }
                            polled = true;
                            lastT = xtimer_now_usec(); // wait for LE_T
                        } else {
                            int quit = 1;
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
                                } else {
                                    quit = 0;
                                }
                            }
                            quit = 0;
                            
                            if (quit) break;

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

                        if (DEBUG == 1) {
                            printf("\nLE: min/newMin %"PRIu32"/%"PRIu32"\n", local_min, new_local_min);
                            printf("LE: leader/newLeader, %s/%s\n", leaderIPv6, newLeaderIPv6);
                        }

                        for (i = 0; i < numNeighbors; i++) {
                            if (DEBUG == 1) {
                                printf(" %d: m=%"PRIu32", curLeader=%s\n", i+1, neighborsVal[i], neighborsLeaders[i]);
                            }

                            // don't have values from this neighbor, skip them
                            if (neighborsVal[i] <= 0 || neighborsVal[i] >= 256) {
                                //printf("  ERROR: stateLE==2, m value is out of range, %"PRIu32"\n", neighborsVal[i]);
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
                        printf("LE: counter reduced to %d\n", counter);

                        // new leader found, either by m value or tie break
                        if (strcmp(leaderIPv6, newLeaderIPv6) != 0) { // *** line 8d of pseudocode
                            printf("LE: new leader, new_local_min %"PRIu32" < %"PRIu32", heard from %d nodes\n", new_local_min, local_min, countedMs);

                            local_min = new_local_min;  // *** line 8dii of pseudocode
                            memset(leaderIPv6, 0, IPV6_ADDRESS_LEN);
                            strcpy(leaderIPv6, newLeaderIPv6); // *** line 8diii of pseudocode                

                            // send out new info: le_ack:m;leader;
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

                            if (gen) {
                                // broadcast
                                char *argsMsg[] = { "udp_send_multi", portBuf, msg, NULL };
                                udp_send_multi(3, argsMsg);
                            } else {
                                // unicast
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

                                    memset(IPv6_2, 0, 46);
                                    strcat(IPv6_2, ipv6_prefix);
                                    strcat(IPv6_2, neighbors[i]);

                                    char *argsMsg[] = { "udp_send", IPv6_2, portBuf, msg, NULL };
                                    udp_send(4, argsMsg);
                                    
                                    xtimer_usleep(1000); // wait 0.001 seconds
                                }
                            }
                        }

                        // quit, *** lines 8e and 8ei
                        else if (counter < 0) {
                            printf("LE: counter < 0 so quit\n");
                            lastT = 0;
                            stateLE = 3;

                            // compute runtime
                            int digits = 0;
                            endTimeLE = xtimer_now_usec();
                            convergenceTimeLE = (endTimeLE - startTimeLE);
                            memset(offset, 0, 15);
                            memset(seconds, 0, 15);
                            memset(decimal, 0, 15);

                            if (convergenceTimeLE >= 1000000) {
                                digits = 6;
                            } else if (convergenceTimeLE >= 100000) {
                                digits = 5;
                            } else if (convergenceTimeLE >= 10000) {
                                digits = 4;
                            } // smaller shouldn't happen ever

                            if (digits == 6) {
                                // last 6 is fractional seconds, before that is seconds
                                sprintf(offset, "%"PRIu32, convergenceTimeLE);
                                substr(offset, 0, strlen(offset)-6, seconds);
                                substr(offset, strlen(offset)-6, 6, decimal);

                                memset(offset, 0, 15);
                                sprintf(offset, "%s.%s", seconds, decimal);
                            } else if (digits == 5) {
                                // value is fractional seconds, 0 whole seconds
                                sprintf(offset, "0.%"PRIu32, convergenceTimeLE);
                            } else if (digits == 4) {
                                // whole number is fractional seconds, 0 whole seconds
                                sprintf(offset, "0.0%"PRIu32, convergenceTimeLE);
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
                    // send results every second until confirmed
                    if (rconf == 0 && lastT < xtimer_now_usec() - 1000000) {
                        // display election result
                        if (sendRes == 0) {
                            printf("\nLE: %s elected as the leader, via m=%"PRIu32"!\n", leaderIPv6, local_min);
                            if (strcmp(leaderIPv6, myIPv6) == 0) {
                                printf("LE: Hey, that's me! I'm the leader!\n");
                            }

                            tMsgs = messagesIn+messagesOut;
                            printf("LE:    start=%"PRIu32"\n", startTimeLE);
                            printf("LE:      end=%"PRIu32"\n", endTimeLE);
                            printf("LE: converge=%"PRIu32"\n", convergenceTimeLE);
                            printf("LE: messages=%d\n\n", tMsgs);

                            //hasElectedLeader = true;
                            countedMs = 0;
                        }

                        // build results package
                        strcpy(msg, "results;");
                        strcat(msg, leaderIPv6);
                        strcat(msg, ";");

                        // Runtime
                        strcat(msg, offset);
                        strcat(msg, ";");

                        memset(messages, 0, 10);
                        sprintf(messages, "%d" , tMsgs);
                        strcat(msg, messages);
                        strcat(msg, ";");
                        memset(messages, 0, 10);
                        sprintf(messages, "%d" , numNeighbors);
                        strcat(msg, messages);
                        strcat(msg, ";");

                        printf("LE: attempt %d of sending results to master\n", sendRes);

                        // send results
                        char *argsMsg[] = { "udp_send", masterIPv6, portBuf, msg, NULL };
                        udp_send(4, argsMsg);

                        sendRes += 1;
                        lastT = xtimer_now_usec(); 
                    } else if (rconf == 1 || sendRes >= 20) { // try for 20 seconds
                        runningLE = false;
                        break;
                    }
                } else {
                    printf("ERROR: leader election in invalid state %d\n", stateLE);
                    break;
                }
            }

            //printf("***BOTTOM***");
            xtimer_usleep(1000); // wait 0.001 seconds
        }

        // reset variables
        numNeighbors = 0;
        //tempNumNeighbors = 0;
        for(i = 0; i < MAX_NEIGHBORS; i++) {
            memset(neighbors[i], 0, IPV6_ADDRESS_LEN);
            //memset(tempNeighbors[i], 0, IPV6_ADDRESS_LEN);
            memset(neighborsLeaders[i], 0, IPV6_ADDRESS_LEN);
            neighborsVal[i] = 257;
        } 

        int z = 20;
        while (z > 0) {
            //printf("clearing queue");
            sock_udp_recv(&my_sock, server_buffer,
                 SERVER_BUFFER_SIZE - 1, 0, //SOCK_NO_TIMEOUT,
                 &remote);
            z -= 1;
        }

        runningLE = false;
        messagesIn = 0;
        messagesOut = 0;
        tMsgs = 0;

        discovered = false;
        topoComplete = false;
        identComplete = false;
        rconf = 0;
        res = 0;
        polled = false; 
        sendRes = 0;

        m = 257;
        local_min = 257; 
        new_local_min = 257;
        counter = LE_K;
        stateLE = 0;
        countedMs = 0;
        lastT = 0;
        startTimeLE = 0;
        endTimeLE = 0;
        convergenceTimeLE = 0;

        //memset(masterIPv6, 0, 46);
        //memset(myIPv6, 0, IPV6_ADDRESS_LEN);
        memset(leaderIPv6, 0, IPV6_ADDRESS_LEN);
        memset(newLeaderIPv6, 0, IPV6_ADDRESS_LEN);

        strcpy(newLeaderIPv6, "unknown");

        if (DEBUG == 1) {
            printf("UDP: variables reset, starting new experiment\n");
        }

        expNum++;
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
        printf("UDP: Error (%d) - could not send message \"%s\" to %s\n", res, argv[3], argv[1]);
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
// argv char**, list of arguments ("udps")
int udp_server(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        puts("MAIN: Usage - udps");
        return -1;
    }

    kernel_pid_t pid = 0;
    if (server_running == false) {
        printf("MAIN: before thread_create\n");
        pid = thread_create(server_stack, sizeof(server_stack), THREAD_PRIORITY_MAIN - 1,
                      THREAD_CREATE_STACKTEST, _udp_server, NULL, "UDP_Server_Thread");
        printf("MAIN: after thread_create\n");
        if (pid <= KERNEL_PID_UNDEF)
        {
            printf("MAIN: Error - failed to start UDP server thread\n");
            return -1;
        }
    }

    return (int)pid;
}
