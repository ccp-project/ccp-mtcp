#! /usr/bin/env python

# Test network throughput.
#
# Usage:
# 1) on host_A: throughput -s [port]                    # start a server
# 2) on host_B: throughput -c  count host_A [port]      # start a client
#
# The server will service multiple clients until it is killed.

import sys, time
from socket import *

BUFSIZE = 8192

REPORT_INTERVAL = 1.0 # seconds

def bulk_recv(conn, host, remoteport):
    bytes_rcvd = 0
    bytes_interval = 0
    start = time.time()
    last_report = time.time()
    while 1:
        data = conn.recv(BUFSIZE)
        n = len(data)
        bytes_rcvd += n
        bytes_interval += n
        
        now = time.time()
        elapsed = now - last_report
        if elapsed > REPORT_INTERVAL:
            thru_interval = ((bytes_interval / elapsed) * 8) / 1000000
            print "\r{:06.3f} Mbps".format(thru_interval)
            last_report = now
            bytes_interval = 0

        #print bytes_rcvd, data[-1] == 0x96 if len(data) > 0 else ""
        if (not data) or (n <= 0) or (data[-1] == '\x96'):
            break
        del data
    print "Received END from client"
    conn.send('OK\n')
    conn.close()
    end = time.time()
    thru = ((bytes_rcvd / (end-start)) * 8) / 1000000
    print "Done with {}:{}, bytes_rcvd: {}, time: {}, thru: {}Mbps".format(host,remoteport,bytes_rcvd,(end-start),thru)

def server(mode):
    ip = None
    port = None

    if mode == "wait":
        port = eval(sys.argv[2])
        s = socket(AF_INET, SOCK_STREAM)
        s.bind(('10.1.1.6', port))
        s.listen(1)
        print 'Server ready...'
        while 1:
            conn, (host, remoteport) = s.accept()
            print 'Connected!'
            bulk_recv(conn, host, remoteport)
    elif mode == "send":
        ip = sys.argv[2]
        port = eval(sys.argv[3])
        s = socket(AF_INET, SOCK_STREAM)
        s.connect((ip, port))
        print 'Connected'
        bulk_recv(s, ip, port)
        


def usage():
    sys.stdout = sys.stderr
    print "usage: python recv.py wait [port]"
    print "usage: python recv.py send [ip] [port]"
    sys.exit(2)

def main():
    if len(sys.argv) < 3:
        usage()
    else:
        mode = sys.argv[1]
        if mode == "wait":
            if len(sys.argv) != 3:
                usage()
        elif mode == "send":
            if len(sys.argv) != 4:
                usage()
        else:
            print "Unknown mode", mode
        server(mode)

main()
