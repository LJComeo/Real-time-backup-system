#include "cloud_client.hpp"


#define STORE_FILE "./list"
#define LISTEN_DIR "./backup_dir/"
#define SERVER_IP "192.168.175.254"
#define SERVER_PORT 9000
int main()
{
	CloudClient client(LISTEN_DIR, STORE_FILE, SERVER_IP, SERVER_PORT);
	client.Start();
	return 0;
}