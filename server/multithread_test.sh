#
# Simple sandbox test script to test/debug the multi-threaded aesdsocket implementation.
#

target=localhost
port=9000

string1="string 1"
string2="string 2"
string3="string 3"
process_send_count=3

function test_socket_thread1 {
    local c
	for (( c=1; c<=${process_send_count}; c++ ))
	do
		echo "Sending string ${string1} from process 1: instance ${c}"
		echo ${string1} | nc ${target} ${port} -w 1 > /dev/null
        #sleep 1
	done
	echo "Process 1 complete"
}

function test_socket_thread2 {
    local c
	for (( c=1; c<=${process_send_count}; c++ ))
	do
		echo "Sending string ${string2} from process 2: instance ${c}"
		echo ${string2} | nc ${target} ${port} -w 1 > /dev/null
        #sleep 1
	done
	echo "Process 2 complete"
}

function test_socket_thread3 {
    local c
	for (( c=1; c<=${process_send_count}; c++ ))
	do
		echo "Sending string ${string3} from process 3: instance ${c}"
		echo ${string3} | nc ${target} ${port} -w 1 > /dev/null
        #sleep 1
	done
	echo "Process 3 complete"
}

test_socket_thread1&
test_socket_thread2&
test_socket_thread3&

sleep 1
echo "done"
exit 0