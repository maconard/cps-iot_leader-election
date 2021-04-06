# cps-iot_leader-election

The `iotlab-m3` executables are for executing on IoT-Lab m3 nodes while the `native` executables are for testing using Linux virtual nodes.

This project uses one master node to discover all the worker nodes. The master node generates an overlay topology for the worker nodes and sends them that information. Finally, the master node will signal to all the worker nodes to begin running their protocol. 

To test on N nodes, you should create an experiment with N+1 nodes. Flash the master firmware to one node and the worker firmware to the other N nodes.

Note that this version of the code uses the Master Node concept only as a means of experimentation and data collection; the algorithm can be deployed without a master node where any node can initiate an election instead of just the master.

# Usage

You can compile binaries in mass using the `binaries/generate_binaries.sh` script. It will produce a master binary for every topology as well as the requested worker binaries. 
It is used as follows: `Usage: ./generate_binaries <board> <min_K> <max_K> <step_K> <min_T> <max_T> <step_T>`

Once deployed on iot-lab, open the terminal for one worker node and the master node. Run `rounds <num>` on the master node to set the number of two-second node discovery rounds to perform each experiment. The default is set to 30 to be safe, but you only need approximately numNodes/2. Then run `sync <unix-time>` to synchronize the clock to unix time and begin the experiments. It will run 10 experiments back to back; the master node will output spreadsheet ready results while you can watch the sample worker node for experiment progress.

When running the `sync` command it is helpful to have a unix clock up and type out a unix time a few seconds in advance, to run it right on time. Make sure you copy the master node results before your iot-lab experiment timer ends, because the terminals will close and all output will be lost.

# Monitoring Data

Assuming you ran the worker nodes with an energy monitoring profile, that information can be found on the iot-lab servers. Here is an example showing how to retrieve all experiment data from the Lille site for user `conard`:

First login to the server via ssh:

`ssh conard@lille.iot-lab.info`

Archive all the experiment data to make it easy to transfer over the network:

`zip -r myResultsFile.zip .iot-lab/`

Return to your local shell to retrieve the archive file:

`scp conard@lille.iot-lab.info:/senslab/users/conard/myResultsFile.zip myResultsFile.zip`

And now all of the monitoring data is on your local machine. You can find the energy data under `.iot-lab/<exp-num>/consumption/...`.


