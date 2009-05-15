# Default logging dir
LOGDIR="$HOME/paxos_logs"
BASEDIR="$HOME/libpaxos/tests"

function set_log_dir () {
	LOGDIR=$1
	rm -rf $LOGDIR	
	mkdir -p $LOGDIR
}

function set_basedir () {
	BASEDIR=$1
}

function init_check () {
	local DIR=`pwd`
	if [[ `basename $DIR` != "cluster" ]]; then
		echo "Please run from scripts/cluster/"
		exit;
	fi

	if [[ `hostname -s` != "node01" ]]; then
		echo "Please run from node01"
		exit;
	fi	

}

# function launch_detach_acceptor () {
#   local a_id=$1
#   local host=$2
#   local logfile="$LOGDIR/acceptor_$a_id"
# 
#   echo "Starting acceptor $a_id on host $host"
#   echo "(logs to: $logfile)"
#   ssh -t $host "$BASEDIR/acceptor_main -i $a_id &> $logfile &" &  
# }

function launch_background_acceptor () {
	local a_id=$1
	local host=$2
	local logfile="$LOGDIR/acceptor_$a_id"

	echo "Starting acceptor $a_id on host $host"
	echo "(logs to: $logfile)"
	ssh $host "$BASEDIR/example_acceptor $a_id &> $logfile" &	
}

function launch_background_oracle () {
	local host=$1
	local logfile="$LOGDIR/oracle.txt"

	echo "Starting oracle on host $host"
	echo "(logs to: $logfile)"
	ssh $host "$BASEDIR/example_oracle &> $logfile" &	
}

function launch_background_proposer () {
	local p_id=$1
	local host=$2
	local logfile="$LOGDIR/proposer_$p_id"

	echo "Starting proposer $p_id on host $host"
	echo "(logs to: $logfile)"
	ssh $host "$BASEDIR/example_proposer $p_id &> $logfile" &
}

CLIENT_COUNT=0;
function launch_background_client () {
	local bench_args=$1
    # local logfile=$2
	local host=$2
	let 'CLIENT_COUNT += 1'
	local logfile="$LOGDIR/client_$CLIENT_COUNT"

	echo "Starting client $CLIENT_COUNT on host $host"
	echo "(logs to: $logfile)"
	ssh $host "$BASEDIR/benchmark_client $bench_args &> $logfile" &
}

function launch_follow () {
	local cmd=$1
	local host=$2
	
	echo "Executing: \"$cmd\" on host $host"
	echo "from $BASEDIR"
	ssh -t $host "cd $BASEDIR && $cmd"
}

# function launch_background () {
#   local cmd=$1
#   local host=$2
#   
#   echo "Executing: \"$cmd\" on host $host"
#   echo "from $BASEDIR"
#   ssh $host "cd $BASEDIR && $cmd" &
# }

function remote_kill () {
	local prog=$1
	local host=$2
		
	echo "Killing $prog on host $host"
	ssh $host "killall -INT $prog"
}

function remote_kill_all () {
    local procnames="$1"
    local first=2;
    local last=16;

    for (( i = $first; i <= $last; i++ )); do
        nodenum="$i"
        if [[ $i -lt 10 ]]; then
            nodenum="0$i"
        fi
        echo "Killing on marco@node$nodenum"
        ssh "marco@node$nodenum" "killall -INT $procnames"
    done
}