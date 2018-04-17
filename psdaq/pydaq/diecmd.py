#!/usr/bin/env python
"""
CM die cmd
"""
import time
import zmq
from CMMsg import CMMsg

def main():

    # Prepare our context and DEALER socket
    ctx = zmq.Context()
    cmd = ctx.socket(zmq.DEALER)
    cmd.linger = 0
    cmd.RCVTIMEO = 5000 # in milliseconds
    cmd.connect("tcp://%s:5556" % CMMsg.host())

    # Initiate client exit
    cmd.send(CMMsg.STARTDIE)
    while True:
        try:
            cmmsg = CMMsg.recv(cmd)
        except Exception as ex:
            print(ex)
            return

        if cmmsg.key == CMMsg.DIESTARTED:
            print ("I: Received DIESTARTED")
            break          # Done
        else:
            print ("W: Received key \"%s\"" % cmmsg.key)
            continue

    print ("Done")

if __name__ == '__main__':
    main()
