#pragma once
#define _SCL_SECURE_NO_WARNINGS
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include "httplib.h"


class FileTool//文件工具类
{
public:
	//从文件中读取所有内容
	static bool Read(const std::string& name, std::string *body)
	{
		std::ifstream fs(name, std::ios::binary);
		if (fs.is_open() == false)
		{
			std::cout << "open file " << name << " failed\n";
			return false;
		}
		//获取文件大小的接口
		int64_t fsize = boost::filesystem::file_size(name);
		body->resize(fsize);
		fs.read(&(*body)[0], fsize);//因为body是一个指针
		if (fs.good() == false)
		{
			std::cout << "file " << name << " read data failed\n";
			return false;
		}
		fs.close();
		return true;

	}
	//向文件中写入数据
	static bool Write(const std::string &name, const std::string& body)
	{
		std::ofstream ofs(name, std::ios::binary);
		if (ofs.is_open() == false)
		{
			std::cout << "open file " << name << " failed\n";
			return false;
		}
		ofs.write(&body[0], body.size());
		if (ofs.good() == false)
		{
			std::cout << "file " << name << " write file falied\n";
			return false;
		}
		ofs.close();
		return true;
	}

};

class DataManager
{
public:
	DataManager(const std::string &filename) :
		_store_file(filename)
	{}
	bool Insert(const std::string &key, const std::string &val)//插入/更新数据
	{
		_backup_list[key] = val;
		Storage();
		return true;
	}
	bool GetEtag(const std::string &key, std::string *val)//通过文件获取原有的etag信息
	{
		auto it = _backup_list.find(key);
		if (it == _backup_list.end())
			return false;
		*val = it->second;
		return true;
	}
	bool Storage()//持久化存储
	{
		//将_file_list中的数据进行持久化存储
		//数据对象进行持久化存储---序列化
		//filename etag\r\n
		std::stringstream tmp;
		auto it = _backup_list.begin();
		for (; it != _backup_list.end(); it++)
		{
			tmp << it->first << " " << it->second << "\r\n";
		}
		FileTool::Write(_store_file, tmp.str());
		return true;
	}
	bool InitLoad()//初始化加载原有数据
	{
		//1.将这个备份文件的数据读取出来
		std::string body;
		if (FileTool::Read(_store_file, &body) == false)
		{
			return false;
		}
		//2.进行字符串处理，按照\r\n进行分割
		std::vector<std::string> list;
		boost::split(list, body, boost::is_any_of("\r\n"),
			boost::token_compress_off);
		//3.每一行按照空格进行分割-前边是key，后边是val
		for (auto i : list)
		{
			size_t pos = i.find(" ");
			if (pos == std::string::npos)
			{
				continue;
			}
			std::string key = i.substr(0, pos);
			std::string val = i.substr(pos + 1);

			//4.将key/val添加到_file_list中
			Insert(key, val);
		}
		return true;
	}
private:
	std::string _store_file;//持久化存储文件名称
	std::unordered_map<std::string, std::string> _backup_list;//备份的文件信息
};


class CloudClient
{
public:
	CloudClient(const std::string &filename,const std::string &store_file,
		const std::string& srv_ip,uint16_t srv_port) :
		_listen_dir(filename),data_manage(store_file),_srv_ip(srv_ip),_srv_port(srv_port){}
	bool Start()//完成整体的文件备份流程
	{
		data_manage.InitLoad();
		while (1)
		{
			std::vector<std::string> list;
			GetBackUpFileList(&list);//获取到所有的需要备份的文件名称
			for (int i = 0; i < list.size(); i++)
			{
				std::string name = list[i];
				std::string pathname = _listen_dir + name;

				std::cout << pathname << " is need to backup" <<std::endl;
				std::string body;
				FileTool::Read(pathname, &body);//读取文件数据，作为上传请求正文
				httplib::Client client(_srv_ip.c_str(), _srv_port);
				std::string req_path = "/" + name;
				auto rsp = client.Put(req_path.c_str(), body, "application/octet-stream");
				if (rsp == NULL || (rsp != NULL && rsp->status != 200))
				{
					//这个文件上传备份失败了，就什么都不干
					std::cout << pathname << " backup failed" << std::endl;
					continue;
				}
				std::string etag;
				GetEtag(pathname, &etag);
				data_manage.Insert(name, etag);//备份成功则插入/更新信息
				std::cout << pathname << " backup success" << std::endl;
			}
			Sleep(1000);//休眠1000毫秒
		}
		return true;
	}
	bool GetBackUpFileList(std::vector<std::string> *list)//获取需要备份的文件列表
	{
		if (boost::filesystem::exists(_listen_dir) == false)
		{
			boost::filesystem::create_directory(_listen_dir);//若目录不存在则创建
		}
		boost::filesystem::directory_iterator begin(_listen_dir);
		boost::filesystem::directory_iterator end;
		for (; begin != end; begin++)
		{
			if (boost::filesystem::is_directory(begin->status()))
			{
				continue;
			}
			std::string pathname = begin->path().string();
			std::string name = begin->path().filename().string();
			std::string cur_etag;
			GetEtag(pathname, &cur_etag);
			std::string old_etag;
			data_manage.GetEtag(name, &old_etag);
			if (cur_etag != old_etag)
			{
				list->push_back(name);//当前etag与原有etag不同则需要备份
			}
		}
		return true;
	}
	static bool GetEtag(std::string &pathname, std::string *etag)//计算文件的etag信息
	{
		//计算文件的etag：文件大小-文件最后一次修改时间
		int64_t fsize = boost::filesystem::file_size(pathname);
		time_t mtime = boost::filesystem::last_write_time(pathname);
		*etag = std::to_string(fsize) + "-" + std::to_string(mtime);
		return true;
	}
private:
	std::string _listen_dir;
	DataManager data_manage;
	std::string _srv_ip;
	uint16_t _srv_port;
};