# UserLevelThreads

User Level Threads Library that mimics the kernel levels threads in their abilities. 
The difference between them to kernel level threads is that they run on user mode 
and not in kernel mode so they can't excecute privileged operations that the OS can.

The functionality of the library is described in the header file.

The architecture of the library is outlined here:

<img width="431" alt="User level threads design" src="https://github.com/Annaseli/UserLevelThreads/assets/44980761/dcfb7159-3a8a-4a72-a8ec-11db539186aa">
