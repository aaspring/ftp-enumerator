OS := $(shell uname)

ifeq ($(OS), Darwin)
	BASE_ORPHAN_DIR = /opt
	BSD_COMPILE_FLAG =
else
	BASE_ORPHAN_DIR = /opt/drewsOrphanPkgs
	BSD_COMPILE_FLAG = -lbsd
endif

SWIG_DIR := swigConversions
LOCAL_LIB_DIR := $(shell pwd)/localLibs

ALL_LOCAL_LIB_INCLUDES := -I$(LOCAL_LIB_DIR)/libconfig/include \
		-I$(LOCAL_LIB_DIR)/libevent/include -I$(LOCAL_LIB_DIR)/jansson/include
ALL_LOCAL_LIB_LIBRARIES := -L$(LOCAL_LIB_DIR)/libconfig/lib \
		-L$(LOCAL_LIB_DIR)/libevent/lib -L$(LOCAL_LIB_DIR)/jansson/lib

CC = gcc
LFLAGS = -lm -lpthread -lssl -lcrypto $(ALL_LOCAL_LIB_LIBRARIES) \
		-lconfig -ldb -levent_openssl -levent -ljansson
CFLAGS = -g -Wno-deprecated-declarations -Werror $(BSD_COMPILE_FLAG) \
		$(ALL_LOCAL_LIB_INCLUDES)

C_TARGETS = bin/ftpEnumerator bin/parseCerts bin/listParseDump \
		bin/portListener
SWIG_TARGETS = $(SWIG_DIR)/_terminationCodes.so $(SWIG_DIR)/_interestCodes.so

all : $(SWIG_TARGETS) $(C_TARGETS)

findDeps = .isSetup $(patsubst $(realpath .)/%, %, $(realpath $(filter-out %.o:, $(shell gcc $(ALL_LOCAL_LIB_INCLUDES) -MM $(1)))))
sourceFromDeps = $(filter %.c, $(1))
removeMakefileFromDeps = $(filter-out Makefile, $(1))

.isSetup :
	@echo "Running pre-build"
	@echo "This will take a while"
	@sleep 2
	rm -rf $(LOCAL_LIB_DIR)
	# Build libconfig
	wget http://www.hyperrealm.com/libconfig/libconfig-1.4.9.tar.gz
	tar -xf libconfig-1.4.9.tar.gz
	cd libconfig-1.4.9; ./configure --prefix=$(LOCAL_LIB_DIR)/libconfig/
	cd libconfig-1.4.9; make
	cd libconfig-1.4.9; make install
	rm libconfig-1.4.9.tar.gz
	rm -rf libconfig-1.4.9/
	# Build libevent
	wget https://github.com/downloads/libevent/libevent/libevent-2.0.21-stable.tar.gz
	tar -xf libevent-2.0.21-stable.tar.gz
	cd libevent-2.0.21-stable; ./configure --prefix=$(LOCAL_LIB_DIR)/libevent/
	cd libevent-2.0.21-stable; make
	cd libevent-2.0.21-stable; make install
	rm -rf libevent-2.0.21-stable/
	rm libevent-2.0.21-stable.tar.gz
	# Build jansson
	wget http://www.digip.org/jansson/releases/jansson-2.7.tar.gz
	tar -xf jansson-2.7.tar.gz
	cd jansson-2.7; ./configure --prefix=$(LOCAL_LIB_DIR)/jansson/
	cd jansson-2.7; make
	cd jansson-2.7; make install
	rm -rf jansson-2.7;
	rm jansson-2.7.tar.gz
	# Setup the local area
	mkdir -p $(SWIG_DIR)
	mkdir -p temp
	touch .isSetup
	mkdir -p bin

$(SWIG_DIR)/_%.so : .isSetup Makefile include/%.h
	@echo "\nUsing SWIG to build $@"
	$(eval moduleName := $(subst _,,$(shell basename $(basename $@))))
	swig -outdir $(SWIG_DIR)/ -python -module $(moduleName) include/$(moduleName).h
	gcc -c -fpic include/$(moduleName)_wrap.c -I/usr/include/python2.7 -I. -o include/$(moduleName)_wrap.o
	gcc -shared include/$(moduleName)_wrap.o -o $@ -lpython2.7
	rm include/$(moduleName)_wrap.c
	rm include/$(moduleName)_wrap.o

