#!/bin/bash

DIR=`pwd`
if [[ `basename $DIR` != "cluster" ]]; then
    echo "Please run from scripts/cluster/"
    exit;
fi
source ../common/auto_launch.sh;

init_check


set_log_dir "$HOME/paxos_logs"
set_basedir "$HOME/libpaxos2/tests"

launch_background_acceptor 0 node03
launch_background_acceptor 1 node04
launch_background_acceptor 2 node05

# launch_background_oracle node02

sleep 4

launch_background_proposer 0 node06

sleep 2

CLIENT_CMD="./benchmark_client -m 30 -M 300 -t 10 -d 30 -c 100 -p 10"
launch_follow "$CLIENT_CMD" node02



# remote_kill "example_oracle" node02
remote_kill "benchmark_client" node02
remote_kill "example_proposer" node06
remote_kill "example_acceptor" node03
remote_kill "example_acceptor" node04
remote_kill "example_acceptor" node05
