#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <unordered_map>
#include <zlib.h>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <pthread.h>
#include "httplib.h"

#define NONHOT_TIME 5//最后一次访问时间在5s内
#define INTERVAL_TIME 30//非热点的检测，每30s一次
#define BACKUP_DIR "./backup_dir/"//文件备份路径
#define GZFILE_DIR "./gzfile_dir/"//文件压缩包存放路径
#define DATA_FILEPATH "./list"//数据管理里模块的数据备份文件名称
namespace _cloud_srv
{
    class FileTool
    {
        public:
            //从文件中读取所有内容
            static bool Read(const std::string& name,std::string *body)
            {
                std::ifstream fs(name,std::ios::binary);
                if(fs.is_open() == false)
                {
                    std::cout << "open file " << name <<" failed\n";
                    return false;
                }
                int64_t fsize = boost::filesystem::file_size(name);
                body->resize(fsize);
                fs.read(&(*body)[0],fsize);
                if(fs.good() == false)
                {
                    std::cout <<"file " << name <<" read data failed\n";
                    return false;
                }
                fs.close();
                return true;

            }
            //向文件中写入数据
            static bool Write(std::string &name,const std::string& body)
            {
                std::ofstream ofs(name,std::ios::binary);
                if(ofs.is_open() == false)
                {
                    std::cout << "open file " << name << " failed\n";
                    return false;
                }
                ofs.write(&body[0],body.size());
                if(ofs.good() == false)
                {
                    std::cout <<"file " <<name << " write file falied\n";
                    return false;
                }
                ofs.close();
                return true;
            }
            
    };
    class CompressUtil//压缩模块
    {
        public:
            static bool Compress(const std::string& src,const std::string& dst)
            {
                //文件压缩
                std::string body;
                FileTool::Read(src,&body);//将需要压缩的文件先放入body中
                gzFile gf = gzopen(dst.c_str(),"wb");//打开压缩包文件，准备后序进行压缩
                if(gf == NULL)
                {
                    std::cout << "open file " << dst << " failed\n";
                    return false;
                }
                int wlen = 0;
                while(wlen < body.size())
                {
                    int ret = gzwrite(gf , &body[wlen] , body.size() - wlen);
                    if(ret == 0)
                    {
                        std::cout << "file "<< dst << " write compress data failed\n";
                        return false;
                    }
                    wlen += ret;
                }
                gzclose(gf);
                return true;
            }
            static bool UnCompress(const std::string&src,const std::string& dst)
            {
                std::ofstream ofs(dst,std::ios::binary);
                if(ofs.is_open() == false)
                {
                    std::cout <<"open file " << dst <<" failed\n";
                    return false;
                }
                gzFile gf = gzopen(src.c_str(),"rb");
                if(gf == NULL)
                {
                    std::cout << "open file " << src << " failed\n";
                    ofs.close();
                    return false;
                }
                int ret;
                char tmp[4096] = {0};
                while((ret = gzread(gf,tmp,4096))>0)
                {
                    ofs.write(tmp,ret);
                }
                ofs.close();
                gzclose(gf);
                return true;
            }
    };
 
