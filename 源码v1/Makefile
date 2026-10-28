.PHONY:all
all:TcpServer calClient
TcpServer: Main.cc
	g++ -o $@ $^ -std=c++11
calClient:calClient.cc
	g++ -o $@ $^ -std=c++11 -lpthread 
.PHONY:clean
clean:
	rm -f TcpServer calClient