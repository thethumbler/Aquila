dirs-$(DEV_CONSOLE) += console/
dirs-$(DEV_PTY)  += pty/
dirs-$(DEV_UART) += uart/
obj-y += tty.o
obj-y += generic.o
