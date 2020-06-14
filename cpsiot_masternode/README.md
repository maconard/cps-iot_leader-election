Master Node - Leader Election Project
================
This application is a part of running leader election on RIOT OS nodes.

Currently the master node only supports ring topology overlays. The other topologies will be added later on.

Topology Meanings
==========
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
