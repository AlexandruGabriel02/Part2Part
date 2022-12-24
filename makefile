all:
	g++ -Wall peer.cpp -lpthread -o peer 
	g++ -Wall server.cpp $$(mysql_config --libs) -lpthread -o server $$(mysql_config --cflags)
clean:
	rm -f peer server