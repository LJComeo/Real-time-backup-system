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

#define STORE_FILE "./list"
/*
1.������Ҫ���ݵ��ļ�---�ж�ETagֵ
2.����Ҫ���ݵ��ļ�һ���������httplib�ͻ��˵�����PUT�������
*/

//�ļ��࣬��Ҫ���ڶ�������д���ļ�
class File
{
public:
	static bool ReadFile(const std::string &iname, std::string *oname)//���ļ��е����ݴ�iname�ж���oname��
	{
		std::ifstream ifs(iname, std::ios::binary);
		if (ifs.is_open() == false)
		{
			std::cout << "open file " << iname << " is failed" << std::endl;
			return false;
		}
		int64_t size = boost::filesystem::file_size(iname);
		oname->resize(size);
		ifs.read(&(*oname)[0], size);
		if (ifs.good() == false)
		{
			std::cout << "read " << iname << " data failed" << std::endl;
		}
		ifs.close();
		return true;
	}
	static bool WriteFile(const std::string &filename, std::string &name)//���ļ�name���ļ��е�����д��filename��
	{
		std::ofstream ofs(filename, std::ios::binary);
		if (ofs.is_open() == false)
		{
			std::cout << "open file " << filename << " is failed" << std::endl;
		}

		ofs.write(&name[0], name.size());
		if (ofs.good() == false)
		{
			std::cout << "write " << filename << " data failed" << std::endl;
		}
		ofs.close();
		return true;
	}
};

//�ļ����ݹ�����
class FileData
{
	std::string m_Save_file;//�־û��洢�ļ�
	std::unordered_map<std::string, std::string> m_SaveFile_list;//�����ļ�����Ϣ��ETag��Ϣ
public:
	FileData(const std::string &filename) :m_Save_file(filename){}
	bool GetETag(const std::string &key,std::string *val)//��ȡ�ļ���ETag��Ϣ
	{
		auto i = m_SaveFile_list.find(key);
		if (i == m_SaveFile_list.end())
		{
			return false;
		}
		*val = i->second;
		return true;
	}
	bool Insert(const std::string &src, const std::string &dst)//����/�����ļ���Ϣ
	{
		m_SaveFile_list[src] = dst;
		Save();
		return true;
	}
	bool Save()//�־û��洢�ļ�
	{
		std::stringstream tmp;
		auto i = m_SaveFile_list.begin();
		for (; i != m_SaveFile_list.end(); i++)
		{
			tmp << i->first << " " << i->second;
		}
		File::WriteFile(m_Save_file, tmp.str());
		return true;
	}
	bool InitLoad()//��ʼ�������ļ�����
	{
		std::string body;
		if (File::ReadFile(m_Save_file, &body) == false)
		{
			return false;
		}
		std::vector<std::string> list;
		boost::split(list, body, boost::is_any_of("\r\n"),boost::token_compress_off);
		for (auto i : list)
		{
			size_t pos = i.find(" ");
			if (pos == std::string::npos)
			{
				continue;
			}
			std::string key = i.substr(0, pos);
			std::string val = i.substr(pos + 1);
			Insert(key, val);
		}
		return true;
	}
};


class BU_Client : public FileData
{
	std::string m_listen_dir;//����Ŀ¼��ÿ�ζ������Ŀ¼������ļ���ʼ�����ļ�
	std::string m_srv_ip;
	uint16_t m_srv_port;
public:
	BU_Client(const std::string &pathname, const std::string &ip, uint16_t port) :
		FileData(STORE_FILE),
		m_listen_dir(pathname),
		m_srv_ip(ip),
		m_srv_port(port)
		{}
	bool Start()//�������������
	{
		FileData::InitLoad();
		while (1)
		{
			std::vector<std::string> list;
			GetList(&list);
			for (int i = 0; i < list.size(); i++)
			{
				std::string name = list[i];
				std::string pathname = m_listen_dir + name;

				std::cout << pathname << " is need to backup" << std::endl;
				std::string body;
				File::ReadFile(pathname, &body);
				httplib::Client client(m_srv_ip.c_str(), m_srv_port);
				std::string req_path = "/" + name;
				auto rsp = client.Put(req_path.c_str(), body, "application/octet-stream");
				if (rsp == NULL || (rsp != NULL && rsp->status != 200))
				{
					std::cout << pathname << " backup failed" << std::endl;
					continue;
				}
				std::string etag;
				GetNowETag(pathname, &etag);
				FileData::Insert(name, etag);
				std::cout << pathname << " backup success" << std::endl;
			}
			Sleep(1000);
		}
		return true;


	}
	bool GetList(std::vector<std::string> *list)//��ȡ��Ҫ���ݵ��ļ��б�
	{
		if (boost::filesystem::exists(m_listen_dir) == false)
		{
			boost::filesystem::create_directory(m_listen_dir);//��Ŀ¼�������򴴽�
		}
		boost::filesystem::directory_iterator begin(m_listen_dir);
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
			GetNowETag(pathname, &cur_etag);
			std::string old_etag;
			FileData::GetETag(name, &old_etag);
			if (cur_etag != old_etag)
			{
				list->push_back(name);//��ǰetag��ԭ��etag��ͬ����Ҫ����
			}
		}
		return true;
	}
	bool GetNowETag(const std::string &pathname, std::string *etag)//�����ʱ��ETag��Ϣ����ԭ�еıȽϣ����жϳ��Ǹ��ļ���Ҫ����
	{
		int64_t fsize = boost::filesystem::file_size(pathname);
		time_t mtime = boost::filesystem::last_write_time(pathname);
		*etag = std::to_string(fsize) + "-" + std::to_string(mtime);
		return true;
	}
};