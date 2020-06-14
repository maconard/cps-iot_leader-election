# Author: Michael Conard

# name of your application
APPLICATION = worker_node

# If no BOARD is found in the environment, use this default:
#BOARD ?= native # native for testing
BOARD ?= iotlab-m3 # m3 nodes on iotlab

# This has to be the absolute path to the RIOT base directory:
RIOTBASE ?= $(CURDIR)/../..

# Set the SSID and password of your WiFi network here
#WIFI_SSID ?= "Your_WiFi_name"
#WIFI_PASS ?= "Your_secure_password"

# Modules to include:

# gnrc is a meta module including all required, basic gnrc networking modules
USEMODULE += gnrc
USEMODULE += gnrc_netdev_default
USEMODULE += auto_init_gnrc_netif
# shell command to send L2 packets with a simple string
USEMODULE += gnrc_txtsnd
# Activate ICMPv6 error messages
USEMODULE += gnrc_icmpv6_error
# Specify the mandatory networking modules for IPv6 and UDP
USEMODULE += gnrc_ipv6_default
USEMODULE += gnrc_ipv6_router_default
USEMODULE += gnrc_udp
USEMODULE += gnrc_sock_udp
# Add a routing protocol
USEMODULE += gnrc_rpl
USEMODULE += auto_init_gnrc_rpl
# This application dumps received packets to STDIO using the pktdump module
USEMODULE += gnrc_pktdump
# Additional networking modules that can be dropped if not needed
USEMODULE += gnrc_icmpv6_echo

USEMODULE += shell
USEMODULE += shell_commands
USEMODULE += ps

# Uncomment this to enable scheduler statistics for ps:
USEMODULE += schedstatistics

USEMODULE += netstats_l2
USEMODULE += netstats_ipv6
USEMODULE += netstats_rpl

USEMODULE += xtimer
USEMODULE += random

# Comment this out to disable code in RIOT that does safety checking
# which is not needed in a production environment but helps in the
# development process:
DEVELHELP ?= 1

# Change this to 0 show compiler invocation lines by default:
QUIET ?= 1

#STDIO_DEV = UART_DEV\(1\)
#CFLAGS += -DSTDIO_UART_DEV=$(STDIO_DEV)

CFLAGS += -DGNRC_PKTBUF_SIZE=512

FEATURES_OPTIONAL += periph_rtc

include $(RIOTBASE)/Makefile.include

# Set a custom channel if needed
include $(RIOTMAKE)/default-radio-settings.inc.mk

