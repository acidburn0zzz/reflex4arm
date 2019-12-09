SRC = reflex_server.c echoserver.c
ifneq ($(SERVERLESS_ENABLE),)
CFLAGS += -DSERVERLESS_ENABLE
endif
SRC += serverless_client.c reflex_ix_client.c

$(eval $(call register_dir, apps, $(SRC)))