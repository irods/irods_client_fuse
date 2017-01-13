#! /usr/bin/env python

# This code is written by Illyoung Choi (iychoi@email.arizona.edu)
# funded by iPlantCollaborative (www.iplantcollaborative.org).

import os
import os.path
import sys
import fcntl
import array

IOCTL_APP_NUMBER = 0xEE
IFUSEIOC_RESET_METADATA_CACHE = 0
IFUSEIOC_SHOW_CONNECTIONS = 1


_IOC_NRBITS = 8
_IOC_TYPEBITS = 8
_IOC_SIZEBITS = 14
_IOC_DIRBITS = 2

_IOC_NRMASK = (1 << _IOC_NRBITS) - 1
_IOC_TYPEMASK = (1 << _IOC_TYPEBITS) - 1
_IOC_SIZEMASK = (1 << _IOC_SIZEBITS) - 1
_IOC_DIRMASK = (1 << _IOC_DIRBITS) - 1

_IOC_NRSHIFT = 0
_IOC_TYPESHIFT = _IOC_NRSHIFT + _IOC_NRBITS
_IOC_SIZESHIFT = _IOC_TYPESHIFT + _IOC_TYPEBITS
_IOC_DIRSHIFT = _IOC_SIZESHIFT + _IOC_SIZEBITS

_IOC_NONE = 0
_IOC_WRITE = 1
_IOC_READ = 2

def _IOC(dir, type, nr, size):
    return (dir  << _IOC_DIRSHIFT) | (type << _IOC_TYPESHIFT) | (nr << _IOC_NRSHIFT) | (size << _IOC_SIZESHIFT)

def _IO(type, nr):
    return _IOC(_IOC_NONE, type, nr, 0)
    
def _IOR(type, nr, size):
    return _IOC(_IOC_READ, type, nr, size)

def _IOW(type, nr, size):
    return _IOC(_IOC_WRITE, type, nr, size)
    
def _IOWR(type, nr, size):
    return _IOC(_IOC_READ | _IOC_WRITE, type, nr, size)



def reset_cache(mount_path):
    print "reset cache: %s" % (mount_path)
    
    fd = os.open(mount_path, os.O_DIRECTORY)
    status = fcntl.ioctl(fd, _IO(IOCTL_APP_NUMBER, IFUSEIOC_RESET_METADATA_CACHE))
    if status != 0:
        print >> sys.stderr, "failed to reset cache"
    else:
        print "Done!"
    os.close(fd)
    
def show_connections(mount_path):
    print "show connections: %s" % (mount_path)
    
    fd = os.open(mount_path, os.O_DIRECTORY)
    buf = array.array('i', [0,0,0,0,0])
    status = fcntl.ioctl(fd, _IOR(IOCTL_APP_NUMBER, IFUSEIOC_SHOW_CONNECTIONS, 20), buf, 1)
    if status != 0:
        print >> sys.stderr, "failed to show connections"
    else:
        inuseShortOpConn = buf[0]
        inuseConn = buf[1]
        inuseOnetimeuseConn = buf[2]
        freeShortopConn = buf[3]
        freeConn = buf[4]
        
        print "In-Use ShortOp Conn: %d" % inuseShortOpConn
        print "In-Use Conn: %d" % inuseConn
        print "In-Use OneTimeUse Conn: %d" % inuseOnetimeuseConn
        print "Free ShortOp Conn: %d" % freeShortopConn
        print "Free Conn: %d" % freeConn
        print "Done!"
    os.close(fd)

COMMANDS = {
    "reset_cache": reset_cache,
    "show_connections": show_connections,
}

COMMANDS_DESCS = {
    "reset_cache": "invalidate all caches",
    "show_connections": "show all established connections"
}

def ioctl(command, mount_path, oargs):
    if command is None:
        print >> sys.stderr, "No command is given"
    
    command = command.lower()
    mount_path = os.path.abspath(mount_path)
    
    if command in COMMANDS:
        COMMANDS[command](mount_path)
    else:
        print >> sys.stderr, "Unrecognized command: %s" % (command)

def main(argv):
    if len(argv) < 2:
        print "command : ./irodsFsCtl.py <COMMAND> <IRODSFS_MOUNT_PATH> <Optional_Args...>"
        print ""
        print "Available Commands"
        for k in COMMANDS.keys():
            print "%s: %s" % (k, COMMANDS_DESCS[k])
    else:
        command = argv[0]
        mount_path = argv[1]
        oargs = argv[2:]
        
        ioctl(command, mount_path, oargs)

if __name__ == "__main__":
    main(sys.argv[1:])
