#include "Client.hpp"



#define LISTEN_DIR "./backup_dir/"
#define SERVER_IP "192.168.175.254"
#define SERVER_PORT 9000
int main()
{
	BU_Client client(LISTEN_DIR, SERVER_IP, SERVER_PORT);
	client.Start();
	return 0;
}