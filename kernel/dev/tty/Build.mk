dirs-$(DEV_CONSOLE) += vt/
dirs-$(DEV_PTY)  += pty/
dirs-$(DEV_UART) += uart/
obj-y += tty.o
obj-y += generic.o
