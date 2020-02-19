all:
	@cd src && $(MAKE)
debug:
	@cd src && $(MAKE) debug
clean:
	@cd src && $(MAKE) clean

.PHONY: all clean
