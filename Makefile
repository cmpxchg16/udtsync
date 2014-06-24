all:
	$(MAKE) all -C udt4/ && $(MAKE) all -f Makefile.udtsync
clean:
	$(MAKE) clean -C udt4/ && $(MAKE) clean -f Makefile.udtsync