    class DataManager
    {
        public:
            DataManager(const std::string &path)
                :_back_file(path)
            {
                pthread_rwlock_init(&_rwlock,NULL);//初始化读写锁
            }
            ~DataManager()
            {
                pthread_rwlock_destroy(&_rwlock);
            }
            //判断文件是否存在
            bool Exists(const std::string &name)
            {
                pthread_rwlock_rdlock(&_rwlock);
                auto it = _file_list.find(name);
                if(it == _file_list.end())
                {
                    pthread_rwlock_unlock(&_rwlock);
                    return false;
                }
                pthread_rwlock_unlock(&_rwlock);
                return true;
            }
            //判断文件是否已经压缩
            bool IsCompress(const std::string &name)
            {
                pthread_rwlock_rdlock(&_rwlock);
                auto it = _file_list.find(name);
                if(it == _file_list.end())
                {
                    pthread_rwlock_unlock(&_rwlock);
                    return false;
                }
                if(it->first == it->second)
                {
                    pthread_rwlock_unlock(&_rwlock);
                    return false;
                }
                pthread_rwlock_unlock(&_rwlock);
                return true;
            }
            //获取未压缩文件列表
            bool NonCompressList(std::vector<std::string> *list)
            {
                pthread_rwlock_rdlock(&_rwlock);
                for(auto it = _file_list.begin();it != _file_list.end();it++)
                {
                    if(it->first == it->second)
                    {
                        list->push_back(it->first);
                    }
                }
                pthread_rwlock_unlock(&_rwlock);
                return true;
            }
            //插入/更新数据
            bool Insert(const std::string &src,const std::string& dst)
            {
                pthread_rwlock_wrlock(&_rwlock);
                _file_list[src] = dst;
                pthread_rwlock_unlock(&_rwlock);
                Storage();
                return true;
            }
            //获取所有文件名称
            bool GetAllName(std::vector<std::string> *list)
            {
                pthread_rwlock_rdlock(&_rwlock);
                auto it = _file_list.begin();
                for(;it != _file_list.end();it++)
                {
                    list->push_back(it->first);
                }
                pthread_rwlock_unlock(&_rwlock);
                return true;
            }
            //数据改变后持久化存储
            bool Storage()
            {
                std::stringstream tmp;
                pthread_rwlock_rdlock(&_rwlock);
                auto it = _file_list.begin(); 
                for(;it != _file_list.end();it++)
                {
                    tmp << it->first << " "<< it->second << "\r\n";
                }
                pthread_rwlock_unlock(&_rwlock);
                FileTool::Write(_back_file,tmp.str());
                return true;
            }
            //获取被压缩的文件
            bool GetgzName(const std::string &src,std::string *dst)
            {
                auto it = _file_list.find(src);
                if(it == _file_list.end())
                {
                    return false;
                }
                *dst = it->second;
                return true;
            }
            //初始化加载原有数据
            bool InitLoad()
            {
                //1.将这个备份文件的数据读取出来
                std::string body;
                if(FileTool::Read(_back_file,&body) == false)
                {
                    return false;
                } 
                //2.进行字符串处理，按照\r\n进行分割
                std::vector<std::string> list;
                boost::split(list,body,boost::is_any_of("\r\n"),
                        boost::token_compress_off);
                //3.每一行按照空格进行分割-前边是key，后边是val
                for(auto i : list)
                {
                    size_t pos = i.find(" ");
                    if(pos == std::string::npos)
                    {
                        continue;
                    }
                    std::string key = i.substr(0,pos);
                    std::string val = i.substr(pos+1);
                
                //4.将key/val添加到_file_list中
                    Insert(key,val);
                }
                return true;
            }
        private:
            std::string _back_file;//持久化数据存储文件名称
            std::unordered_map<std::string,std::string> _file_list;
            pthread_rwlock_t _rwlock;
    };
    DataManager data_manage(DATA_FILEPATH);    
    class NonHotCompress
    {
        public:
            NonHotCompress(const std::string gz_dir,const std::string bu_dir)
                :_gz_dir(gz_dir),_bu_dir(bu_dir){}
            bool Start()//总体向外提供的功能接口，开始压缩模块
            {
                while(1)
                {
                    //1.获取所有未压缩的文件列表
                    std::vector<std::string> list;
                    data_manage.NonCompressList(&list);
                    //2.逐个判断这个文件是不是热点文件
                    for(int i = 0;i < list.size();i++)
                    {
                        bool ret = FileIsHot(list[i]);
                        if(ret == false)
                        {
                            std::cout <<"non hot file " << list[i] << std::endl;
                            std::string s_filename = list[i];
                            std::string d_filename = list[i] + ".gz";
                            std::string src_name = _bu_dir + s_filename;
                            std::string dst_name = _gz_dir + d_filename;
                            //3.如果是非热点文件，则压缩这个文件，删除源文件
                            if(CompressUtil::Compress(src_name,dst_name) == true)
                            {
                                data_manage.Insert(s_filename,d_filename);
                                unlink(src_name.c_str());//删除源文件
                            }
                        }
                    }
                    //4.休眠一会
                    sleep(INTERVAL_TIME);
                }
                return true;
            }
        private:
            //判断一个文件是否是一个热点文件
            bool FileIsHot(const std::string &name)
            {
                time_t cur_t = time(NULL);//获取当前时间
                struct stat st;
                if(stat(name.c_str(),&st) < 0)
                {
                    std::cout <<"get file " << name << " stat failed\n";
                    return false;
                }
                if((cur_t - st.st_atime) > NONHOT_TIME)
                {
                    return false;
                }
                return true;
            }
        private:
            std::string _gz_dir;//压缩后文件的存储路径
            std::string _bu_dir;//压缩前文件的所在路径
    };
    class Server
    {
        public:
            bool Start()//启动网络通信模块接口
            {
                _server.Put("/(.*)",Upload);
                _server.Get("/list",List);
                //正则表达式: .* 表示匹配任意字符串 ()表示捕捉这个字符串
                _server.Get("/download/(.*)",Download);
                //为了避免有文件名叫list与list请求混淆

                _server.listen("0.0.0.0",9000);//搭建tcp服务器，进行http数据接收处理
                return true;
            }
        private:
            static void Upload(const httplib::Request& req,httplib::Response& rsp)
            {
                std::string filename = req.matches[1];//纯文件名称
                std::string pathname = BACKUP_DIR + filename;
                //组织文件路径名,备份到指定的路径下
                FileTool::Write(pathname,req.body);//向文件中写入数据
                std::cout << "upload success" <<  std::endl;
                data_manage.Insert(filename,filename);//添加文件信息到数据管理模块
                rsp.status = 200;
                return;
            }
            static void List(const httplib::Request& req,httplib::Response& rsp)
            {
                std::vector<std::string> list;
                data_manage.GetAllName(&list);
                std::stringstream tmp;
                tmp << "<html><body><hr />";
                for(int i = 0;i < list.size();i++)
                {
                    tmp<<"<a href='/download/" <<list[i] <<  "'>" << list[i] << "</a>";
                    tmp << "<hr />";
                }
                tmp << "<hr /></body></html>";

                rsp.set_content(tmp.str().c_str(),tmp.str().size(),"text/html");
                rsp.status = 200;
                return;
            }
            static void Download(const httplib::Request& req,httplib::Response& rsp)
            {
                std::string filename = req.matches[1];//这就是前边路由注册时捕捉的(.*)
                if(data_manage.Exists(filename) == false)
                {
                    rsp.status = 404;//文件不存在则page not found
                    return;
                }
                std::string pathname = BACKUP_DIR + filename;
                std::cout <<"filename : "<< filename <<std::endl;
                std::cout <<"pathname : "<< pathname <<std::endl;
                if(data_manage.IsCompress(filename) == true)
                {
                    std::string gzfile;
                    data_manage.GetgzName(filename,&gzfile);
                    std::string gzpathname = GZFILE_DIR + gzfile;
                    CompressUtil::UnCompress(gzpathname,pathname);
                    unlink(gzpathname.c_str());//删除压缩包
                    data_manage.Insert(filename,filename);//更新数据信息
                }
                FileTool::Read(pathname,&rsp.body);
                rsp.set_header("Content-Type","application/octet-stream");
                rsp.status = 200;
                return;
            }
        private:
            std::string _file_dir;//文件上传备份路径
            httplib::Server _server;
    };
};

