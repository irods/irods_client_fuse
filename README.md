iRODS FUSE client
=================

Description
-----------

This repository builds the irodsFs binary which can be used to
mount an iRODS collection to a local directory. Then the files and
sub-collections in the mounted iRODS collection can be accessed
using regular UNIX commands through the local directory.

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


Prerequisites for building iRODS FUSE:
--------------------------------------

You will need to install a few packages from https://packages.irods.org:

```
sudo apt-get install irods-dev
sudo apt-get install irods-runtime
sudo apt-get install irods-externals-clang3.8-0
sudo apt-get install irods-externals-cppzmq4.1-0
sudo apt-get install irods-externals-cmake3.5.2-0
```

This will install the necessary buildchain into `/opt/irods-externals`.

You will also need a couple additional packages:

```
sudo apt-get install pkg-config
sudo apt-get install libfuse-dev
```

Make sure that the iRODS CMake is in your PATH:

```
export PATH=/opt/irods-externals/cmake3.5.2-0/bin:$PATH
```

Building iRODS FUSE:
--------------------

If `/usr/lib/x86_64-linux-gnu/libstdc++.so` does not exist, you may need to create a symlink:

```
sudo ln -s /usr/lib/x86_64-linux-gnu/libstdc++.so.6 /usr/lib/x86_64-linux-gnu/libstdc++.so
```

```
git clone https://github.com/irods/irods_client_fuse
cd irods_client_fuse
mkdir build
cd build
cmake ../
make
```

Running iRODS FUSE:
-------------------

Make sure the iCommands are already capable of logging into your Zone:

```
iinit
```

Mount the home collection to a local directory:

```
mkdir /usr/tmp/fmount
irodsFs /usr/tmp/fmount
```

The user's home collection is now mounted. The iRODS files and sub-collections
in the user's home collection should be accessible with normal UNIX commands
through the /usr/tmp/fmount directory.


Run irodsFs in debug mode
-------------------------

To run irodsFs in debug mode:

1) set the env variable irodsLogLevel to 4:

```
export irodsLogLevel=4
```

2) run irodsFs in debug mode, e.g.

```
irodsFs -d yourMountPoint
```

Use irodsFsCtl for control
--------------------------

1) Clear all metadata cache:
```
irodsFsCtl.py reset_cache yourMountPoint
```

2) Show established connections:
```
irodsFsCtl.py show_connections yourMountPoint
```

Helpful options
---------------

1) Arguments
- `-h`: show help message
- `-v` or `-V`: show version
- `-d`: run irodsFs in debug mode
- `-t <ticket_number>`: use ticket
- `-w <irods_working_dir>`: set working directory
- `-o <fuse_args>`: fuse-specific parameters

2) Enabling/disabling features
- `--nocache`: Disable all caching features (Buffered IO, Preload, Metadata
   Cache)
- `--nodirectio`: Disable direct_io to allow OS-level page caching. By default,
   the direct_io is turned on.
- `--nopreload`: Disable Preload feature that pre-fetches file blocks in
   advance. By default, the preload is turned on.
- `--nocachemetadata`: Disable metadata caching feature. By default, the
   metadata cacheing is turned on.
- `--connreuse`: Set to reuse network connections for performance. This may
   provide inconsistent metadata with mysql-backed iCAT. By default, connections
   are not reused.

3) Other configurations
- `--maxconn <num_conn>`: Set max number of network connection to be established
   at the same time. By default, this is set to 10.
- `--blocksize <block_size>`: Set block size at data transfer. All transfer is
   made in a block-level for performance. By default, this is set to
   1048576(1MB).
- `--conntimeout <timeout_in_seconds>`: Set timeout of a network connection.
   After the timeout, idle connections will be automatically closed. By default,
   this is set to 300(5 minutes).
- `--connkeepalive <interval_in_seconds>`: Set interval of keepalive requests.
   For every keepalive interval, keepalive message is sent to iCAT to keep
   network connections live. By default, this is set to 180(3 minutes).
- `--conncheckinterval <interval_in_seconds>`: Set intervals of connection
   timeout check. For every check intervals, all connections established are
   checked to figure out if they are timed-out. By default, this is set to
   10(10 seconds).
- `--apitimeout <timeout_in_seconds>`: Set timeout of iRODS client API calls.
   If an API call does not respond before the timeout, the API call and the
   network connection associated with are killed. By default, this is set to
   90(90 seconds).
- `--preloadblocks <num_blocks>`: Set the number of blocks pre-fetched. By
   default, this is set to 3 (next 3 blocks in advance).
- `--metadatacachetimeout <timeout_in_seconds>`: Set timeout of a metadata
   cache. Metadata caches are invalidated after the timeout. By default, this is
   set to 180(3 minutes).

For example, following command will 1) reuse connections, 2) prefetch next
5 blocks and 3) set timeout of metadata cache to 1 hour.
```
irodsFs --connreuse --preloadblocks 5 --metadatacachetimeout 3600 yourMountPoint
```

An example of using ticket is:
```
irodsFs --connreuse -t yourTicket -w dataDirectory yourMountPoint
```


WARNING
-------

When a collection is mounted using irodsFs, uses of iCommands
such as iput, irm, icp, etc that change the content of the collection should be
avoided because the FUSE implementation caches the attributes of the contents of
the collection.
