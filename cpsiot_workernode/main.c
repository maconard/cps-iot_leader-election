/*
 * @author  Michael Conard <maconard@mtu.edu>
 *
 * Purpose: Apply a leader election protocol to a network on RIOT.
 */

// Standard C includes
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <msg.h>

// Standard RIOT includes
#include "thread.h"
#include "shell.h"
#include "shell_commands.h"
#include "xtimer.h"

// Networking includes
#include "net/gnrc/pktdump.h"
#include "net/gnrc.h"
#include "net/gnrc/ipv6.h"
#include "net/gnrc/ndp.h"
#include "net/gnrc/pkt.h"

#define CHANNEL                 11
#define MAIN_QUEUE_SIZE         (32)
#define MAX_IPC_MESSAGE_SIZE    (128)
#define SERVER_BUFFER_SIZE      (128)
#define IPV6_ADDRESS_LEN        (22)

#define DEBUG                   (1)

// External functions defs
extern int udp_send(int argc, char **argv);
extern int udp_server(int argc, char **argv);
extern kernel_pid_t leader_election(int argc, char **argv);

// Forward declarations
static int hello_world(int argc, char **argv);
static int run(int argc, char **argv);
int ipc_msg_send_receive(char *message, kernel_pid_t destinationPID, msg_t *response, uint16_t type);
int ipc_msg_send(char *message, kernel_pid_t destinationPID, bool blocking);
int ipc_msg_reply(char *message, msg_t incoming);
void substr(char *s, int a, int b, char *t);
void extractMsgSegment(char **s, char *t);
int indexOfSemi(char *ipv6);

// Data structures (i.e. stacks, queues, message structs, etc)
static msg_t _main_msg_queue[MAIN_QUEUE_SIZE];
//static msg_t msg_in;

// ************************************
// START MY CUSTOM RIOT SHELL COMMANDS

// hello world shell command, prints hello
static int hello_world(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("hello world!\n");

    return 0;
}

// END MY CUSTOM RIOT SHELL COMMANDS
// ************************************

// shell command structure
const shell_command_t shell_commands[] = {
    {"hello", "prints hello world", hello_world},
    { NULL, NULL, NULL }
};


// Purpose: find the index of a semicolon in a string for data packing
//
// ipv6 char*, string to check for the semicolon in
int indexOfSemi(char *ipv6) {
    for (uint32_t i = 0; i < strlen(ipv6); i++) {
        if (ipv6[i]  == ';') {
            return i+1; // start of second id
        }
    }
    return -1;
}

// Purpose: write into t from s by extracting the next IP from the list
//
// s char*, source string
// t char*, destination string
void extractMsgSegment(char **s, char *t) 
{
    int in;
    
    in = indexOfSemi(*s);   
    memset(t, 0, in); 
    substr(*s, 0, in-1, t);
    *s += in;
}

// Purpose: write into t from s starting at index a for length b
//
// s char*, source string
// t char*, destination string
// a int, starting index in s
// b int, length to copy from s following index a
void substr(char *s, int a, int b, char *t) 
{
    memset(t, 0, b);
    strncpy(t, s+a, b);
}

// IPC HELPER FUNCTIONS

// Purpose: send message to destinationPID, blocking or not
//
// message char*, the message to send out
// destinationPID kernel_pid_t, the destination thread ID
// blocking bool, whether or not to block for message to be received
int ipc_msg_send(char *message, kernel_pid_t destinationPID, bool blocking) {
    msg_t msg_out;
    msg_out.content.ptr = message;
    msg_out.type = (uint16_t)strlen(message)+1;
    //blocking = true;

    if (DEBUG == 1) 
        printf("DEBUG: send %s to %" PRIkernel_pid ", type=%d\n", (char*)msg_out.content.ptr, destinationPID, msg_out.type);
    
    int res;
    if (blocking) {
        res = msg_send(&msg_out, destinationPID);
    } else {
        res = msg_try_send(&msg_out, destinationPID);
    }
    
    return res;
}

// Purpose: respond to an incoming message
//
// message char*, the message to reply with
// incoming msg_t, the incoming message to reply to
int ipc_msg_reply(char *message, msg_t incoming) {
    msg_t msg_out;
    msg_out.content.ptr = message;
    msg_out.type = (uint16_t)strlen(message);

    if (DEBUG == 1) 
        printf("DEBUG: reply %s\n", (char*)msg_out.content.ptr);
  
    int res = msg_reply(&incoming, &msg_out);

    return res;
}

// initiates main program
static int run(int argc, char **argv) {
    (void)argc;
    (void)argv;

    // start leader election UDP server
    (void) puts("MAIN: Trying to launch UDP server thread");
    char *argsUDP[] = { "udp_server", NULL };
    
    if (udp_server(1, argsUDP) == -1) {
        (void) puts("MAIN: Error - failed to start UDP server thread");
    }
    (void) puts("MAIN: Launched UDP server thread");

    return 0;
}

// main method
int main(void)
{
    (void) puts("MAIN: Welcome to RIOT!");

    msg_init_queue(_main_msg_queue, MAIN_QUEUE_SIZE);
    
    run(0, NULL);

    // start the RIOT shell for this node
    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE);

    return 0;
}
