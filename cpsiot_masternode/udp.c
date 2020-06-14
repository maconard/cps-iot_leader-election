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

// Standard RIOT includes
#include "thread.h"
#include "xtimer.h"

// Networking includes
#include "net/sock/udp.h"
#include "net/ipv6/addr.h"

#define CHANNEL                 (11)

#define SERVER_MSG_QUEUE_SIZE   (64)
#define SERVER_BUFFER_SIZE      (256)
#define IPV6_ADDRESS_LEN        (46)
#define MAX_IPC_MESSAGE_SIZE    (256)

#define MAX_NODES               (8)

// 1=ring, 2=line, grid, mesh
#define MY_TOPO                 (1)

#define DEBUG                   (1)

// Forward declarations
void *_udp_server(void *args);
int udp_send(int argc, char **argv);
int udp_send_multi(int argc, char **argv);
int udp_server(int argc, char **argv);
int alreadyANeighbor(char **neighbors, char *ipv6);
int getNeighborIndex(char **neighbors, char *ipv6);

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

// Purpose: main code for the UDP server
void *_udp_server(void *args)
{
    (void)args;
    printf("UDP: Entered UDP server code\n");
    // socket server setup
    sock_udp_ep_t server = { .port = SERVER_PORT, .family = AF_INET6 };
    sock_udp_ep_t remote;
    msg_init_queue(server_msg_queue, SERVER_MSG_QUEUE_SIZE);
    char msg_content[MAX_IPC_MESSAGE_SIZE];
    char ipv6[IPV6_ADDRESS_LEN] = { 0 };
    char portBuf[6];
    sprintf(portBuf,"%d",SERVER_PORT);

    int numNodes = 0;
    int i;
    char **nodes = (char**)calloc(MAX_NODES, sizeof(char*));
    for(i = 0; i < MAX_NODES; i++) {
        nodes[i] = (char*)calloc(IPV6_ADDRESS_LEN, sizeof(char));
    }

    uint64_t lastDiscover = 0;
    uint64_t wait = 5*1000000; // 5 seconds
    int discoverLoops = 3; // 15 seconds of discovery

    // create the socket
    if(sock_udp_create(&sock, &server, NULL, 0) < 0) {
        return NULL;
    }

    server_running = true;
    printf("UDP: Success - started UDP server on port %u\n", server.port);

    // main server loop
    while (1) {
        // discover nodes
        if (lastDiscover + wait < xtimer_now_usec64()) {
            // multicast to find nodes
            if (discoverLoops == 0) break;

            char msg[5] = "ping";
            char *argsMsg[] = { "udp_send_multi", portBuf, msg, NULL };
            udp_send_multi(3, argsMsg);
            discoverLoops--;
            lastDiscover = xtimer_now_usec64();
        }
    
        // incoming UDP
        int res;
        memset(msg_content, 0, MAX_IPC_MESSAGE_SIZE);
        memset(server_buffer, 0, SERVER_BUFFER_SIZE);

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
            if (DEBUG == 1) 
                printf("UDP: recvd: %s from %s\n", server_buffer, ipv6);
        }

        // react to UDP message
        if (res == 1) {
            // a node has responded to our discovery request
            if (strncmp(server_buffer,"pong",7) == 0) {
                // if node with this ipv6 is already found, ignore
                // otherwise record them
                int found = alreadyANeighbor(nodes, ipv6);
                printf("For IP=%s, found=%d\n", ipv6, found);
                if (found == 0) {
                    strcpy(nodes[numNodes], ipv6);
                    printf("UDP: recorded new node, %s\n", nodes[numNodes]);
                    numNodes++;
                
                    // send back discovery confirmation
                    char msg[5] = "conf";
                    char *argsMsg[] = { "udp_send", ipv6, portBuf, msg, NULL };
                    udp_send(4, argsMsg);
                }
            }
        }

        xtimer_usleep(50000); // wait 0.05 seconds
    }

    printf("Node discovery complete, %d nodes:\n",numNodes);
    int c = 1;
    for (i = 0; i < MAX_NODES; i++) {
        if (strcmp(nodes[i],"") == 0) 
            continue;
        printf("%2d: %s\n", c, nodes[i]);
        c += 1;
    }

    // send out topology info to all discovered nodes
    if (MY_TOPO == 1) {
        // compose message, "ips:<yourIP>;<neighbor1>;<neighbor2>;
        printf("UDP: generating ring topology\n");
        int j;
        for(j = 0; j < 3; j++) { // send topology info 3 times
            for(i = 0; i < numNodes; i++) {
                int pre = (i-1);
                int post = (i+1);

                if(pre == -1) pre = numNodes-1;
                if(post == numNodes) post = 0;

                printf("UDP: in for loop %d, pre/post: %d/%d\n", i, pre, post);
                char msg[SERVER_BUFFER_SIZE] = "ips:";
                
                strcat(msg, nodes[i]);
                strcat(msg, ";");
                strcat(msg, nodes[pre]);
                strcat(msg, ";");
                strcat(msg, nodes[post]);
                strcat(msg, ";");

                printf("UDP: Sending topology to %s, %s\n", nodes[i], msg);

                char *argsMsg[] = { "udp_send", nodes[i], portBuf, msg, NULL };
                udp_send(4, argsMsg);
            }
            xtimer_usleep(1000000); // wait 1 seconds
        }
    } else if (MY_TOPO == 2) {
        // compose message, "ips:<yourIP>;<neighbor1>;<neighbor2>;
        printf("UDP: generating line topology\n");
        for(i = 0; i < numNodes; i++) {
            printf("***%d: in for loop\n", i);
            char msg[SERVER_BUFFER_SIZE] = "ips:";
            printf("***%d: after buff\n", i);
            
            strcat(msg, nodes[i]);
            strcat(msg, ";");
            if(i > 0) {
                strcat(msg, nodes[(i-1)%numNodes]);
                strcat(msg, ";");
            }
            if(i < numNodes-1) {
                strcat(msg, nodes[(i+1)%numNodes]);
                strcat(msg, ";");
            }

            printf("UDP: Sending topology to %s, %s\n", nodes[i], msg);

            char *argsMsg[] = { "udp_send", nodes[i], portBuf, msg, NULL };
            udp_send(4, argsMsg);
        }  
    }

    // synchronization? tell nodes to go?
    xtimer_usleep(5000000); // wait 5 seconds
    for(i = 0; i < numNodes; i++) {
        char msg[7] = "start:";
        char *argsMsg[] = { "udp_send", nodes[i], portBuf, msg, NULL };
        udp_send(4, argsMsg);
    }

    printf("UDP: start messages sent\n");

    // termination loop, waiting for info on protocol termination
    while (1) {
        // incoming UDP
        int res;
        memset(msg_content, 0, MAX_IPC_MESSAGE_SIZE);
        memset(server_buffer, 0, SERVER_BUFFER_SIZE);

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
            if (DEBUG == 1) 
                printf("UDP: recvd: %s from %s\n", server_buffer, ipv6);
        }

        // handle UDP message
        if (res == 1) {
            // David? TODO
            // react to final messages about election results, convergence, etc
            if (strncmp(server_buffer,"something",7) == 0) {
                
            } else if (strncmp(server_buffer,"something",7) == 0) {
                
            } else if (strncmp(server_buffer,"something",7) == 0) {
                
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
