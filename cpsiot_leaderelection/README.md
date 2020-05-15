Michael's Leader Election Project
================
This application run's leader election on RIOT OS nodes.

To do this, the application uses the `shell` and `shell_commands` modules and all the driver modules each board supports.

`shell` is a very simple interactive command interpreter that can be used to call functions.  Many of RIOT's modules define some generic shell commands. These are included via the `shell_commands` module.

Usage
=====

Build, flash and start the application:
```
make all term PORT=your_port
```

The `term` make target starts a terminal emulator for your node. It connects to a default port so you can interact with the shell.

For hardware, probably: `PORT=/dev/ttyUSB0`.  
For virtual native linux nodes, create taps/tuns and set the `PORT=yourTap` variable in the make statement.


RIOT Shell
==============

The shell commands come with online help. Call `help` to see which commands exist and what they do.

RIOT Specific
=============

The `ps` command is used to see and analyze all the threads' states and memory statuses.

Networking
==========

The `ifconfig` command will help you to configure all available network interfaces. Particularly, note the HWaddr and IPv6 addresses in the output.

Type `ifconfig help` to get an online help for all available options (e.g. setting the radio channel via `ifconfig 4 set chan 12`).

The `txtsnd` command allows you to send a simple string directly over the link layer using unicast or multicast. The application will also automatically print information about any received packet over the serial. This will look like.

My Protocols
==========

Neighbor Discovery will run automatically as soon as the protocols thread has established communication with the UDP thread. Leader Election will initiate after some fixed delay and at least two neighbors have been discovered.

My Scripts
==========
## `mac_topology_gen.py`
My python script that can generate line, ring, tree, and grid topologies while specifying size, broadcast packet loss, incoming packet loss, and RIOT executable files

### Command Line Arguments
--r, default=2, type=int, choices=range(1,27), Number of rows in the grid/mesh from 1 to 26  
--c, default=2, type=int, Number of cols in the grid/mesh  
--s, default=4, type=int, Number of nodes in the network (not used with --t grid/mesh)  
--t, default="ring", type=str, choices=['ring', 'line', 'binary-tree', 'grid', 'mesh', 'star', 'complete'], The topology to create for this network  
--d, default="bi", type=str, choices=['uni','bi'], Uni or bidirectional links (not used with --t grid/mesh/star/complete)  
--b, default="0.0", type=str, Percentage of broadcast loss given as a string (default "0.0")  
--l, default="0.0", type=str, Percentage of packet loss given as a string (default "0.0")  
--e, default="", type=str, Address of a compiled RIOT project .elf file to run on all the nodes  

### Topology Meanings
ring - All nodes are each connected to two other nodes in a circle. The links look like `0 -> 1 -> 2 -> ... -> N -> 0`, where unidirectional ring nodes receive from `(i-1)%N` and send to `(i+1)%N`, while bidirectional ring nodes can send and receive with both neighbors.  

line - A straight line of nodes, like a ring without the `N -> 0` link. A unidirectional line is not meaningful, as one end can only ever send and the other can only ever recieve, while a bidirectional line can communicate both directions.  

binary-tree - A tree of arity=2, filled in top to bottom left to right. As an example, the command `python mac_topology_gen.py --t binary-tree --s 6` would generate the following topology:
```
      root
     /    \
    a0     a1
   /  \    /
  b0  b1  b2
```
where all the links a bidirectional. A unidirectional tree (`--d uni`) is possible but not very meaningfull, presenting a model where the root can send orders down the tree but can never recieve any information back up the tree (may work for a scenario where orders to do things need to be given and the leaf nodes do not collect any data or contribute to computation).  

