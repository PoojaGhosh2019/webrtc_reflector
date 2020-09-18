LINKFLAGS=/LIBPATH:.\openssl-1.1\x64\lib /LIBPATH:"C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Lib\x64" /WX
LIBS=WSock32.lib Winmm.lib AdvAPI32.lib user32.lib libcrypto.lib libssl.lib
INC=-I. -I.\openssl-1.1\x64\include

CC=cl.exe
LINK=link.exe

FLAGS=/D_WINDOWS /DENABLE_MEDIA_ROUTER=1 /D__STD_C /D_CRT_RAND_S /D_CRT_SECURE_NO_DEPRECATE /D_HAS_EXCEPTIONS=0 /DWINVER=0x0A00 /D__STDC_CONSTANT_MACROS /D__STDC_FORMAT_MACROS /D_AMD64_ /EHsc /W3 /DUSE_STANDALONE_ASIO /DDEBUG


default:   server.exe

server.exe: server.o getopt.o
	$(LINK) $(LINKFLAGS) /OUT:server.exe server.o getopt.o $(LIBS)

getopt.o: getopt.cpp
	$(CC) $(FLAGS) $(INC) /c getopt.cpp /Fogetopt.o

server.o: server.cpp
	$(CC) $(FLAGS) $(INC) /c server.cpp /Foserver.o

clean:
	del *.o
	del *.exe
	del server.lib
	del server.exp
