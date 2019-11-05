SRC = reflex_server.c echoserver.c
ifneq ($(SERVERLESS),)
CFLAGS += -DSERVERLESS_ENABLE
SRC += serverless_client.c
else
SRC += reflex_ix_client.c
endif

$(eval $(call register_dir, apps, $(SRC)))