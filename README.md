# cps-iot_leader-election

Group project continued from the CPS-IoT course at Michigan Tech, Spring 2020.

The `-debug` elf files are the same code with more debugging print statements activated. The `m3` executables are for executing on IoT-Lab m3 nodes while the `native` executables are for testing using Linux virtual nodes.

This project uses one master node to discover all the worker nodes. The master node generates an overlay topology for the worker nodes and sends them that information. Finally, the master node will signal to all the worker nodes to begin running their protocol. 

To test on N nodes, you should create an experiment with N+1 nodes. Flash the master firmware to one node and the worker firmware to the other N nodes.
