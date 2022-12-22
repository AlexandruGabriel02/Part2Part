all:
	g++ peer.cpp -lpthread -o peer 
	g++ server.cpp $$(mysql_config --libs) -lpthread -o server $$(mysql_config --cflags)
clean:
	rm -f peer server