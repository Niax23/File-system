# File-system
Implementation of a simple EXT file system, project of the Operating System 2024Spring

This simple file system uses the mmap() system call to simulate a local file as a disk. A disk management server is designed to handle disk space, and a file system management server is responsible for managing the file system. Communication between them is done via sockets. It supports most basic and advanced Linux command-line operations, as well as multi-user access and concurrent usage.