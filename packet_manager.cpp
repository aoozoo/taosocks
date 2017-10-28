#include <iostream>

#include <process.h>

#include "packet_manager.h"
#include "log.h"


namespace taosocks {
ClientPacketManager::ClientPacketManager(Dispatcher & disp)
    : _disp(disp)
    , _client(disp)
    , _connected(false)
{
    LogLog("�����ͻ��˰���������fd=%d\n", _client.GetDescriptor());
}

void ClientPacketManager::StartActive()
{
    ::UuidCreate(&_guid);

    LogLog("������");

    ::_beginthreadex(nullptr, 0, __ThreadProc, this, 0, nullptr);

    _client.OnConnected([this](ClientSocket*) {
        LogLog("�����ӵ������");
        _connected = true;
    });

    _client.OnRead([this](ClientSocket* client, unsigned char* data, size_t size) {
        LogLog("���յ����� size=%d", size);
        return OnRead(client, data, size);
    });

    _client.OnWritten([](ClientSocket*, size_t size) {
        LogLog("�������� size=%d", size);
    });

    _client.OnClosed([this](ClientSocket*){
        LogLog("�����ѹر�");
    });

    in_addr addr;
    addr.S_un.S_addr = ::inet_addr("127.0.0.1");
    _client.Connect(addr, 8081);
}

void ClientPacketManager::Send(BasePacket* pkt)
{
    assert(pkt != nullptr);
    std::memcpy(&pkt->__guid, &_guid, sizeof(GUID));
    _packets.push_back(pkt);
    LogLog("���һ�����ݰ�");
}

void ClientPacketManager::OnRead(ClientSocket* client, unsigned char* data, size_t size)
{
    auto& recv_data = _recv_data;

    recv_data.insert(recv_data.cend(), data, data + size);
    if(recv_data.size() >= sizeof(BasePacket)) {
        auto bpkt = (BasePacket*)recv_data.data();
        if((int)recv_data.size() >= bpkt->__size) {
            LogLog("���յ�һ�����ݰ� cmd=%d", bpkt->__cmd);
            auto pkt = new (new unsigned char[bpkt->__size]) BasePacket;
            std::memcpy(pkt, recv_data.data(), bpkt->__size);
            recv_data.erase(recv_data.cbegin(), recv_data.cbegin() + bpkt->__size);
            auto handler = _handlers.find(pkt->__cfd);
            assert(handler != _handlers.cend());
            handler->second->OnPacket(pkt);
        }
    }
}

unsigned int ClientPacketManager::PacketThread()
{
    for(;;) {
        BasePacket* pkt =nullptr;

        if(_connected) {
            _lock.LockExecute([&] {
                if(!_packets.empty()) {
                    pkt = _packets.front();
                    _packets.pop_front();
                }
            });

        }

        if(pkt != nullptr) {
            LogLog("����д��һ�����ݰ�, cmd=%d", pkt->__cmd);
            _client.Write((char*)pkt, pkt->__size, nullptr);
        }
        else {
            ::Sleep(500);
        }
    }

    return 0;
}

unsigned int ClientPacketManager::__ThreadProc(void * that)
{
    return static_cast<ClientPacketManager*>(that)->PacketThread();
}

//////////////////////////////////////////////////////////////////////////

ServerPacketManager::ServerPacketManager(Dispatcher & disp)
    : _disp(disp)
{
}

void ServerPacketManager::StartPassive()
{
    LogLog("������");
    ::_beginthreadex(nullptr, 0, __ThreadProc, this, 0, nullptr);
}

void ServerPacketManager::Send(BasePacket* pkt)
{
    assert(pkt != nullptr);
    _lock.LockExecute([&] {
        LogLog("���һ�����ݰ�");
        _packets.push_back(pkt);
    });
}

void ServerPacketManager::AddClient(ClientSocket* client)
{
    client->OnRead([this](ClientSocket* client, unsigned char* data, size_t size) {
        return OnRead(client, data, size);
    });

    client->OnWritten([](ClientSocket* client, size_t size) {
    });
}

void ServerPacketManager::RemoveClient(ClientSocket* client)
{

}

void ServerPacketManager::OnRead(ClientSocket* client, unsigned char* data, size_t size)
{
    auto& recv_data = _recv_data[client];

    recv_data.insert(recv_data.cend(), data, data + size);
    if(recv_data.size() >= sizeof(BasePacket)) {
        auto bpkt = (BasePacket*)recv_data.data();
        if(bpkt->__cmd == PacketCommand::ResolveAndConnect) {
            AddClient(client);
            _clients.emplace(bpkt->__guid, client);
        }
        if((int)recv_data.size() >= bpkt->__size) {
            LogLog("�յ�һ�����ݰ�������fd=%d, cmd=%d", client->GetDescriptor(), bpkt->__cmd);
            auto pkt = new (new unsigned char[bpkt->__size]) BasePacket;
            std::memcpy(pkt, recv_data.data(), bpkt->__size);
            recv_data.erase(recv_data.cbegin(), recv_data.cbegin() + bpkt->__size);
            auto handler = _handlers.find(pkt->__sfd);
            assert(handler != _handlers.cend());
            handler->second->OnPacket(pkt);
        }
    }
}

unsigned int ServerPacketManager::PacketThread()
{
    for(;;) {
        BasePacket* pkt =nullptr;

            _lock.LockExecute([&] {
                if(!_packets.empty()) {
                    pkt = _packets.front();
                    _packets.pop_front();
                }
            });

        if(pkt != nullptr) {
            auto range = _clients.equal_range(pkt->__guid);
            if(range.first == range.second) {
                assert(0 && "û�н��ն�");
            }
            for(auto it = range.first; it != range.second; ++it) {
                auto client = it->second;
                client->Write((char*)pkt, pkt->__size, nullptr);
                LogLog("����һ�����ݰ���fd=%d, cmd=%d", client->GetDescriptor(), pkt->__cmd);
                break;
            }
        }
        else {
            ::Sleep(500);
        }
    }

    return 0;
}

unsigned int ServerPacketManager::__ThreadProc(void * that)
{
    return static_cast<ServerPacketManager*>(that)->PacketThread();
}

}