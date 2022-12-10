all:
	g++ peer.cpp -lpthread -o peer 
	g++ server.cpp -lpthread -o server 
clean:
	rm -f peer server