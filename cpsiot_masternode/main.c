/*
 * @author  Michael Conard <maconard@mtu.edu>
 *
 * Purpose: Launch the master node for an iot-lab experiment.
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

#define CHANNEL 11

#define MAIN_QUEUE_SIZE         (64)
#define MAX_IPC_MESSAGE_SIZE    (128)
#define IPV6_ADDRESS_LEN        (46)
#define MAX_NODES               (10)

#define    DEBUG     (0)

// External functions defs
extern int udp_send(int argc, char **argv);
extern int udp_server(int argc, char **argv);

// Forward declarations
static int hello_world(int argc, char **argv);
static int run(int argc, char **argv);

// Data structures (i.e. stacks, queues, message structs, etc)
static msg_t _main_msg_queue[MAIN_QUEUE_SIZE];

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

// initiates main program
static int run(int argc, char **argv) {
    (void)argc;
    (void)argv;

    // start ipv6 support thread
    (void) puts("MAIN: Trying to start IPv6 thread");
    int ipv6_thread = gnrc_ipv6_init();
    if(ipv6_thread == EOVERFLOW || ipv6_thread == EEXIST) {
        (void) puts("MAIN: Error - failed to start ipv6 thread");
        return -1;
    }
    (void) puts("MAIN: Launched IPv6 thread");

    // start internal UDP server
    (void) puts("MAIN: Trying to launch UDP server thread");
    char *argsUDP[] = { "udp_server", NULL };
    
    if (udp_server(2, argsUDP) == -1) {
        (void) puts("MAIN: Error - failed to start UDP server thread");
    }
    (void) puts("MAIN: Launched UDP server thread");

    return 0;
}

// main method
int main(void)
{
    // initialize networking and packet tools
    gnrc_netreg_entry_t dump = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, gnrc_pktdump_pid);
    gnrc_netreg_register(GNRC_NETTYPE_UNDEF, &dump);
    msg_init_queue(_main_msg_queue, MAIN_QUEUE_SIZE);

    run(0, NULL);

    (void) puts("MAIN: Welcome to RIOT!");

    // start the RIOT shell for this node
    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE);

    return 0;
}
