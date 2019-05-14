#include "Connection.h"
#include "ConnectPacket.h"
#include "ConnectionSocket.h"
#include "kcpTypedef.h"
#include <iostream>
#include "ikcp.h"

namespace AsioKCP 
{
	Connection::Connection(const std::weak_ptr<ConnectionSocket>& manager_ptr)
		:Manager(manager_ptr)
	{
		StartClock = GetClock();
	}

	Connection::~Connection()
	{
		Clean();
	}

	std::shared_ptr<Connection> Connection::Create(const std::weak_ptr<ConnectionSocket>& manager_ptr, const uint32_t & conv, const asio::ip::udp::endpoint & udp_remote_endpoint)
	{
		std::shared_ptr<Connection> ptr = std::make_shared<Connection>(manager_ptr);
		if (ptr)
		{
			ptr->Init(conv);
			ptr->SetUdpRemoteEndpoint(udp_remote_endpoint);
		}
		return ptr;
	}

	void Connection::SetUdpRemoteEndpoint(const asio::ip::udp::endpoint & udp_remote_endpoint)
	{
		RemoteEndpoint = udp_remote_endpoint;
	}

	void Connection::Update(int64_t clock)
	{
		ikcp_update(Kcp, static_cast<uint32_t>(clock - StartClock));
	}

	void Connection::Input(char * udp_data, size_t bytes_recvd, const asio::ip::udp::endpoint & udp_remote_endpoint)
	{
		LastPacketRecvTime = GetClock();
		RemoteEndpoint = udp_remote_endpoint;
		
		ikcp_input(Kcp, udp_data, bytes_recvd);
		{
			int32_t checkSize = ikcp_peeksize(Kcp);
			if (checkSize > 0)
			{
				if (auto ptr = Manager.lock())
				{
					std::shared_ptr<std::string> package = std::make_shared<std::string>(checkSize, 0);
					int kcp_recvd_bytes = ikcp_recv(Kcp, &((*package)[0]), checkSize);
					if (kcp_recvd_bytes > 0)
					{
						ptr->OnEvent(Conv, eEventType::eRcvMsg, package);
					}
				}
			}
			else
			{
				char buffer[1024] = { 0 };
				int size = ikcp_recv(Kcp, buffer, 1024);
				if (size > 0)
					std::cout << buffer << std::endl;
			}
		}
	}

	bool Connection::IsTimeout() const
	{
		if (LastPacketRecvTime == 0)
			return false;
		int64_t time = GetClock() - LastPacketRecvTime;
		return GetClock() - LastPacketRecvTime > GetTimeoutTime();
	}

	void Connection::DoTimeout()
	{
		if (auto ptr = Manager.lock())
		{
			std::shared_ptr<std::string> msg(new std::string("timeout"));
			ptr->OnEvent(Conv, eEventType::eTimeout, msg);
		}
	}

	void Connection::SendMsg(const std::string & msg)
	{
		int send_ret = ikcp_send(Kcp, msg.data(), msg.size());
		if (send_ret < 0)
		{
			std::cout << "send_ret<0: " << send_ret << std::endl;
		}
	}

	void Connection::Init(const uint32_t & conv)
	{
		Conv = conv;
		Kcp = ikcp_create(conv, (void*)this);
		Kcp->output = &Connection::UdpOutput;
		// ��������ģʽ
		// �ڶ������� nodelay-�����Ժ����ɳ�����ٽ�����
		// ���������� intervalΪ�ڲ�����ʱ�ӣ�Ĭ������Ϊ 10ms
		// ���ĸ����� resendΪ�����ش�ָ�꣬����Ϊ2
		// ��������� Ϊ�Ƿ���ó������أ������ֹ
		//ikcp_nodelay(p_kcp_, 1, 10, 2, 1);
		ikcp_nodelay(Kcp, 1, 5, 1, 1); // ���ó�1��ACK��Խֱ���ش�, ������Ӧ�ٶȻ����. �ڲ�ʱ��5����.
	}

	void Connection::Clean()
	{
		std::string disconnect_msg = AsioKCP::making_disconnect_packet(Conv);
		SendPackage(disconnect_msg.c_str(), disconnect_msg.size());
		ikcp_release(Kcp);
		Kcp = nullptr;
		Conv = 0;
	}

	int Connection::UdpOutput(const char * buf, int len, ikcpcb * kcp, void * user)
	{
		((Connection*)user)->SendPackage(buf, len);
		return 0;
	}

	void Connection::SendPackage(const char * buf, int len)
	{
		if (auto ptr = Manager.lock())
		{
			ptr->SendPackage(buf, len, RemoteEndpoint);
		}
	}

	uint64_t Connection::GetClock() const
	{
		if (auto ptr = Manager.lock())
		{
			return ptr->GetClock();
		}
		return 0;
	}

	uint32_t Connection::GetTimeoutTime() const
	{
		return 10000;
	}
}