bin/ftpEnumerator : \
					Makefile \
					bin/ftpEnumerator.o \
					bin/logger.o \
					bin/dataChannel.o \
					bin/ipQueue.o \
					bin/strQueue.o \
					bin/ctrlChannel.o \
					bin/ctrlSend.o \
					bin/ctrlRead.o \
					bin/DBuffer.o \
					bin/recorder.o \
					bin/base64.o \
					bin/dbKeys.o \
					bin/robotParser.o \
					bin/robotStructs.o \
					bin/listParser.o \
					bin/ctrlEnd.o \
					bin/textParseHelper.o \
					bin/cfaaBannerCheck.o \
					bin/parseResp.o \
					bin/badPort.o \
					bin/ctrlSecurity.o \
					bin/ctrlDeciders.o \
					bin/listParseHelper.o
	@echo "\nBuilding ftpEnumerator"
	$(CC) -o bin/ftpEnumerator $(call removeMakefileFromDeps, $^) $(CFLAGS) $(LFLAGS)

bin/dumpDB : \
					Makefile \
					bin/dumpDB.o
	@echo "\nBuilding dumpDB"
	$(CC) -o bin/dumpDB $(call removeMakefileFromDeps, $^) $(CFLAGS) $(LFLAGS)

bin/parseCerts : \
					Makefile \
					bin/parseCerts.o \
					bin/strQueue.o \
					bin/logger.o
	@echo "\nBuilding parseCerts"
	$(CC) -o bin/parseCerts $(call removeMakefileFromDeps, $^) $(CFLAGS) $(LFLAGS) -lsqlite3

bin/portListener : \
					Makefile \
					bin/portListener.o \
					bin/logger.o
	@echo "\nBuilding portListener"
	$(CC) -o bin/portListener $(call removeMakefileFromDeps, $^) $(CFLAGS) $(LFLAGS)

bin/listParseDump : \
					Makefile \
					bin/listParseDump.o \
					bin/listParser.o \
					bin/logger.o \
					bin/DBuffer.o \
					bin/strQueue.o \
					bin/textParseHelper.o \
					bin/listParseHelper.o
	@echo "\nBuilding listParseDump"
	$(CC) -o bin/listParseDump $(call removeMakefileFromDeps, $^) $(CFLAGS) $(LFLAGS)

bin/ctrlChannel.o : Makefile $(call findDeps, ctrlChannel/ctrlChannel.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/ctrlSend.o : Makefile $(call findDeps, ctrlChannel/ctrlSend.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/ctrlRead.o : Makefile $(call findDeps, ctrlChannel/ctrlRead.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/logger.o : Makefile $(call findDeps, logger/logger.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/dataChannel.o : Makefile $(call findDeps, dataChannel/dataChannel.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/parseResp.o : Makefile $(call findDeps, ctrlChannel/parseResp.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/ipQueue.o : Makefile $(call findDeps, dataStructures/ipQueue.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/strQueue.o : Makefile $(call findDeps, dataStructures/strQueue.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/DBuffer.o : Makefile $(call findDeps, dataStructures/DBuffer.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/recorder.o : Makefile $(call findDeps, recorder/recorder.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/base64.o : Makefile $(call findDeps, recorder/base64.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/ftpEnumerator.o : Makefile $(call findDeps, tools/ftpEnumerator.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/robotParser.o : Makefile $(call findDeps, robotParser/robotParser.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/listParseDump.o : Makefile $(call findDeps, tools/listParseDump.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/robotStructs.o : Makefile $(call findDeps, robotParser/robotStructs.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/listParser.o : Makefile $(call findDeps, dataChannel/listParser.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/listParseHelper.o : Makefile $(call findDeps, dataChannel/listParseHelper.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/cfaaBannerCheck.o : Makefile $(call findDeps, ctrlChannel/cfaaBannerCheck.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/ctrlEnd.o : Makefile $(call findDeps, ctrlChannel/ctrlEnd.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/textParseHelper.o : Makefile $(call findDeps, dataStructures/textParseHelper.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/parseCerts.o : Makefile $(call findDeps, tools/parseCerts.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/portListener.o : Makefile $(call findDeps, tools/portListener.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/badPort.o : Makefile $(call findDeps, ctrlChannel/badPort.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/ctrlSecurity.o : Makefile $(call findDeps, ctrlChannel/ctrlSecurity.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

bin/ctrlDeciders.o : Makefile $(call findDeps, ctrlChannel/ctrlDeciders.c)
	@# DEPENDS : $@ == $^
	@echo "\nBuilding $(shell basename $@)"
	$(CC) -c $(call sourceFromDeps, $^) -o $@ $(CFLAGS)

clean :
	rm -rf bin/*.o
	rm -rf $(C_TARGETS)
	rm -rf bin/*.dSYM
	rm -rf $(SWIG_DIR)/*.so
	rm -rf $(SWIG_DIR)/*.py
	rm -rf $(SWIG_DIR)/*.pyc

