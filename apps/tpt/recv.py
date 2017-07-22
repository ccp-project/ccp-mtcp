#! /usr/bin/env python

# Test network throughput.
#
# Usage:
# 1) on host_A: throughput -s [port]                    # start a server
# 2) on host_B: throughput -c  count host_A [port]      # start a client
#
# The server will service multiple clients until it is killed.
#
# The client performs one transfer of count*BUFSIZE bytes and
# measures the time it takes (roundtrip!).


import sys, time
from socket import *

MY_PORT = 50000 + 42

BUFSIZE = 8192

def server(port):
    if len(sys.argv) > 1:
        port = eval(port)
    else:
        usage()
    s = socket(AF_INET, SOCK_STREAM)
    s.bind(('10.1.1.6', port))
    s.listen(1)
    print 'Server ready...'
    while 1:
        conn, (host, remoteport) = s.accept()
        recvd = 0
        start = time.time()
        while 1:
            data = conn.recv(BUFSIZE)
            recvd += len(data)
            #print recvd, data[-1] == 0x96 if len(data) > 0 else ""
            if not data or repr(data[-1]) == '\x96':
                break
            del data
        print "DONE"
        conn.send('OK\n')
        conn.close()
        end = time.time()
        thru = ((recvd / (end-start)) * 8) / 1000000
        print "Done with {}:{}, recvd: {}, time: {}, thru: {}Mbps".format(host,remoteport,recvd,(end-start),thru)

def usage():
    sys.stdout = sys.stderr
    print "usage: python recv.py [port]"
    sys.exit(2)

def main():
    if len(sys.argv) < 2:
        usage()
    server(sys.argv[1])

main()
