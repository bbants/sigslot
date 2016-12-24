# sigslot
a C++ 11 version of Sigslot implement

Features:
1. no extra dependency
2. provides two versions, the namedsigslot version supports 
to store signals locally, slots can be connected by signal name.
3. support bidirection disconnection. you can either reset a connection to stop receiving further events,or disconnect explicitly from a sginal
4. supports bidirection enable/disable. that means you can keep a signal or a connection but disable event dispatching
5. header only, each implement is self-contained.
6. an individual connection is stored in a shared_ptr.
7. provides a container class('connections') to hold all connected connections' shared_ptr.
