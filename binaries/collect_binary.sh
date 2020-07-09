#!/bin/bash

# Build, name, and sort RIOT binaries
# helps speed up the recompiling process

# Note: T1 and T2 should be entered in seconds

# usage: ./collect_binary <board> <master|worker> <topo|K> <T1> <T2>

BOARD="$1"
TYPE="$2"
PARAM="$3"
T1="$4"
T2="$5"

if [[ "$BOARD" != "iotlab-m3" && "$BOARD" != "native" ]]; then
    echo "Please select either iotlab-m3 or native."
    echo "Usage: ./collect_binary <board> <master|worker> <topo|K> <T1> <T2>"
    exit
fi

if [[ "$TYPE" != "master" && "$TYPE" != "worker" ]]; then
    echo "Please select either master or worker."
    echo "Usage: ./collect_binary <board> <master|worker> <topo|K> <T1> <T2>"
    exit
fi

if [[ "$PARAM" == "" ]]; then
    echo "Please enter either a master topology or worker K"
    echo "Usage: ./collect_binary <board> <master|worker> <topo|K> <T1> <T2>"
    exit
fi

if [[ "$TYPE" == "worker" && "$T1" == "" ]]; then
    echo "Please enter a T1 value in seconds."
    echo "Usage: ./collect_binary <board> <master|worker> <topo|K> <T1> <T2>"
    exit
fi

if [[ "$TYPE" == "worker" && "$T2" == "" ]]; then
    echo "Please enter a T1 value in milliseconds."
    echo "Usage: ./collect_binary <board> <master|worker> <topo|K> <T1> <T2>"
    exit
fi

if [[ "$TYPE" == "master" ]]; then
    FILE="master_${BOARD}_${PARAM}.elf"
    TARGET="./${BOARD}/${TYPE}/$FILE"

    echo "Compiling $FILE..."
    pushd ../cpsiot_masternode > /dev/null
        make BOARD="$BOARD" TOPO="$PARAM" > /dev/null
    popd  > /dev/null
    cp "../cpsiot_masternode/bin/${BOARD}/master_node.elf" "$TARGET"
    echo "Saved the master binary $FILE."
else
    T1us=`echo $T1 \* 1000000.0 | bc -l`
    T2us=`echo $T2 \* 1000000.0 | bc -l`
    T1="${T1//./_}"
    T2="${T2//./_}"
    FILE="worker_${BOARD}_${PARAM}-${T1}-${T2}.elf"
    TARGET="./${BOARD}/${TYPE}/$FILE"

    echo "Compiling $FILE..."
    pushd ../cpsiot_workernode > /dev/null
        make BOARD="$BOARD" K="$PARAM" T1="$T1us" T2="$T2us" > /dev/null
    popd  > /dev/null
    cp "../cpsiot_workernode/bin/${BOARD}/worker_node.elf" "$TARGET"
    echo "Saved the worker binary $FILE."
fi

