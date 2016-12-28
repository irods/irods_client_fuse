DIRECTORY
iRODS/clients/fuse	- iRODS/fuse implementation

DESCRIPTION
	This directory, and its subdirectories, contain modules for
	building the irodsFs binary which can be used to mount an
	iRODS collection to a local directory. Then the files and
        sub-collections in this mounted iRODS collection can be
	accessed using regular UNIX commands through this local directory.

FUSE (Filesystem in Userspace) is a free Unix kernel module that allows 
non-privileged users to create their own file systems without editing 
the kernel code. This is achieved by running the file system code in 
user space, while the FUSE module only provides a "bridge" to the actual 
kernel interfaces. 

The iRODS FUSE implementation allows normal users to access data stored 
in iRODS using standard UNIX commands (ls, cp, etc) and system calls 
(open, read, write, etc).

The user's iRODS passwd or credential will be used for authentication at 
the time of the mount. e.g., doing a iinit before the mount and he/she will 
be able to access all files/collections he/she is allowed to access under 
iRODS. The user will need to set the appropriate UNIX permission (mode) of 
the UNIX mount directory to control access to the mounted data.


Building irods FUSE:
--------------------

Type in:

cd clients/fuse
gmake

Running irods Fuse:
-------------------

1) cd clients/fuse/bin

2) make a local directory for mounting. e.g.,

    mkdir /usr/tmp/fmount

3) Setup the iRODS client env (~/irods/irods_environment.json) so that iCommands
will work. Type in:
    iinit

and do the normal login.

4) Mount the home collection to the local directory by typing in:
./irodsFs /usr/tmp/fmount

The user's home collection is now mounted. The iRODS files and sub-collections 
in the user's home collection should be accessible with normal UNIX commands 
through the /usr/tmp/fmount directory. 

Run irodsFs in debug mode
-------------------------

To run irodsFs in debug mode:

1) set the env variable irodsLogLevel to 4, e.g. in csh:

setenv irodsLogLevel 4

2) run irodsFs in debug mode, e.g.

irodsFs -d yourMountPoint

It should print out a lot of debugging info.


Use irodsFsCtl for control
--------------------------

1) Clear all metadata cache:
```
irodsFsCtl.py yourMountPoint reset_cache
```


Helpful options
---------------

1) Enabling/disabling features
- `-onocache`: Disable all caching features (Buffered IO, Preload, Metadata 
   Cache)
- `-onopreload`: Disable Preload feature that pre-fetches file blocks in 
   advance. By default, the preload is turned on.
- `-onocachemetadata`: Disable metadata caching feature. By default, the 
   metadata cacheing is turned on.
- `-oconnreuse`: Set to reuse network connections for performance. This may 
   provide inconsistent metadata with mysql-backed iCAT. By default, connections
   are not reused.

2) Other configurations
- `-omaxconn`: Set max number of network connection to be established at the 
   same time. By default, this is set to 10.
- `-oblocksize`: Set block size at data transfer. All transfer is made in a 
   block-level for performance. By default, this is set to 1048576(1MB).
- `-oconntimeout`: Set timeout of a network connection. After the timeout, idle 
   connections will be automatically closed. By default, this is set to 300
   (5 minutes).
- `-oconnkeepalive`: Set intervals of keepalive requests. For every keepalive 
   intervals, keepalive message is sent to iCAT to keep network connections 
   live. By default, this is set to 180(3 minutes).
- `-oconncheckinterval`: Set intervals of connection timeout check. For every 
   check intervals, all connections established are checked to figure out if 
   they are timed-out. By default, this is set to 10(10 seconds).
- `-oapitimeout`: Set timeout of iRODS client API calls. If an API call does not
   respond before the timeout, the API call and the network connection 
   associated with are killed. By default, this is set to 90(90 seconds).
- `-opreloadblocks`: Set the number of blocks pre-fetched. By default, this is 
   set to 3 (next 3 blocks in advance).
- `-ometadatacachetimeout`: Set timeout of a metadata cache. Metadata caches are
   invalidated after the timeout. By default, this is set to 180(3 minutes).


For example, following command will 1) reuse connections, 2) prefetch next 
5 blocks and 3) set timeout of metadata cache to 1 hour.
```
irodsFs -oconnreuse -opreloadblocks 5 -ometadatacachetimeout 3600 yourMountPoint
```


WARNING
-------

1) When a collection is mounted using irodsFs, uses of iCommands
such as iput, irm, icp, etc that change the content of the collection should be 
avoided because the FUSE implementation caches the attributes of the contents of
the collection.
