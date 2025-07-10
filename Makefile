all:
	$(MAKE) -C $(KERNELDIR) M=$(M) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(M) clean
