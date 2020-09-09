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

#define SERVER_MSG_QUEUE_SIZE   (32)
#define SERVER_BUFFER_SIZE      (128)
#define IPV6_ADDRESS_LEN        (40)

#define MAX_NODES               (50)

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

// Purpose: main code for the UDP server
void *_udp_server(void *args)
{
    (void)args;
    printf("UDP: Entered UDP server code\n");
    // socket server setup
    sock_udp_ep_t server = { .port = SERVER_PORT, .family = AF_INET6 };
    sock_udp_ep_t remote;
    msg_init_queue(server_msg_queue, SERVER_MSG_QUEUE_SIZE);
    char ipv6[IPV6_ADDRESS_LEN] = { 0 };
    char tempipv6[IPV6_ADDRESS_LEN] = { 0 };
    char tempunixtime[15] = { 0 };
    char tempunixsec[15] = { 0 };
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

    int failedNodes = 0;
    int correctNodes = 0;
    int minMsgs = 0;
    int maxMsgs = 0;
    int sumMsgs = 0;
    int min = 257;
    int minIndex = -1;

    char msg[SERVER_BUFFER_SIZE] = { 0 };
    char *msgP = (char*)calloc(SERVER_BUFFER_SIZE, sizeof(char));

    uint32_t lastDiscover = 0;
    uint32_t wait = 2*1000000;
    int resetDiscoverLoops = 30;
    int discoverLoops = 30; // 60sec of discovery

    // create the socket
    if(sock_udp_create(&sock, &server, NULL, 0) < 0) {
        return NULL;
    }

    server_running = true;
    printf("UDP: Success - started UDP server on port %u\n\n", server.port);
    printf("UDP: set to 30 two-second rounds for 60 seconds of node discovery\n");
    printf("UDP: you should have approx nodes/2 discovery rounds, update it with \"rounds <num>\"\n");
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
    while (expNum <= 10) {  // run the experiment 10 times
        if (DEBUG == 1)
            printf("Starting experiment %d...", expNum);
        // main server loop
        while (1) {
            memset(msg, 0, SERVER_BUFFER_SIZE);
            memset(msgP, 0, SERVER_BUFFER_SIZE);
            memset(server_buffer, 0, SERVER_BUFFER_SIZE);

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
                         sizeof(server_buffer) - 1, 0.05 * US_PER_SEC, //SOCK_NO_TIMEOUT,
                         &remote)) < 0) {
                if(res != 0 && res != -ETIMEDOUT && res != -EAGAIN && DEBUG == 1) 
                    printf("UDP: Error - failed to receive UDP, %d\n", res);
            }
            else if (res == 0) {
                if (DEBUG == 1)
                    (void) puts("UDP: no UDP data received");
            }
            else {
                server_buffer[res] = '\0';
                res = 1;
                ipv6_addr_to_str(ipv6, (ipv6_addr_t *)&remote.addr.ipv6, IPV6_ADDRESS_LEN);
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
                    int found = alreadyANeighbor(nodes, ipv6);
                    //printf("For IP=%s, found=%d\n", ipv6, found);
                    if (found == 0 && numNodes < MAX_NODES) {
                        strcpy(nodes[numNodes], ipv6);
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
                        
                        numNodes++;
                    
                        // send back discovery confirmation
                        strcpy(msg, "conf;");
                        char *argsMsg[] = { "udp_send", ipv6, portBuf, msg, NULL };
                        udp_send(4, argsMsg);
                    }
                }
            }

            xtimer_usleep(50000); // wait 0.05 seconds
        }

        if (DEBUG == 1 || expNum == 1)
            printf("Found %d nodes:\n\n",numNodes);

        for (i = 0; i < MAX_NODES; i++) {
            if (strcmp(nodes[i],"") == 0) 
                continue;
            if (DEBUG == 1)
                printf("%2d: %s, m=%d\n", i, nodes[i], m_values[i]);
        }
        if (DEBUG == 1)
            printf("\n");
    
        memset(msg, 0, SERVER_BUFFER_SIZE);
        memset(msgP, 0, SERVER_BUFFER_SIZE);
        memset(server_buffer, 0, SERVER_BUFFER_SIZE);

        for (i = 0; i < numNodes; i++) {
            strcpy(msg, "you;");
            memset(mStr, 0, 5);
            sprintf(mStr, "%d;", m_values[i]);
            strcat(msg, mStr);
            strcat(msg, nodes[i]);
            strcat(msg, ";");

            if (DEBUG == 1) {
                printf("UDP: sending m/identify info to %s\n", nodes[i]);
            }
            
            char *argsMsg[] = { "udp_send", nodes[i], portBuf, msg, NULL };
            udp_send(4, argsMsg);
            //xtimer_usleep(1000); // wait .001 seconds
        }

        memset(msg, 0, SERVER_BUFFER_SIZE);
        memset(msgP, 0, SERVER_BUFFER_SIZE);
        memset(server_buffer, 0, SERVER_BUFFER_SIZE);

        xtimer_usleep(500000); // wait 0.5 seconds

        // send out topology info to all discovered nodes
        if (strcmp(MY_TOPO,"ring") == 0) {
            // compose message, "ips:<m>;<yourIP>;<neighbor1>;<neighbor2>;
            if (DEBUG == 1)
                printf("UDP: generating ring topology\n");
            int j;
            for (j = 0; j < 2; j++) { // send topology info 2 time(s)
                for (i = 0; i < numNodes; i++) {
                    memset(msg, 0, SERVER_BUFFER_SIZE);

                    int pre = (i-1);
                    int post = (i+1);

                    if (pre == -1) { pre = numNodes-1; }
                    if (post == numNodes) { post = 0; }

                    if (j == 0 && DEBUG == 1) {
                        printf("UDP: node %d [%s]'s neighbors are %d and %d\n", i, nodes[i], pre, post);
                    }
                    strcpy(msg, "ips;");

                    strcat(msg, nodes[pre]);
                    strcat(msg, ";");
                    strcat(msg, nodes[post]);
                    strcat(msg, ";");

                    if (j == 0 && DEBUG == 1) {
                        printf("UDP: Sending node %d's info: %s\n", i, msg);
                    }

                    char *argsMsg[] = { "udp_send", nodes[i], portBuf, msg, NULL };
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
            int j;
            for (j = 0; j < 2; j++) { // send topology info 2 time(s)
                for (i = 0; i < numNodes; i++) {
                    memset(msg, 0, SERVER_BUFFER_SIZE);

                    int pre = (i-1);
                    int post = (i+1);

                    if (j == 0 && DEBUG == 1) {
                        if (pre == -1)
                            printf("UDP: node %d [%s]'s neighbor is %d\n", i, nodes[i], post);
                        else if (post == numNodes) 
                            printf("UDP: node %d [%s]'s neighbor is %d\n", i, nodes[i], pre);
                        else
                            printf("UDP: node %d [%s]'s neighbors are %d and %d\n", i, nodes[i], pre, post);
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

                    char *argsMsg[] = { "udp_send", nodes[i], portBuf, msg, NULL };
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

                    if (j == 0 && DEBUG == 1) {
                        if (i == 0) { //root
                            if (right < numNodes) //2 children
                                printf("UDP: node %d [%s] (root)'s neighbors are %d and %d\n", i, nodes[i], left, right);
                            else if (left < numNodes) //1 child
                                printf("UDP: node %d [%s] (root)'s neighbor is %d\n", i, nodes[i], left);
                            else //no chlidren
                                printf("UDP: node %d [%s] (root) has no neighbors\n", i, nodes[i]);
                        }
                        else { //not root
                            if (right < numNodes) //2 children
                                printf("UDP: node %d [%s]'s neighbors are %d, %d, and %d\n", i, nodes[i], parent, left, right);
                            else if (left < numNodes) //1 child
                                printf("UDP: node %d [%s]'s neighbors are %d and %d\n", i, nodes[i], parent, left);
                            else //no children
                                printf("UDP: node %d [%s]'s neighbor is %d\n", i, nodes[i], parent);
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

                    char *argsMsg[] = { "udp_send", nodes[i], portBuf, msg, NULL };
                    udp_send(4, argsMsg);
                    xtimer_usleep(1000); // wait .001 seconds
                }
                xtimer_usleep(500000); // wait 0.5 seconds
            }

        } else if (strcmp(MY_TOPO,"grid") == 0) {
            printf("UDP: generating grid topology\n");
            return NULL;
        } else if (strcmp(MY_TOPO,"mesh") == 0) {
            printf("UDP: generating mesh topology\n");
            return NULL;
        }

        // synchronization? tell nodes to go?
        memset(msg, 0, SERVER_BUFFER_SIZE);
        xtimer_usleep(1000000); // wait 1 second
        startTime = xtimer_now_usec();

        int j;
        for (j = 0; j < 2; j++) {
            for (i = 0; i < numNodes; i++) {
                strcpy(msg, "start;");
                char *argsMsg[] = { "udp_send", nodes[i], portBuf, msg, NULL };
                udp_send(4, argsMsg);
                xtimer_usleep(2000); // wait .002 seconds
            }
        }

        if (DEBUG == 1) {
            printf("UDP: start messages sent\n");
        }

        // termination loop, waiting for info on protocol termination
        while (1) {
            // incoming UDP
            int res;
            memset(msg, 0, SERVER_BUFFER_SIZE);
            memset(msgP, 0, SERVER_BUFFER_SIZE);
            memset(server_buffer, 0, SERVER_BUFFER_SIZE);

            if ((res = sock_udp_recv(&sock, server_buffer,
                                     sizeof(server_buffer) - 1, 0.03 * US_PER_SEC, //SOCK_NO_TIMEOUT,
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
                ipv6_addr_to_str(ipv6, (ipv6_addr_t *)&remote.addr.ipv6, IPV6_ADDRESS_LEN);
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

                        char *argsMsg[] = { "udp_send", ipv6, portBuf, rconf, NULL };
                        udp_send(4, argsMsg);

                        if (numNodesFinished == 0) {
                            printf("\nnode,m,elected,correct,startTime,runTime,messages\n");
                            resBegin = xtimer_now_usec();
                        }

                        int index = getNeighborIndex(nodes,ipv6);
                        if (confirmed[index] == 1 && DEBUG == 1) {
                            printf("UDP: node %s was already confirmed\n", ipv6);
                            continue;
                        }

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

                        extractMsgSegment(&mem, tempunixsec);
                        extractMsgSegment(&mem, tempmessagecount); // Extract the message count
                        //printf("UDP: Node %s exchanged %s messages\n",ipv6,tempmessagecount);

                        int msgs = atoi(tempmessagecount);
                        if (minMsgs == 0 || msgs < minMsgs)
                            minMsgs = msgs;
                        if (maxMsgs == 0 || msgs > maxMsgs)
                            maxMsgs = msgs;
                        sumMsgs += msgs;

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

                        printf("%s,%d,%s,%s,%s,%s,%s\n", ipv6, m_values[index], tempipv6, correct ? "yes" : "no", tempunixtime, tempunixsec, tempmessagecount);

                        confirmed[index] = 1; // results confirmed
                        numNodesFinished++;

                        //printf("UDP: %d nodes reported so far\n",numNodesFinished);
                        if (numNodesFinished >= numNodes) {
                            if (DEBUG == 1)
                                printf("Correct: %s\n", correctNodes == numNodesFinished ? "yes" : "no");
                            //printf("AvgTime: %d/%d sec\n", sumTime, numNodesFinished);
                            //printf("AvgMsgs: %d/%d msgs\n", sumMsgs, numNodesFinished);

                            if (DEBUG == 1)
                                printf("\nUDP: All nodes have reported!\n");
                            finished = 1;
                            break; // terminate
                        }
                    }
                }
            }

            if (resBegin > 0 && xtimer_now_usec() - resBegin >= 30000000) {
                // 20 sec trying to get results...
                printf("ERROR: didn't get results from all nodes within 30 seconds\n");
                finished = 1;
                break;
            }

            xtimer_usleep(30000); // wait 0.03 seconds
        }

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

        lastDiscover = 0;
        discoverLoops = resetDiscoverLoops;

        if (DEBUG == 1)
            printf("Variables reset, starting next experiment in 5 seconds\n");
        
        xtimer_usleep(5000000); // wait 5 seconds
        expNum++;
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
