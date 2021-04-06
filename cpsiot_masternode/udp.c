/*
 * Original sample file taken from: https://github.com/RIOT-OS/Tutorials/tree/master/task-06
 *
 * All changes and final product:
 * @author  Michael Conard <maconard@mtu.edu>
 *
 * Purpose: A UDP server designed to work as the master node to do topology setup on iot-lab.
 */

// Standard C includes
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <msg.h>
#include <math.h>

// Standard RIOT includes
#include "thread.h"
#include "xtimer.h"
#include "random.h"

// Networking includes
#include "net/sock/udp.h"
#include "net/ipv6/addr.h"

// Inlcude leader election parameters
#include "leaderElectionParams.h"

#define CHANNEL                 11

#define SERVER_MSG_QUEUE_SIZE   (64)
#define SERVER_BUFFER_SIZE      (256)
#define MAX_IPC_MESSAGE_SIZE    (256)
#define IPV6_ADDRESS_LEN        (22)

#define MAX_NODES               (70)
#define MAX_EXP                 (10)

#define DEBUG                   (0)

// Forward declarations
void *_udp_server(void *args);
int udp_send(int argc, char **argv);
int udp_send_multi(int argc, char **argv);
int udp_server(int argc, char **argv);
int alreadyANeighbor(char **neighbors, char *ipv6);
int getNeighborIndex(char **neighbors, char *ipv6);

//External functions defs
extern void substr(char *s, int a, int b, char *t);
extern int indexOfSemi(char *ipv6);
extern void extractMsgSegment(char **s, char *t);

// Data structures (i.e. stacks, queues, message structs, etc)
static char server_buffer[SERVER_BUFFER_SIZE];
static char server_stack[THREAD_STACKSIZE_DEFAULT];
static msg_t server_msg_queue[SERVER_MSG_QUEUE_SIZE];
static sock_udp_t sock;

// State variables
static bool server_running = false;
const int SERVER_PORT = 3142;
uint32_t unixTime;
uint32_t syncTime;

// Purpose: determine if an ipv6 address is already registered
//
// neighbors char**, list of registered neighbors
// ipv6 char*, the address to check for
int alreadyANeighbor(char **neighbors, char *ipv6) {
    for(int i = 0; i < MAX_NODES; i++) {
        if(strcmp(neighbors[i], ipv6) == 0) return 1;
    }
    return 0;
}

// Purpose: retrieve the internal index of an address
//
// neighbors char**, list of registered neighbors
// ipv6 char*, ipv6 to check for
int getNeighborIndex(char **neighbors, char *ipv6) {
    for(int i = 0; i < MAX_NODES; i++) {
        if(strcmp(neighbors[i], ipv6) == 0) return i;
    }
    return -1;
}

