dirs-$(NET_UNIX) += unix/
obj-y  += socket.o
obj-$(NET_INET) += netbuf.o
obj-$(NET_INET) += netif.o
dirs-$(NET_INET) += inet/
