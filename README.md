# Client-Server-messaging
The general purpose of this application is similar to a message feed. UDP clients will post messages to different topic boards and TCP clients will have the option to subscribe to certain topics. A key feature is the `S&F` (store and forward) option, which, when activated, will keep track of messages sent while the client is offline. When the client reconnects, it will receive any message that was posted while it was offline.

The homework framework included the UDP clients. I implemented the server and the TCP clients.

# The server
The server does most of the "thinking". Some general ideas regarding the data structures I used: the topic keeps a pointer list to all its subscribers,
the client structure has a vector memorizing the messages it stored for S&F used as a queue.

# Connection
For a fluid connection process, I invented my own handshake:
```
client: new <ID>
server: OK. / Duplicate ID / Wrong handshake format (doesn't begin with new)
```

Because I need to keep track of the client state (new, reconnected etc), every TCP connection the server gets it verifies if it's a new client
or a reconnected one. If it's reconnected, it sets its attribute to online and checks for the S&F option/messages in queue.