// Purpose: return the log base k of x
//
// x, the number to take the log of
// k, the base to use in the log
float logk(int x, int k) {
    return log10(x) / log10(k);
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

// Purpose: main code for the UDP server
void *_udp_server(void *args)
{
    (void)args;
    printf("UDP: Entered UDP server code\n");
    // socket server setup
    sock_udp_ep_t server = { .port = SERVER_PORT, .family = AF_INET6 };
    sock_udp_ep_t remote;
    msg_init_queue(server_msg_queue, SERVER_MSG_QUEUE_SIZE);

    char ipv6[30] = { 0 };
    char tempipv6[30] = { 0 };
    char ipv6_unique[20] = { 0 };
    char ipv6_prefix[7] = "fe80::";
    //char ipv6_suffix[12] = { 0 };

    char tempunixtime[15] = { 0 };
    char tempunixsec[15] = { 0 };
    char tempdegree[15] = { 0 };
    char temprunsec[15] = { 0 };
    char tempmessagecount[5] = { 0 };
    int m_values[MAX_NODES] = { 0 };
    int confirmed[MAX_NODES] = { 0 };
    char mStr[5] = { 0 };
    char portBuf[6];
    char codeBuf[10];
    char timeBuf[32];
    char rconf[6] = "rconf;";
    sprintf(portBuf,"%d",SERVER_PORT);
    uint32_t startTime;
    uint32_t resBegin = 0;

    char offset[15] = { 0 };
    char seconds[15] = { 0 };
    char decimal[15] = { 0 };

    int numNodes = 0;
    int numNodesFinished = 0;
    int finished = 0;
    int i;
    char **nodes = (char**)calloc(MAX_NODES, sizeof(char*));
    for(i = 0; i < MAX_NODES; i++) {
        nodes[i] = (char*)calloc(IPV6_ADDRESS_LEN, sizeof(char));
    }

    int numCorrect = 0;
    char **expStarts = (char**)calloc(MAX_EXP, sizeof(char*));
    for(i = 0; i < MAX_EXP; i++) {
        expStarts[i] = (char*)calloc(15, sizeof(char));
    }

    char **expRuns = (char**)calloc(MAX_EXP, sizeof(char*));
    for(i = 0; i < MAX_EXP; i++) {
        expRuns[i] = (char*)calloc(15, sizeof(char));
    }

    int failedNodes = 0;
    int correctNodes = 0;
    int minMsgs = 0;
    int maxMsgs = 0;
    int sumMsgs = 0;
    int min = 257;
    int minIndex = -1;
    float maxRun = 0.0;

    char msg[SERVER_BUFFER_SIZE] = { 0 };
    char *msgP = (char*)calloc(SERVER_BUFFER_SIZE, sizeof(char));

    uint32_t lastDiscover = 0;
    uint32_t wait = 2*1000000;
    int resetDiscoverLoops = 3;
    int discoverLoops = 3; // 10sec of discovery

    // create the socket
    if(sock_udp_create(&sock, &server, NULL, 0) < 0) {
        return NULL;
    }

    server_running = true;
    printf("UDP: Success - started UDP server on port %u\n\n", server.port);
    //printf("UDP: set to 30 two-second rounds for 60 seconds of node discovery\n");
    //printf("UDP: you should have approx nodes/2 discovery rounds, update it with \"rounds <num>\"\n");
    printf("UDP: I will generate a %s topology\n", MY_TOPO);
    printf("UDP: waiting for clock sync\n");
    
    msg_t msg_u_in;
    int res = msg_try_receive(&msg_u_in);
    while (true) {
        if (res > 0) {
            strncpy(msg, (char*)msg_u_in.content.ptr, 32);
            //printf("UDP: read startup msg: %s\n", msg);
            char* mem = msg;
            memset(codeBuf, 0, 10);
            memset(timeBuf, 0, 32);
            extractMsgSegment(&mem, codeBuf);  // chop off the header string
            extractMsgSegment(&mem, timeBuf);  // collect param

            //printf("code: %s, param: %s\n", codeBuf, timeBuf);

            if (strncmp(codeBuf,"rounds",6) == 0) {
                int newLoops = atoi(timeBuf);
                printf("UDP: discover loops changed from %d to %d\n", discoverLoops, newLoops);
                discoverLoops = newLoops;
                resetDiscoverLoops = newLoops;
            } else if (strncmp(codeBuf,"unix",4) == 0) {
                unixTime = atoi(timeBuf);
                syncTime = xtimer_now_usec();
                printf("UDP: clock synced to unix %"PRIu32"\n", unixTime);
                break;
            }
        }
        xtimer_usleep(100000); // wait 0.1 seconds
        res = msg_try_receive(&msg_u_in);
    }

    int expNum = 1;
    while (numCorrect < MAX_EXP) {  // run the experiment for 10 success
        printf("Starting experiment %d... (%d correct, %d failed)\n", expNum, numCorrect, expNum-numCorrect-1);
        // main server loop
        while (1) {
            memset(msg, 0, SERVER_BUFFER_SIZE);
            memset(msgP, 0, SERVER_BUFFER_SIZE);
            memset(server_buffer, 0, SERVER_BUFFER_SIZE);
            memset(ipv6, 0, 30);

            // discover nodes
            if (lastDiscover + wait < xtimer_now_usec()) {
                // multicast to find nodes
                if (discoverLoops == 0) break;

                strcpy(msg, "ping;");
                char *argsMsg[] = { "udp_send_multi", portBuf, msg, NULL };
                udp_send_multi(3, argsMsg);
                discoverLoops--;
                lastDiscover = xtimer_now_usec();
                memset(msg, 0, SERVER_BUFFER_SIZE);
            }
        
            // incoming UDP
            int res;
            if ((res = sock_udp_recv(&sock, server_buffer,
                         sizeof(server_buffer) - 1, 0.005 * US_PER_SEC, //SOCK_NO_TIMEOUT,
                         &remote)) < 0) {
                if(res != 0 && res != -ETIMEDOUT && res != -EAGAIN && DEBUG == 1) {
                    printf("UDP: Error - failed to receive UDP, %d\n", res);
                }
            }
            else if (res == 0) {
                if (DEBUG == 1) {
                    (void) puts("UDP: no UDP data received");
                }
            }
            else {
                server_buffer[res] = '\0';
                res = 1;
                ipv6_addr_to_str(ipv6, (ipv6_addr_t *)&remote.addr.ipv6, 47);

                //int c = getIndexOfSuffix(ipv6);
                //if (strcmp(ipv6_suffix, "") == 0) {
                //    strncpy(ipv6_suffix, ipv6+c, strlen(ipv6)-c);
                //    printf("UDP: discovered IP suffix: %s\n", ipv6_suffix);
                //}
                int len = strlen(ipv6)-6;
                memset(ipv6_unique, 0, IPV6_ADDRESS_LEN);
                strncpy(ipv6_unique, ipv6+6, len);
                ipv6_unique[len] = '\0';

                if (DEBUG == 1) {
                    printf("UDP: recvd: %s from %s\n", server_buffer, ipv6);
                }
            }

            // react to UDP message
            if (res == 1) {
                // a node has responded to our discovery request
                if (strncmp(server_buffer,"pong;",5) == 0) {
                    // if node with this ipv6 is already found, ignore
                    // otherwise record them
                    int found = alreadyANeighbor(nodes, ipv6_unique);
                    //printf("For IP=%s, found=%d\n", ipv6, found);
                    if (found == 0 && numNodes < MAX_NODES) {
                        strcpy(nodes[numNodes], ipv6_unique);
                        if (DEBUG == 1) {
                            printf("UDP: recorded new node, %s\n", nodes[numNodes]);
                        }
                        m_values[numNodes] = (random_uint32() % 254)+1;

                        if (m_values[numNodes] < min) {
                            minIndex = numNodes;
                            min = m_values[minIndex];
                        } else if (m_values[numNodes] == min) {
                            int tie = strcmp(nodes[minIndex], nodes[numNodes]);
                            if (tie > 0) { // new node won the tie
                                minIndex = numNodes;
                                min = m_values[minIndex];
                            }
                        }

                        memset(mStr, 0, 5);
                        sprintf(mStr, "%d;", m_values[numNodes]);

                        strcpy(msg, "conf;");
                        strcat(msg, mStr);
                        strcat(msg, nodes[numNodes]);
                        strcat(msg, ";");

                        //printf("Confirming %s, sending %s\n", nodes[numNodes], msg);
                        
                        numNodes++;
                    
                        // send back discovery confirmation
                        char *argsMsg[] = { "udp_send", ipv6, portBuf, msg, NULL };
                        udp_send(4, argsMsg);
                    }
                }
            }

            //xtimer_usleep(5000); // wait 0.005 seconds
        }

        //if (DEBUG == 1)
            printf("Found %d nodes:\n\n",numNodes);
/*
        for (i = 0; i < MAX_NODES; i++) {
            if (strcmp(nodes[i],"") == 0) 
                continue;
            //if (DEBUG == 1) 
                printf("%2d: %s m=%d\n", i, nodes[i], m_values[i]);
        }

        //if (DEBUG == 1)
            printf("\n");
*/
        memset(msg, 0, SERVER_BUFFER_SIZE);
        memset(msgP, 0, SERVER_BUFFER_SIZE);
        memset(server_buffer, 0, SERVER_BUFFER_SIZE);

/*
        int q;
        for (i = 0; i < numNodes; i++) {
            strcpy(msg, "you;");
            memset(mStr, 0, 5);
            sprintf(mStr, "%d;", m_values[i]);
            strcat(msg, mStr);
            strcat(msg, nodes[i]);
            strcat(msg, ";");
            strcat(msg, ipv6_suffix);
            strcat(msg, ";");

            memset(ipv6, 0, 30);
            strcat(ipv6, ipv6_prefix);
            strcat(ipv6, nodes[i]);
            strcat(ipv6, ipv6_suffix);

            for (q = 0; q < 2; q++) { //send ip info 2 times
                //if (DEBUG == 1) {
                    printf("UDP: sending m/identify info to %s\n", nodes[i]);
                //}

                char *argsMsg[] = { "udp_send", ipv6, portBuf, msg, NULL };
                udp_send(4, argsMsg);
                xtimer_usleep(100000); // wait .1 seconds
            }
            xtimer_usleep(10000); // wait .01 seconds
        }

        memset(msg, 0, SERVER_BUFFER_SIZE);
        memset(msgP, 0, SERVER_BUFFER_SIZE);
        memset(server_buffer, 0, SERVER_BUFFER_SIZE);

        xtimer_usleep(500000); // wait 0.5 seconds
*/

        // send out topology info to all discovered nodes
        if (strcmp(MY_TOPO,"ring") == 0) {
            // compose message, "ips:<m>;<yourIP>;<neighbor1>;<neighbor2>;
            if (DEBUG == 1)
                printf("UDP: generating ring topology\n");
            printf("node, neighborID, neighbors\n");
            int j;
            for (j = 0; j < 3; j++) { // send topology info 3 time(s)
                for (i = 0; i < numNodes; i++) {
                    memset(msg, 0, SERVER_BUFFER_SIZE);

                    int pre = (i-1);
                    int post = (i+1);

                    if (pre == -1) { pre = numNodes-1; }
                    if (post == numNodes) { post = 0; }

                    if (j == 0) {
                        printf("%s, %d, %d %d\n", nodes[i], i, pre, post);
                    }
                    strcpy(msg, "ips;");

                    strcat(msg, nodes[pre]);
                    strcat(msg, ";");
                    strcat(msg, nodes[post]);
                    strcat(msg, ";");

                    if (j == 0 && DEBUG == 1) {
                        printf("UDP: Sending node %d's info: %s\n", i, msg);
                    }

                    memset(ipv6, 0, 30);
                    strcat(ipv6, ipv6_prefix);
                    strcat(ipv6, nodes[i]);

                    char *argsMsg[] = { "udp_send", ipv6, portBuf, msg, NULL };
                    udp_send(4, argsMsg);
                    xtimer_usleep(1000); // wait .001 seconds
                }
                xtimer_usleep(500000); // wait 0.5 seconds
            }
            if (DEBUG == 1) {
                printf("UDP:");
                for (i = 0; i < numNodes; i++) {
                    if (i < numNodes-1)
                        printf(" %d <->", i);
                    else
                        printf(" %d <-> 0\n", i);
                }
            }

        } else if (strcmp(MY_TOPO,"line") == 0) {
            if (DEBUG == 1)
                printf("UDP: generating line topology\n");
            printf("node, neighborID, neighbors\n");
            int j;
            for (j = 0; j < 3; j++) { // send topology info 3 time(s)
                for (i = 0; i < numNodes; i++) {
                    memset(msg, 0, SERVER_BUFFER_SIZE);

                    int pre = (i-1);
                    int post = (i+1);

                    if (j == 0) {
                        if (pre == -1)
                            printf("%s, %d, %d\n", nodes[i], i, post);
                        else if (post == numNodes) 
                            printf("%s, %d, %d\n", nodes[i], i, pre);
                        else
                            printf("%s, %d, %d %d\n", nodes[i], i, pre, post);
                    }

                    strcpy(msg, "ips;");

                    if (pre != -1) {
                        strcat(msg, nodes[pre]);
                        strcat(msg, ";");
                    }
                    if (post != numNodes) {
                        strcat(msg, nodes[post]);
                        strcat(msg, ";");
                    }

                    if (j == 0 && DEBUG == 1) {
                        printf("UDP: Sending node %d's info: %s\n", i, msg);
                    }

                    memset(ipv6, 0, 30);
                    strcat(ipv6, ipv6_prefix);
                    strcat(ipv6, nodes[i]);

                    char *argsMsg[] = { "udp_send", ipv6, portBuf, msg, NULL };
                    udp_send(4, argsMsg);
                    xtimer_usleep(1000); // wait .001 seconds
                }
                xtimer_usleep(500000); // wait 0.5 seconds
            }
            if (DEBUG == 1) {
                printf("UDP:");
                for (i = 0; i < numNodes; i++) {
                    if (i < numNodes-1)
                        printf(" %d <->", i);
                    else
                        printf(" %d\n", i);
                }
            }
        } else if (strcmp(MY_TOPO,"tree") == 0) {
            if (DEBUG == 1)
                printf("UDP: generating tree topology\n");
            printf("node, neighborID, neighbors\n");
            int depth = (int)logk(numNodes,2);
            if (DEBUG == 1)
                printf("UDP: numNodes=%d, depth=%d\n\n", numNodes, depth);
            int j;
            for (j = 0; j < 2; j++) {
                for (i = 0; i < numNodes; i++) {
                    memset(msg, 0, SERVER_BUFFER_SIZE);

                    int parent = (i-1)/2;
                    int left = (i*2)+1;
                    int right = (i*2)+2;

                    if (j == 0) {
                        if (i == 0) { //root
                            if (right < numNodes) //2 children
                                printf("%s, %d, %d %d\n", nodes[i], i, left, right);
                            else if (left < numNodes) //1 child
                                printf("%s, %d, %d\n", nodes[i], i, left);
                            else //no chlidren
                                printf("%s, %d, -\n", nodes[i], i);
                        }
                        else { //not root
                            if (right < numNodes) //2 children
                                printf("%s, %d, %d %d %d\n", nodes[i], i, parent, left, right);
                            else if (left < numNodes) //1 child
                                printf("%s, %d, %d %d\n", nodes[i], i, parent, left);
                            else //no children
                                printf("%s, %d, %d\n", nodes[i], i, parent);
                        }
                    }

                    strcpy(msg, "ips;");
                    
                    if (i > 0) { //has a parent neighbor
                        strcat(msg, nodes[parent]);
                        strcat(msg, ";");
                    }
                    if (left < numNodes) { //has a left neighbor
                        strcat(msg, nodes[left]);
                        strcat(msg, ";");
                    }
                    if (right < numNodes) { //has a right neighbor
                        strcat(msg, nodes[right]);
                        strcat(msg, ";");
                    }

                    if (j == 0 && DEBUG == 1) {
                        printf("UDP: Sending node %d's info: %s\n", i, msg);
                    }

                    memset(ipv6, 0, 30);
                    strcat(ipv6, ipv6_prefix);
                    strcat(ipv6, nodes[i]);

                    char *argsMsg[] = { "udp_send", ipv6, portBuf, msg, NULL };
                    udp_send(4, argsMsg);
                    xtimer_usleep(1000); // wait .001 seconds
                }
                xtimer_usleep(500000); // wait 0.5 seconds
            }

        } else if (strcmp(MY_TOPO,"gen") == 0) {
            printf("UDP: discovering general topology\n");

            /*
            for (i = 0; i < numNodes; i++) {
                memset(msg, 0, SERVER_BUFFER_SIZE);
                strcpy(msg, "ipsd;");

                memset(ipv6, 0, 30);
                strcat(ipv6, ipv6_prefix);
                strcat(ipv6, nodes[i]);
                        
                int j = 0;
                for (j = 0; j < numNodes; j++) {
                    if (i != j) {
                        strcat(msg, nodes[j]);
                        strcat(msg, ";");
                    }
                }
                char *argsMsg[] = { "udp_send", ipv6, portBuf, msg, NULL };
                udp_send(4, argsMsg);
                xtimer_usleep(10000); // wait .01 seconds
            }*/

            for (i = 0; i < numNodes; i++) {
                memset(msg, 0, SERVER_BUFFER_SIZE);
                strcpy(msg, "discover;");

                memset(ipv6, 0, 30);
                strcat(ipv6, ipv6_prefix);
                strcat(ipv6, nodes[i]);

                char *argsMsg[] = { "udp_send", ipv6, portBuf, msg, NULL };
                udp_send(4, argsMsg);
                xtimer_usleep(10000); // wait .01 seconds
            }

            xtimer_usleep(resetDiscoverLoops * wait); // wait resetDiscoverLoops * wait seconds
    
        } else if (strcmp(MY_TOPO,"grid") == 0) {
            printf("UDP: generating grid topology\n");
            return NULL;
        } else if (strcmp(MY_TOPO,"mesh") == 0) {
            if (DEBUG == 1) {
                printf("UDP: generating mesh topology\n");
            }
            printf("node, neighborID, neighbors\n");
            int width = round(sqrt(numNodes));
            int height = ceil(sqrt(numNodes));
            if (DEBUG == 1) {
                printf("UDP: numNodes=%d, width=%d, height=%d\n", numNodes, width, height);
            }
            int j;
            int neighborGroup[5] = {0};
            int groupCount = 0;
            int north = -1;
            int east = -1;
            int south = -1;
            int west = -1;
            for (j = 0; j < 1; j++) {
                for (i = 0; i < numNodes; i++) {
                    memset(msg, 0, SERVER_BUFFER_SIZE);

                    groupCount = 0;
                    if (i >= width) {
                        north = i - width;
                        neighborGroup[groupCount] = north;
                        groupCount += 1;
                    }
                    if (i % width != 0) {
                        west = i - 1;
                        neighborGroup[groupCount] = west;
                        groupCount += 1;
                    }
                    if (i % width != width - 1 && i + 1 < numNodes) {
                        east = i + 1;
                        neighborGroup[groupCount] = east;
                        groupCount += 1;
                    }
                    if (i + width < numNodes) {
                        south = i + width;
                        neighborGroup[groupCount] = south;
                        groupCount += 1;
                    }

                    if (j == 0) {
                        if (groupCount == 1) // 1 neighbor
                            printf("%s, %d, %d\n", nodes[i], i, neighborGroup[0]);
                        else if (groupCount == 2) // 2 neighbors
                            printf("%s, %d, %d %d\n", nodes[i], i, neighborGroup[0], neighborGroup[1]);
                        else if (groupCount == 3) // 3 neighbors
                            printf("%s, %d, %d %d %d\n", nodes[i], i, neighborGroup[0], neighborGroup[1], neighborGroup[2]);
                        else if (groupCount == 4) // 4 neighbors
                            printf("%s, %d, %d %d %d %d\n", nodes[i], i, neighborGroup[0], neighborGroup[1], neighborGroup[2], neighborGroup[3]);
                        else // no neighbors, shouldn't happen
                            printf("%s, %d,\n", nodes[i], i);
                    }

                    strcpy(msg, "ips;");

                    int g;
                    for (g = 0; g < groupCount; g++) {
                        strcat(msg, nodes[neighborGroup[g]]);
                        strcat(msg, ";");
                    }

                    if (j == 0 && DEBUG == 1) {
                        printf("UDP: Sending node %d's info: %s\n", i, msg);
                    }

                    memset(ipv6, 0, 30);
                    strcat(ipv6, ipv6_prefix);
                    strcat(ipv6, nodes[i]);

                    char *argsMsg[] = { "udp_send", ipv6, portBuf, msg, NULL };
                    udp_send(4, argsMsg);
                    xtimer_usleep(5000); // wait .005 seconds
                }
                xtimer_usleep(500000); // wait 0.5 seconds
            }
        }

        // synchronization? tell nodes to go?
        memset(msg, 0, SERVER_BUFFER_SIZE);
        xtimer_usleep(1000000); // wait 1 second
        startTime = xtimer_now_usec();

        int j;
        for (j = 0; j < 2; j++) {

            /*
            for (i = 0; i < numNodes; i++) {
                strcpy(msg, "start;");

                memset(ipv6, 0, 30);
                strcat(ipv6, ipv6_prefix);
                strcat(ipv6, nodes[i]);

                char *argsMsg[] = { "udp_send", ipv6, portBuf, msg, NULL };
                udp_send(4, argsMsg);
                xtimer_usleep(100); // wait .0001 seconds
            }
            */

            strcpy(msg, "start;");
            char *argsMsg[] = { "udp_send_multi", portBuf, msg, NULL };
            udp_send_multi(3, argsMsg);
            xtimer_usleep(100); // wait .0001 seconds
        }

        //if (DEBUG == 1) {
            //printf("UDP: start messages sent\n");
        //}

        // termination loop, waiting for info on protocol termination
        while (1) {
            // incoming UDP
            int res;
            memset(msg, 0, SERVER_BUFFER_SIZE);
            memset(msgP, 0, SERVER_BUFFER_SIZE);
            memset(server_buffer, 0, SERVER_BUFFER_SIZE);
            memset(ipv6, 0, 30);

            if ((res = sock_udp_recv(&sock, server_buffer,
                                     sizeof(server_buffer) - 1, 0.005 * US_PER_SEC, //SOCK_NO_TIMEOUT,
                                     &remote)) < 0) {
                if(res != 0 && res != -ETIMEDOUT && res != -EAGAIN && DEBUG == 1)  {
                    printf("UDP: Error - failed to receive UDP, %d\n", res);
                }
            }
            else if (res == 0) {
                if (DEBUG == 1)
                    (void) puts("UDP: no UDP data received");
            }
            else {
                server_buffer[res] = '\0';
                res = 1;
                ipv6_addr_to_str(ipv6, (ipv6_addr_t *)&remote.addr.ipv6, 47);

                int len = strlen(ipv6)-6;
                memset(ipv6_unique, 0, IPV6_ADDRESS_LEN);
                strncpy(ipv6_unique, ipv6+6, len);
                ipv6_unique[len] = '\0';

                if (DEBUG == 1) {
                    printf("UDP: recvd: %s from %s\n", server_buffer, ipv6);
                }
            }

            // handle UDP message
            if (res == 1) {
                if (strncmp(server_buffer,"failure;",8) == 0) {
                    // a node has failed, terminate algorithm
                    printf("ERROR: protocol failed by node %s\n", ipv6);
                    for (i = 0; i < numNodes; i++) {
                        strcpy(msg, "failure;");

                        memset(ipv6, 0, 30);
                        strcat(ipv6, ipv6_prefix);
                        strcat(ipv6, nodes[i]);

                        char *argsMsg[] = { "udp_send", nodes[i], portBuf, msg, NULL };
                        udp_send(4, argsMsg);
                        xtimer_usleep(1000); // wait .001 seconds
                    }
                    break;

                // Getting results from a node
                } else if (strncmp(server_buffer,"results;",8) == 0) {
                    // If we are already done don't save results anymore
                    if (!finished) {
                        int correct = -1;

                        if (numNodesFinished == 0) {
                            printf("node,m,elected,correct,startTime,runTime,messages\n");
                            resBegin = xtimer_now_usec();
                        }

                        int index = getNeighborIndex(nodes,ipv6_unique);
                        if (confirmed[index] == 1) {
                            if (DEBUG == 1)
                                printf("UDP: node %s was already confirmed\n", ipv6_unique);
                            continue;
                        }
                        confirmed[index] = 1; // results confirmed

                        // extract data
                        strcpy(msg, server_buffer);
                        char *mem = msg;
                        extractMsgSegment(&mem, codeBuf);  // chop off the results string
                        extractMsgSegment(&mem, tempipv6); // extract the elected IP
                        //printf("UDP: Node %s elected %s as leader\n",ipv6,tempipv6);

                        // determine election correctness
                        if (strcmp(tempipv6,nodes[minIndex]) == 0) {
                            correctNodes += 1;
                            correct = 1;
                        } else {
                            failedNodes += 1;
                            correct = 0;
                        }

                        memset(tempunixtime, 0, 15);
                        memset(tempunixsec, 0, 15);
                        memset(tempdegree, 0, 15);

                        extractMsgSegment(&mem, tempunixsec);
                        extractMsgSegment(&mem, tempmessagecount); // Extract the message count
                        extractMsgSegment(&mem, tempdegree); // degree
                        //printf("UDP: Node %s exchanged %s messages\n",ipv6,tempmessagecount);

                        float tempRun = atof(tempunixsec);
                        if (tempRun > maxRun) {
                            maxRun = tempRun;
                            memset(temprunsec, 0, 15);
                            strncpy(temprunsec, tempunixsec, 15);
                        }

                        int msgs = atoi(tempmessagecount);
                        if (minMsgs == 0 || msgs < minMsgs)
                            minMsgs = msgs;
                        if (maxMsgs == 0 || msgs > maxMsgs)
                            maxMsgs = msgs;
                        sumMsgs += msgs;

                        int degree = atoi(tempdegree);

                        // offset unix time
                        uint32_t offValue = startTime - syncTime;
                        int digits = 0;

                        memset(offset, 0, 15);
                        memset(seconds, 0, 15);
                        memset(decimal, 0, 15);

                        if (offValue >= 1000000) {
                            digits = 6;
                        } else if (offValue >= 100000) {
                            digits = 5;
                        } else if (offValue >= 10000) {
                            digits = 4;
                        } // smaller shouldn't happen ever

                        if (digits == 6) {
                            // last 6 is fractional seconds, before that is seconds
                            sprintf(offset, "%"PRIu32, offValue);
                            substr(offset, 0, strlen(offset)-6, seconds);
                            substr(offset, strlen(offset)-6, 6, decimal);

                            sprintf(tempunixtime, "%"PRIu32, unixTime + atoi(seconds));
                        } else  {
                            // value is fractional seconds, 0 whole seconds
                            sprintf(tempunixtime, "%"PRIu32, unixTime);
                        }

                        printf("%s,%d,%s,%s,%s,%s,%s,%d\n", ipv6_unique, m_values[index], tempipv6, correct ? "yes" : "no", tempunixtime, tempunixsec, tempmessagecount, degree);

                        numNodesFinished++;

                        char *argsMsg[] = { "udp_send", ipv6, portBuf, rconf, NULL };
                        udp_send(4, argsMsg);

                        //printf("UDP: %d nodes reported so far\n",numNodesFinished);
                        if (numNodesFinished >= numNodes) {
                            //printf("In finish block\n");
                            if (DEBUG == 1)
                                printf("Correct: %s\n", correctNodes == numNodesFinished ? "yes" : "no");
                            //printf("AvgTime: %d/%d sec\n", sumTime, numNodesFinished);
                            //printf("AvgMsgs: %d/%d msgs\n", sumMsgs, numNodesFinished);

                            printf("\nUDP: All nodes have reported!\n");
                            finished = 1;
                            //printf("Before finish block break statement\n");
                            break; // terminate
                        }
                    }
                }
            }
            
            uint32_t timeout = (uint32_t)((numNodes+1)/2);
            if (timeout < 20) timeout = 20;
            if (resBegin > 0 && xtimer_now_usec() - resBegin >= timeout * 1000000) {
                // 20 sec trying to get results...
                printf("ERROR: didn't get results from all nodes within %"PRIu32" seconds\n", timeout);
                finished = 1;
                break;
            }

            //xtimer_usleep(5000); // wait 0.005 seconds
        }
        //printf("After experiment loop\n");

        if (correctNodes == numNodesFinished) {
            //experiment was correct
            //printf("Recording runtimes\n");
            strncpy(expRuns[numCorrect], temprunsec, 15);
            //printf("Recording starts\n");
            strncpy(expStarts[numCorrect], tempunixtime, 15);
            //printf("Incrementing numCorrect\n");
            numCorrect += 1;
            printf("\n");
        } else {
            printf("********ABOVE EXPERIMENT FAILED********\n\n");
        }
        //printf("Resetting all vars\n");
        
        memset(tempunixtime, 0, 15);
        memset(temprunsec, 0, 15);

        // reset variables
        for(i = 0; i < MAX_NODES; i++) {
            memset(nodes[i], 0, IPV6_ADDRESS_LEN);
            m_values[i] = 0;
            confirmed[i] = 0;
        }

        numNodes = 0;
        numNodesFinished = 0;
        finished = 0;
        resBegin = 0;

        failedNodes = 0;
        correctNodes = 0;
        minMsgs = 0;
        maxMsgs = 0;
        sumMsgs = 0;
        min = 257;
        minIndex = -1;
        maxRun = 0.0;

        lastDiscover = 0;
        discoverLoops = resetDiscoverLoops;

        int z = 20;
        while (z > 0) {
            //printf("clearing queue");
            sock_udp_recv(&sock, server_buffer,
                 SERVER_BUFFER_SIZE - 1, 0, //SOCK_NO_TIMEOUT,
                 &remote);
            z -= 1;
        }

        if (DEBUG == 1)
            printf("Variables reset, starting next experiment in 5 seconds\n");
        
        xtimer_usleep(5000000); // wait 5 seconds
        expNum++;
    }

    // output run info for power processing script
    printf("\n%d/%d correct experiment results:\n", numCorrect, expNum);
    for(i = 0; i < numCorrect; i++) {
        if (i == numCorrect-1) {
            printf("%s\n", expStarts[i]);
        } else {
            printf("%s,", expStarts[i]);
        }
    }
    for(i = 0; i < numCorrect; i++) {
        if (i == numCorrect-1) {
            printf("%s\n", expRuns[i]);
        } else {
            printf("%s,", expRuns[i]);
        }
    }

    for(i = 0; i < MAX_NODES; i++) {
        free(nodes[i]);
    }
    free(nodes);
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
        if (DEBUG == 1)
            (void) puts("UDP: Usage - udp <ipv6-addr> <port> <payload>");
        return -1;
    }

    if (ipv6_addr_from_str((ipv6_addr_t *)&remote.addr, argv[1]) == NULL) {
        if (DEBUG == 1)
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
        if (DEBUG == 1)
            printf("UDP: Error - could not send message \"%s\" to %s\n", argv[3], argv[1]);
    }
    else {
        if (DEBUG == 1) 
            printf("UDP: Success - sent %u bytes to %s\n", (unsigned) res, argv[1]);
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
        if (DEBUG == 1)
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
        if (DEBUG == 1)
            printf("UDP: Error - could not send message \"%s\" to %s\n", argv[2], ipv6);
    }
    else {
        if (DEBUG == 1) 
            printf("UDP: Success - sent %u bytes to %s\n", (unsigned)res, ipv6);
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
