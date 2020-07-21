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

#define CHANNEL                 11

#define SERVER_MSG_QUEUE_SIZE   (16)
#define SERVER_BUFFER_SIZE      (512)
#define IPV6_ADDRESS_LEN        (46)

#define MAX_NODES               (10)

#define QUOTE(name) #name
#define STR(macro) QUOTE(macro)

// 1=ring, 2=line, 3=tree, 4=grid, 5=mesh
#ifndef LE_TOPO
    #define LE_TOPO             ring
#endif

#define MY_TOPO                 STR(LE_TOPO)

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
    char tempstarttime[10] = { 0 };
    char tempruntime[10] = { 0 };
    char tempmessagecount[5] = { 0 };
    int m_values[MAX_NODES] = { 0 };
    int confirmed[MAX_NODES] = { 0 };
    char mStr[5] = { 0 };
    char portBuf[6];
    char codeBuf[10];
    char rconf[6] = "rconf;";
    sprintf(portBuf,"%d",SERVER_PORT);

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
    int minTime = 0;
    int maxTime = 0;
    int sumTime = 0;
    int minMsgs = 0;
    int maxMsgs = 0;
    int sumMsgs = 0;
    int min = 257;
    int minIndex = -1;

    char msg[SERVER_BUFFER_SIZE] = { 0 };
    char *msgP = (char*)calloc(SERVER_BUFFER_SIZE, sizeof(char));

    uint32_t lastDiscover = 0;
    uint32_t wait = 2.5*1000000; // 2.5 seconds
    int discoverLoops = 4; // 10 seconds of discovery

    // create the socket
    if(sock_udp_create(&sock, &server, NULL, 0) < 0) {
        return NULL;
    }

    server_running = true;
    printf("UDP: Success - started UDP server on port %u\n", server.port);

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
            if(res != 0 && res != -ETIMEDOUT && res != -EAGAIN) 
                printf("UDP: Error - failed to receive UDP, %d\n", res);
        }
        else if (res == 0) {
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
                if (found == 0) {
                    strcpy(nodes[numNodes], ipv6);
                    printf("UDP: recorded new node, %s\n", nodes[numNodes]);
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

    printf("\nNode discovery complete, found %d nodes:\n",numNodes);
    for (i = 0; i < MAX_NODES; i++) {
        if (strcmp(nodes[i],"") == 0) 
            continue;
        printf("%2d: %s, m=%d\n", i, nodes[i], m_values[i]);
    }
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
        xtimer_usleep(1000); // wait .001 seconds
    }

    memset(msg, 0, SERVER_BUFFER_SIZE);
    memset(msgP, 0, SERVER_BUFFER_SIZE);
    memset(server_buffer, 0, SERVER_BUFFER_SIZE);

    xtimer_usleep(500000); // wait 0.5 seconds

    // send out topology info to all discovered nodes
    if (strcmp(MY_TOPO,"ring") == 0) {
        // compose message, "ips:<m>;<yourIP>;<neighbor1>;<neighbor2>;
        printf("UDP: generating ring topology\n");
        int j;
        for (j = 0; j < 2; j++) { // send topology info 2 time(s)
            for (i = 0; i < numNodes; i++) {
                memset(msg, 0, SERVER_BUFFER_SIZE);

                int pre = (i-1);
                int post = (i+1);

                if (pre == -1) { pre = numNodes-1; }
                if (post == numNodes) { post = 0; }

                if (j == 0) {
                    printf("UDP: node %d's neighbors are %d and %d\n", i, pre, post);
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
        printf("UDP:");
        for (i = 0; i < numNodes; i++) {
            if (i < numNodes-1)
                printf(" %d <->", i);
            else
                printf(" %d <-> 0\n", i);
        }

    } else if (strcmp(MY_TOPO,"line") == 0) {
        printf("UDP: generating line topology\n");
        int j;
        for (j = 0; j < 2; j++) { // send topology info 2 time(s)
            for (i = 0; i < numNodes; i++) {
                memset(msg, 0, SERVER_BUFFER_SIZE);

                int pre = (i-1);
                int post = (i+1);

                if (j == 0) {
                    if (pre == -1)
                        printf("UDP: node %d's neighbor is %d\n", i, post);
                    else if (post == numNodes) 
                        printf("UDP: node %d's neighbor is %d\n", i, pre);
                    else
                        printf("UDP: node %d's neighbors are %d and %d\n", i, pre, post);
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
        printf("UDP:");
        for (i = 0; i < numNodes; i++) {
            if (i < numNodes-1)
                printf(" %d <->", i);
            else
                printf(" %d\n", i);
        }
    } else if (strcmp(MY_TOPO,"tree") == 0) {
        printf("UDP: generating tree topology\n");
        int depth = (int)logk(numNodes,2);
        printf("UDP: numNodes=%d, depth=%d\n", numNodes, depth);
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
                            printf("UDP: node %d (root)'s neighbors are %d and %d\n", i, left, right);
                        else if (left < numNodes) //1 child
                            printf("UDP: node %d (root)'s neighbor is %d\n", i, left);
                        else //no chlidren
                            printf("UDP: node %d (root) has no neighbors\n", i);
                    }
                    else { //not root
                        if (right < numNodes) //2 children
                            printf("UDP: node %d's neighbors are %d, %d, and %d\n", i, parent, left, right);
                        else if (left < numNodes) //1 child
                            printf("UDP: node %d's neighbors are %d and %d\n", i, parent, left);
                        else //no children
                            printf("UDP: node %d's neighbor is %d\n", i, parent);
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

    int j;
    for (j = 0; j < 2; j++) {
        for (i = 0; i < numNodes; i++) {
            strcpy(msg, "start;");
            char *argsMsg[] = { "udp_send", nodes[i], portBuf, msg, NULL };
            udp_send(4, argsMsg);
            xtimer_usleep(2000); // wait .002 seconds
        }
    }

    printf("UDP: start messages sent\n");

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
            if(res != 0 && res != -ETIMEDOUT && res != -EAGAIN)  {
                printf("UDP: Error - failed to receive UDP, %d\n", res);
            }
        }
        else if (res == 0) {
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
                        printf("\nnode,elected,correct,startTime,runTime,messages\n");
                    }

                    int index = getNeighborIndex(nodes,ipv6);
                    if (confirmed[index] == 1) {
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

                    extractMsgSegment(&mem, tempstarttime); // Extract the start time
                    //printf("UDP: Node %s started at %s microseconds\n",ipv6,tempstarttime);

                    extractMsgSegment(&mem, tempruntime); // Extract the run time
                    //printf("UDP: Node %s finished in %s microseconds\n",ipv6,tempruntime);

                    int time = atoi(tempruntime);
                    if (minTime == 0 || time < minTime)
                        minTime = time;
                    if (maxTime == 0 || time > maxTime)
                        maxTime = time;
                    sumTime += time;

                    extractMsgSegment(&mem, tempmessagecount); // Extract the message count
                    //printf("UDP: Node %s exchanged %s messages\n",ipv6,tempmessagecount);

                    int msgs = atoi(tempmessagecount);
                    if (minMsgs == 0 || msgs < minMsgs)
                        minMsgs = time;
                    if (maxMsgs == 0 || msgs > maxMsgs)
                        maxMsgs = time;
                    sumMsgs += msgs;

                    printf("%s,%s,%s,%s,%s,%s\n", ipv6, tempipv6, correct ? "yes" : "no", tempstarttime, tempruntime, tempmessagecount);

                    confirmed[index] = 1; // results confirmed
                    numNodesFinished++;

                    //printf("UDP: %d nodes reported so far\n",numNodesFinished);
                    if (numNodesFinished >= numNodes) {
                        printf("Correct: %s\n", correctNodes == numNodesFinished ? "yes" : "no");
                        printf("AvgTime: %d/%d sec\n", sumTime, numNodesFinished);
                        printf("AvgMsgs: %d/%d msgs\n", sumMsgs, numNodesFinished);

                        printf("\nUDP: All nodes have reported!\n");
                        finished = 1;
                        break; // terminate
                    }
                }
            }
        }

        xtimer_usleep(20000); // wait 0.02 seconds
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
        if (DEBUG == 1) 
            printf("UDP: Success - sent %u bytes to %s\n", (unsigned)res, ipv6);
    }
    return 0;
}

// Purpose: creates the UDP server thread
//
// argc int, number of arguments (should be 2)
// argv char**, list of arguments ("udps", <thread-pid>)
int udp_server(int argc, char **argv)
{
    if (argc != 1) {
        puts("MAIN: Usage - udps");
        return -1;
    }

    if ((server_running == false) &&
        thread_create(server_stack, sizeof(server_stack), THREAD_PRIORITY_MAIN - 1,
                      THREAD_CREATE_STACKTEST, _udp_server, argv[0], "UDP_Server_Thread")
        <= KERNEL_PID_UNDEF) {
        printf("MAIN: Error - failed to start UDP server thread\n");
        return -1;
    }

    return 0;
}
