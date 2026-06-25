CC = gcc
MODULES_DIR = modules
MODULES := tmr_httpd.c \
$(MODULES_DIR)/base64.c \
$(MODULES_DIR)/run_log.c 

INCDIR = -I$(MODULES_DIR) -I.
LDLINK = -Llibcjson -lcjson

all:
	$(CC) $(MODULES) $(INCDIR) $(LDLINK) -o tmr_httpd.bin

run:
	export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:./libcjson
	./tmr_httpd.bin

clean:
	rm -rf tmr_httpd.bin

	


