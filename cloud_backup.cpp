#include "cloud_backup.hpp"

void Non_compress()
{
    _cloud_srv::NonHotCompress ncom(GZFILE_DIR,BACKUP_DIR);
    ncom.Start();
    return;
}

void thr_http_server()
{
    _cloud_srv::Server srv;
    srv.Start();
    return;
}

int main()
{
    
    if(boost::filesystem::exists(GZFILE_DIR) == false)
    {
        boost::filesystem::create_directory(GZFILE_DIR);
    }
    if(boost::filesystem::exists(BACKUP_DIR) == false)
    {
        boost::filesystem::create_directory(BACKUP_DIR);
    }
    std::thread thr_compress(Non_compress);//C++11中的线程 --启动非热点文件压缩模块
    std::thread thr_server(thr_http_server);//网络通信服务端模块启动
    thr_compress.join();//等待线程退出
    thr_server.join(); 
    return 0;
}
