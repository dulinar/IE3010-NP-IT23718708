# Design Diary - NetMessenger

---

## 1. Initial Design

Build the system using a client-server architecture. The server acts as the main communication point and multiple clients connect to it using TCP.

TCP was selected because it provides reliable communication between the clients and the server.

The main components planned were:

- TCP server
- TCP client
- Multiple client handling
- Text-based communication commands
- Chat rooms
- File sharing
- Error handling
- Server logging

---

## 2. Personalization

I added the required student-specific values to the system.

The selected values are:

- Registration Number: `IT23718708`
- Server Port: `14708`
- NID: `NID:7187`
- Server File: `server_8708.c`
- Client File: `client_8708.c`
- Makefile: `Makefile_8708`

The same values were also used in the log file and file storage path.

---

## 3. Server Design

The server was designed to create a TCP socket and listen on port `14708`.

The main socket operations used are:

`socket()` → `setsockopt()` → `bind()` → `listen()` → `accept()`

When a client connects, the server accepts the connection and creates a separate thread to handle that client.

I used a fixed client array to keep track of connected clients.

---

## 4. Multi Client Handling

One of the main design requirements was to support multiple clients at the same time.

I used POSIX threads for this. A new thread is created for each connected client.

A mutex was also added to protect shared information such as the client list and chat rooms.

This prevents different client threads from changing the same data at the same time.

---

## 5. Client Design

The client creates a TCP socket and connects to the server using the server IP address and port number.

The client has a separate receiver thread. This allows the client to receive messages from the server while the user is entering commands.

This was important for features such as broadcast messages, private messages, room messages, and file receiving.

---

## 6. Communication Protocol

I used simple text-based commands so that the system would be easy to test from the terminal.

The main commands are:

- `REGISTER`
- `LIST`
- `BCAST`
- `PMSG`
- `JOIN`
- `LEAVE`
- `ROOMS`
- `RMSG`
- `SENDFILE`
- `QUIT`

The server returns `OK` or `ERR` responses and includes the required `NID:7187` tag.

---

## 7. Messaging and Chat Rooms

After user registration, clients can communicate using broadcast and private messages.

For group communication, I added chat rooms. The server keeps a list of rooms and the clients that have joined each room.

A mutex is used when accessing room information because several client threads can use the rooms at the same time.

---

## 8. File Transfer Design

File transfer was implemented using the `SENDFILE` command.

The file name and file size are sent before the actual file data.

I used the file size to determine how many bytes need to be received. This is important because TCP provides a continuous byte stream and one `recv()` call may not receive the complete file.

The server stores received files using the personalized path:

`./storage/IT23718708/<username>/<filename>`

The server then forwards the file to the selected client or room.

---

## 9. Error Handling

I added error responses for common problems.

Examples include:

- Duplicate username
- Unknown user
- Unknown command
- Invalid command arguments
- User not registered
- Room not found
- File too large
- Storage errors

The purpose was to make sure invalid requests do not cause the server to stop.

---

## 10. Disconnection and Logging

The `QUIT` command was added for normal client disconnection.

The server also handles unexpected client disconnections by removing the client from the active client list and any rooms.

Important server events are written to:

`netmsg_IT23718708.log`

This makes it easier to check what happened during testing.

---

## 11. Testing and Changes

I tested the system using multiple client terminals.

I tested:

- Server startup
- Client connection
- User registration
- Duplicate usernames
- User listing
- Broadcast messages
- Private messages
- Chat rooms
- Room messages
- File transfer
- Invalid commands
- Invalid users
- Client disconnection

During testing, I checked both the server output and client output to confirm that messages and responses were working correctly.

---

## 12. Final Design

The final system uses a simple TCP client-server design.

The server handles multiple clients using POSIX threads and protects shared data using a mutex. The client uses a receiver thread so that messages can be received while the user is entering commands.

The final implementation also includes file sharing, error handling, logging, personalized storage, and the required student-specific values.

This design was chosen because it is simple enough to understand and test while still meeting the main requirements of the assignment.
