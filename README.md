This project consists of two main parts.

### Thread-Safe Ring Buffer

The ring buffer is designed so that multiple threads can access it at the same time. Critical sections are protected with locks. To avoid busy-waiting, I used signals and waits to manage the locking.  Additionally, pthread_cond_timedwait is used to enable timed waits.

### Simple Network Daemon

The network daemon simulates handling incoming network packets using the thread-safe ring buffer. Incoming messages are added to the buffer, and several worker threads read these messages to process them. This processing involves applying basic firewall rules and forwarding messages based on specific conditions. The messages are then saved to output files named after the target port numbers.