grid - An `r x c` matrix of nodes where every node can bidirectionally communicate with the `(r-1,c)`, `(r,c-1)`, `(r+1,c)`, `(r,c+1)` neighbor nodes (if they exist -- it doesn't wrap around the edge of the grid). `--d uni` is not supported as there's no effective way to automate that; you would have to manually define for every grid node where the unidirectional links are.  

mesh - A grid (above) with 45 degree neighbors instead of 90 degree neighbors  

complete - All `N` nodes are neighboring every other node -- everyone can talk to anyone (i.e. a node's local neighborhood is the whole network).  

star - One central hub and the remaining `N-1` nodes are each connected to only it. With this topology if an outer node dies it doesn't affect any routes through the network, but the central hub node has a lot of traffic.

### Output
- A topology.xml file intended to be consumed by RIOT's desvirt/vnet tools.  
- A cleanup.sh script that will delete all the taps/tuns that desvirt/vnet will create for the network. Takes your project name as the one command line argument, in order to cleanup .elf files.

## `install_topology` 
A simple script that moves the output file from the topology generator to the desvirt working directory. Depends on the RIOTBASE environment variable.

### Command Line Arguments

$1: the topology.xml file to install for desvirt

## `riot` 
A simple script that makes it easier to use desvirt. Requires that desvirt is already built.

### Command Line Arguments

$1: the desvirt command to run, from: `list`, `define`, `undefine`, `start`, `stop`  
$2: the topology file to launch (not used with `list`)

## `setup`
A simple script that tears down your old RIOT instance and fully launches a new one, which is useful for compiling and launching changes made to your project. You need to edit the file to set the `PROJ` variable to your project name and the `TOPO` variable to whatever topology you want to tear down and re-launch. If you're trying to change topologies, you should run `{TOPO}_cleanup.sh {PROJ}` separately before your next run of `setup` with the new topology.

Sample Use
==========

```
> cd /path/to/RIOT/examples/my_project

> make
...output from make
Created /path/to/my/project/binary.elf

> python mac_topology_gen.py --s 5 --t ring --d uni --l 20.0 --e /path/to/my/project/binary.elf
Created uni-ring5.xml
Created uni-ring5_cleanup.sh

> ./install_topology uni-ring5.xml
uni-ring5.xml has been installed for desvirt to use

> ./riot define uni-ring5
cd /home/michael/Documents/Software/RIOT/dist/tools/desvirt/desvirt && ./vnet -d /home/michael/Documents/Software/RIOT/dist/tools/desvirt/desvirt/.desvirt/ -n uni-ring5
vnet           : Loaded statefile .desvirt/lib/uni-ring5.macs.
vnet           : Network Name: uni-ring5
vnet           : Setting up virtual topology uni-ring5...

> ./riot start uni-ring5
cd /path/to/RIOT/dist/tools/desvirt/desvirt && ./vnet -s -n uni-ring5
...output from creating node taps
...output from defining node links/neighbors
...output from initiating RIOT processes

> ./riot list
cd /path/to/RIOT/dist/tools/desvirt/desvirt && ./vnet -l
Network Name         State
----------------------------
uni-ring5            running

> ./riot stop uni-ring5
cd /path/to/RIOT/dist/tools/desvirt/desvirt && ./vnet -q -n uni-ring5
vnet           : Loaded statefile .desvirt/lib/uni-ring5.macs.
vnet           : Loaded statefile .desvirt/lib/uni-ring5.taps.
vnet           : Loaded statefile .desvirt/lib/uni-ring5.pids.
vnet           : Network Name: uni-ring5
vnet           : Shutting down bridge and links...
vnet           : Shutting down nodes...
riotnative     : Kill the RIOT: /path/to/my/project/binary.elf (5996)

> .'riot undefine uni-ring5
cd /path/to/RIOT/dist/tools/desvirt/desvirt && ./vnet -u /path/to/RIOT/dist/tools/desvirt/desvirt/.desvirt/ -n uni-ring5
vnet           : Loaded statefile .desvirt/lib/uni-ring5.macs.
vnet           : Loaded statefile .desvirt/lib/uni-ring5.taps.
vnet           : Loaded statefile .desvirt/lib/uni-ring5.pids.
vnet           : Network Name: uni-ring5
vnet           : Undefining network...
vnet           : Done.

> ./uni-ring5_cleanup.sh
```

