#!/bin/bash

# Build, name, and sort RIOT binaries
# helps speed up the recompiling process

# Note: T1 and T2 should be entered in seconds

# usage: ./collect_binary <board> <master|worker> <topo|K> <T>

BOARD="$1"
TYPE="$2"
PARAM="$3"
T="$4"

if [[ "$BOARD" != "iotlab-m3" && "$BOARD" != "native" ]]; then
    echo "Please select either iotlab-m3 or native."
    echo "Usage: ./collect_binary <board> <master|worker> <topo|K> <T>"
    exit
fi

if [[ "$TYPE" != "master" && "$TYPE" != "worker" ]]; then
    echo "Please select either master or worker."
    echo "Usage: ./collect_binary <board> <master|worker> <topo|K> <T>"
    exit
fi

if [[ "$PARAM" == "" ]]; then
    echo "Please enter either a master topology or worker K"
    echo "Usage: ./collect_binary <board> <master|worker> <topo|K> <T>"
    exit
fi

if [[ "$TYPE" == "worker" && "$T" == "" ]]; then
    echo "Please enter a T value in seconds."
    echo "Usage: ./collect_binary <board> <master|worker> <topo|K> <T>"
    exit
fi

if [[ "$TYPE" == "master" ]]; then
    FILE="master_${BOARD}_${PARAM}.elf"
    TARGET="./${BOARD}/${TYPE}/$FILE"

    echo "Compiling $FILE..."
    pushd ../cpsiot_masternode > /dev/null
        make BOARD="$BOARD" LE_TOPO="$PARAM" > /dev/null
    popd  > /dev/null
    cp "../cpsiot_masternode/bin/${BOARD}/master_node.elf" "$TARGET"
    echo "Saved the master binary $FILE."
else
    Tus=`echo $T \* 1000000.0 | bc -l`
    T="${T//./_}"
    FILE="worker_${BOARD}_${PARAM}-${T}.elf"
    TARGET="./${BOARD}/${TYPE}/$FILE"

    echo "Compiling $FILE..."
    pushd ../cpsiot_workernode > /dev/null
        make BOARD="$BOARD" LE_K="$PARAM" LE_T="$Tus" > /dev/null
    popd  > /dev/null
    cp "../cpsiot_workernode/bin/${BOARD}/worker_node.elf" "$TARGET"
    echo "Saved the worker binary $FILE."
fi

