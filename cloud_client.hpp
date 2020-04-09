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


class FileTool//�ļ�������
{
public:
	//���ļ��ж�ȡ��������
	static bool Read(const std::string& name, std::string *body)
	{
		std::ifstream fs(name, std::ios::binary);
		if (fs.is_open() == false)
		{
			std::cout << "open file " << name << " failed\n";
			return false;
		}
		//��ȡ�ļ���С�Ľӿ�
		int64_t fsize = boost::filesystem::file_size(name);
		body->resize(fsize);
		fs.read(&(*body)[0], fsize);//��Ϊbody��һ��ָ��
		if (fs.good() == false)
		{
			std::cout << "file " << name << " read data failed\n";
			return false;
		}
		fs.close();
		return true;

	}
	//���ļ���д������
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
	bool Insert(const std::string &key, const std::string &val)//����/��������
	{
		_backup_list[key] = val;
		Storage();
		return true;
	}
	bool GetEtag(const std::string &key, std::string *val)//ͨ���ļ���ȡԭ�е�etag��Ϣ
	{
		auto it = _backup_list.find(key);
		if (it == _backup_list.end())
			return false;
		*val = it->second;
		return true;
	}
	bool Storage()//�־û��洢
	{
		//��_file_list�е����ݽ��г־û��洢
		//���ݶ�����г־û��洢---���л�
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
	bool InitLoad()//��ʼ������ԭ������
	{
		//1.����������ļ������ݶ�ȡ����
		std::string body;
		if (FileTool::Read(_store_file, &body) == false)
		{
			return false;
		}
		//2.�����ַ�����������\r\n���зָ�
		std::vector<std::string> list;
		boost::split(list, body, boost::is_any_of("\r\n"),
			boost::token_compress_off);
		//3.ÿһ�а��տո���зָ�-ǰ����key�������val
		for (auto i : list)
		{
			size_t pos = i.find(" ");
			if (pos == std::string::npos)
			{
				continue;
			}
			std::string key = i.substr(0, pos);
			std::string val = i.substr(pos + 1);

			//4.��key/val��ӵ�_file_list��
			Insert(key, val);
		}
		return true;
	}
private:
	std::string _store_file;//�־û��洢�ļ�����
	std::unordered_map<std::string, std::string> _backup_list;//���ݵ��ļ���Ϣ
};


class CloudClient
{
public:
	CloudClient(const std::string &filename,const std::string &store_file,
		const std::string& srv_ip,uint16_t srv_port) :
		_listen_dir(filename),data_manage(store_file),_srv_ip(srv_ip),_srv_port(srv_port){}
	bool Start()//���������ļ���������
	{
		data_manage.InitLoad();
		while (1)
		{
			std::vector<std::string> list;
			GetBackUpFileList(&list);//��ȡ�����е���Ҫ���ݵ��ļ�����
			for (int i = 0; i < list.size(); i++)
			{
				std::string name = list[i];
				std::string pathname = _listen_dir + name;

				std::cout << pathname << " is need to backup" <<std::endl;
				std::string body;
				FileTool::Read(pathname, &body);//��ȡ�ļ����ݣ���Ϊ�ϴ���������
				httplib::Client client(_srv_ip.c_str(), _srv_port);
				std::string req_path = "/" + name;
				auto rsp = client.Put(req_path.c_str(), body, "application/octet-stream");
				if (rsp == NULL || (rsp != NULL && rsp->status != 200))
				{
					//����ļ��ϴ�����ʧ���ˣ���ʲô������
					std::cout << pathname << " backup failed" << std::endl;
					continue;
				}
				std::string etag;
				GetEtag(pathname, &etag);
				data_manage.Insert(name, etag);//���ݳɹ������/������Ϣ
				std::cout << pathname << " backup success" << std::endl;
			}
			Sleep(1000);//����1000����
		}
		return true;
	}
	bool GetBackUpFileList(std::vector<std::string> *list)//��ȡ��Ҫ���ݵ��ļ��б�
	{
		if (boost::filesystem::exists(_listen_dir) == false)
		{
			boost::filesystem::create_directory(_listen_dir);//��Ŀ¼�������򴴽�
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
				list->push_back(name);//��ǰetag��ԭ��etag��ͬ����Ҫ����
			}
		}
		return true;
	}
	static bool GetEtag(std::string &pathname, std::string *etag)//�����ļ���etag��Ϣ
	{
		//�����ļ���etag���ļ���С-�ļ����һ���޸�ʱ��
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