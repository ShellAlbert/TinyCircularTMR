CC = gcc
MODULES_DIR = modules
MODULES := TMR_httpd.c \
$(MODULES_DIR)/base64.c \
$(MODULES_DIR)/run_log.c 

INCDIR = -I$(MODULES_DIR) -I.
LDLINK = -Llibcjson -lcjson

all:
	$(CC) $(MODULES) $(INCDIR) $(LDLINK) -o TMR_httpd.bin
	$(CC) TMR_Capture.c -o TMR_Capture.bin -g
	$(CC) TMR_Plot.c -o TMR_Plot.bin


clean:
	rm -rf TMR_httpd.bin

	


