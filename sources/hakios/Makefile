.PHONY: all kernel deps userland
all: kernel
kernel:
	cd ../.. && ./lilac build kernel
deps:
	cd ../.. && ./lilac deps build
userland:
	cd ../.. && ./lilac build userspace
