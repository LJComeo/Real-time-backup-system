cloud_backup:cloud_backup.cpp
	g++ -std=c++11 $^ -o $@ -lz -lpthread -lboost_filesystem -lboost_system
