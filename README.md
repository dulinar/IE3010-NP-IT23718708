# IE3010 Network Programming - NetMessenger

## Project Overview

NetMessenger is a multi-client chat and file-sharing application developed for the IE3010 Network Programming assignment.

The system is written in **C** and uses **TCP/IP sockets** for communication between clients and the server. Multiple clients can connect to the server at the same time. The server uses POSIX threads to handle each connected client.

The system supports:

* User registration and online user listing
* Broadcast messages
* Private messages
* Chat rooms
* Room messages
* File sharing
* Error handling
* Graceful client disconnection
* Server-side logging



## Student and System Details

Registration Number - IT23718708              
Last Four Digits    - 8708                    
Server Port         - 14708                   
Node ID (NID)       - NID:7187                
Server Source       - server_8708.c        
Client Source       - client_8708.c       
Makefile            - Makefile_8708        
Log File            - netmsg_IT23718708.log
Storage Directory   - ./storage/IT23718708/ 

The server port is calculated using:

`6000 + 8708 = 14708`

The NID is based on digits 3-6 of the numeric part of the registration number.



## Requirements

The program is designed to run on a Linux system with:

* GCC
* POSIX threads
* TCP/IP socket support
* Make
* Standard Linux command-line tools



## Files

server_8708.c   -    Server program
client_8708.c   -    Client program
Makefile_8708   -    Build and run commands
netmsg_IT23718708.log - Server log file
storage/        -     Received file storage



## How to Compile

Open a terminal in the project directory and run:

`make -f Makefile_8708`


This builds both the server and client programs.

The following executable files will be created:

`server_8708
client_8708`

To build only the server:

`make -f Makefile_8708 server`

To build only the client:

`make -f Makefile_8708 client`



## How to Run

### 1. Start the Server

Run:

`./server_8708`

The server will listen for TCP connections on port `14708`.

You can also use:

`make -f Makefile_8708 run-server`



### 2. Start a Client

Open another terminal and run:

`./client_8708 127.0.0.1`

Or use:

`make -f Makefile_8708 run-client`



## Main Commands

After connecting to the server, the following commands can be used.

### Register

```
REGISTER <username>
```

Example:

```
REGISTER Alice
```

### List Users

```
LIST
```

### Broadcast Message

```
BCAST <message>
```

Example:

```
BCAST Hello everyone
```

### Private Message

```
PMSG <username> <message>
```

Example:

```
PMSG Bob Hello Bob
```

### Join a Chat Room

```
JOIN <room>
```

Example:

```
JOIN developers
```

### List Chat Rooms

```
ROOMS
```

### Send a Room Message

```
RMSG <room> <message>
```

Example:

```
RMSG developers Hello everyone
```

### Leave a Chat Room

```
LEAVE <room>
```

Example:

```
LEAVE developers
```

### Send a File

```
SENDFILE <target> <filepath>
```

Example:

```
SENDFILE Bob ./test.txt
```

The server receives the file and stores a copy under the personalized storage directory:

```
./storage/IT23718708/<sender_username>/<filename>
```

The file can then be forwarded to the target user or chat room.

### Disconnect

```
QUIT
```

This closes the client connection gracefully.

## Server Logging

The server records important events in:

```
netmsg_IT23718708.log
```

The log includes events such as:

* Server startup
* Client connections
* User registration
* Private messages
* File transfers
* Client disconnections
* Other important server events

Each log entry contains a timestamp.

## File Storage

Received files are stored using the following structure:

```
storage/
└── IT23718708/
    └── <username>/
        └── <filename>
```

For example:

```
storage/
└── IT23718708/
    └── Alice/
        └── report.txt
```



## Concurrency

The server uses **POSIX threads**.

A separate thread is created for each connected client. This allows multiple clients to communicate with the server at the same time.

A mutex is used when accessing shared client and chat-room information to reduce conflicts between threads.


## Error Handling

The server returns an error response when an invalid operation is received.

Examples include:

* Registering an already used username
* Sending a message to an unknown user
* Using an invalid command
* Sending a command before registration
* Using invalid command arguments
* Sending a file that is too large

The server also handles unexpected client disconnections and removes the disconnected client from the active user and room lists.


## Cleaning the Build

To remove the compiled server and client programs:

`make -f Makefile_8708 clean`

To remove generated files, logs, storage, and archives:

`make -f Makefile_8708 distclean`


## Help

To see the available Makefile commands:

`make -f Makefile_8708 help`
