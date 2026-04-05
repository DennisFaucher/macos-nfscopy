# nfscopy

A minimal CLI tool for copying files to and from an NFS server on macOS, built on top of [libnfs](https://github.com/sahlberg/libnfs).

Unlike `cp` or `scp`, `nfscopy` speaks NFS directly over the network — no OS-level mount required, and no root privileges needed.

## Usage

```
nfscopy <source> <destination>
```

At least one of `source` or `destination` must be an NFS URL in the form:

```
nfs://<server>/<export>/<path>
```

### Examples

```bash
# Copy a local file to an NFS server
nfscopy /local/file.txt nfs://192.168.1.1/mnt/share/file.txt

# Copy a file from an NFS server to local
nfscopy nfs://192.168.1.1/mnt/share/file.txt /local/copy.txt

# Copy between two paths on the same NFS export
nfscopy nfs://192.168.1.1/mnt/share/src.txt nfs://192.168.1.1/mnt/share/dst.txt
```

> **Note:** NFS-to-NFS copy requires both paths to be on the same server and export.

## Building

### 1. Clone this repo

```bash
git clone [https://github.com/yourusername/nfscopy.git](https://github.com/DennisFaucher/macos-nfscopy.git)
cd macos-nfscopy
```

### 2. Build libnfs as a static library

```bash
git clone https://github.com/sahlberg/libnfs.git
cd libnfs
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
cmake --build build
cd ..
```

### 3. Build nfscopy

```bash
make LIBNFS_PREFIX=./libnfs LIBNFS_LIB=./libnfs/build/lib
```

### 4. (Optional) Install to /usr/local/bin

```bash
make install LIBNFS_PREFIX=./libnfs LIBNFS_LIB=./libnfs/build/lib
```

## Requirements

- macOS (tested on macOS 15)
- [CMake](https://cmake.org/) — install via Homebrew: `brew install cmake`
- Xcode Command Line Tools: `xcode-select --install`

## How it works

libnfs implements the NFS protocol as a userspace library, communicating directly over TCP/UDP sockets. This means:

- No `mount` command or root privileges required
- Works on macOS without the NFS client kernel extension
- The NFS export just needs to be accessible on the network

Progress is printed to stderr for transfers over 10 MiB.

## License

nfscopy is released under the MIT License. libnfs is licensed under LGPL-2.1.
