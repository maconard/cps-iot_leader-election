#!/bin/bash

# Build, name, and sort RIOT binaries
# Generates binaries for a range of parameters

# Note: T should be entered in seconds
# Note: will default to ranges K=(2,10,1) and T=(0.3,2.0,0.1)

# usage: ./generate_binaries <board> <min_K> <max_K> <step_K> <min_T> <max_T> <step_T>

BOARD="$1"
MIN_K="$2"
MAX_K="$3"
STEP_K="$4"
MIN_T="$5"
MAX_T="$6"
STEP_T="$7"

if [[ "$BOARD" != "iotlab-m3" && "$BOARD" != "native" ]]; then
    echo "Please select either iotlab-m3 or native."
    echo "Usage: ./generate_binaries <board> <min_K> <max_K> <step_K> <min_T> <max_T> <step_T>"
    exit
fi

if [[ "$MIN_K" == "" ]]; then
    echo "No constraints entered, defaulting to ranges K=(2,10,1) and T=(0.3,2.0,0.1)"
    MIN_K=2
    MAX_K=10
    STEP_K=1
    MIN_T=0.3
    MAX_T=2.0
    STEP_T=0.1
elif [[ "$MAX_K" == "" ]]; then
    echo "Please enter no K/T constraints or all of them."
    echo "Usage: ./generate workers <board> <min_K> <max_K> <step_K> <min_T> <max_T> <step_T>"
    exit
elif [[ "$STEP_K" == "" ]]; then
    echo "Please enter no K/T constraints or all of them."
    echo "Usage: ./generate workers <board> <min_K> <max_K> <step_K> <min_T> <max_T> <step_T>"
    exit
elif [[ "$MIN_T" == "" ]]; then
    echo "Please enter no K/T constraints or all of them."
    echo "Usage: ./generate workers <board> <min_K> <max_K> <step_K> <min_T> <max_T> <step_T>"
    exit
elif [[ "$MAX_T" == "" ]]; then
    echo "Please enter no K/T constraints or all of them."
    echo "Usage: ./generate workers <board> <min_K> <max_K> <step_K> <min_T> <max_T> <step_T>"
    exit
elif [[ "$STEP_T" == "" ]]; then
    echo "Please enter no K/T constraints or all of them."
    echo "Usage: ./generate workers <board> <min_K> <max_K> <step_K> <min_T> <max_T> <step_T>"
    exit
fi

# Build master binaries
echo ""
echo "Beginning master binary generation for line, ring, and tree..."
echo ""
./collect_binary.sh $BOARD master line
./collect_binary.sh $BOARD master ring
./collect_binary.sh $BOARD master tree
echo ""
echo "Master node generation complete."
echo ""

# Build worker binaries
echo "Beginning worker binary generation for ranges K=($MIN_K,$MAX_K,$STEP_K) and T=($MIN_T,$MAX_T,$STEP_T)..."
echo ""
for k in `seq $MIN_K $STEP_K $MAX_K`
do
    for t in `seq $MIN_T $STEP_T $MAX_T`
    do
        ./collect_binary.sh $BOARD worker $k $t
    done
done
echo ""
echo "Worker node generation complete."
echo ""
