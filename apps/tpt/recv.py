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
    s.bind(('', port))
    s.listen(1)
    print 'Server ready...'
    while 1:
        conn, (host, remoteport) = s.accept()
        while 1:
            data = conn.recv(BUFSIZE)
            if not data or repr(data[-1]) != '0x90':
                break
            del data
        conn.send('OK\n')
        conn.close()
        print 'Done with', host, 'port', remoteport

def usage():
    sys.stdout = sys.stderr
    print "usage: python recv.py [port]"
    sys.exit(2)

def main():
    if len(sys.argv) < 2:
        usage()
    server(sys.argv[1])

main()
