#! /usr/bin/env python

import os
import os.path
import sys
import fcntl

def reset_cache(mount_path):
    fd = os.open(mount_path, os.O_DIRECTORY)
    status = fcntl.ioctl(fd, 0xEE00)
    if status != 0:
        print >> sys.stderr, "failed to reset cache"
    os.close(fd)

def ioctl(mount_path, command, oargs):
    if command == None:
        print >> sys.stderr, "No command is given"
        
    if command.lower() in ["reset_cache"]:
        reset_cache(mount_path)
    else:
        print >> sys.stderr, "Unrecognized command: %s" % (command)

def main(argv):
    if len(argv) < 2:
        print "command : ./irodsFsCtl.py <MOUNT_PATH> <COMMAND> <Optional_Args...>"
    else:
        mount_path = argv[0]
        command = argv[1]
        oargs = argv[2:]
        
        ioctl(mount_path, command, oargs)

if __name__ == "__main__":
    main(sys.argv[1:])
