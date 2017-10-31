#include <iostream>

#include <process.h>

#include "packet_manager.h"
#include "log.h"


namespace taosocks {
ClientPacketManager::ClientPacketManager(Dispatcher & disp)
    : _disp(disp)
    , _client(disp)
    , _connected(false)
    , _seq(0)
{
    LogLog("�����ͻ��˰���������fd=%d\n", _client.GetDescriptor());
}

void ClientPacketManager::StartActive()
{
    ::UuidCreate(&_guid);

    LogLog("������");

    ::_beginthreadex(nullptr, 0, __ThreadProc, this, 0, nullptr);

    _client.OnConnect([this](ClientSocket*, bool connected) {
        LogLog("�����ӵ������");
        _connected = true;
    });

    _client.OnRead([this](ClientSocket* client, unsigned char* data, size_t size) {
        // LogLog("���յ����� size=%d", size);
        return OnRead(client, data, size);
    });

    _client.OnWrite([](ClientSocket*, size_t size) {
        // LogLog("�������� size=%d", size);
    });

    _client.OnClose([this](ClientSocket*, CloseReason::Value reason){
        LogLog("�����˶Ͽ�����");
    });

    in_addr addr;
    addr.S_un.S_addr = ::inet_addr("127.0.0.1");
    _client.Connect(addr, 8081);
}

void ClientPacketManager::Send(BasePacket* pkt)
{
    assert(pkt != nullptr);
    std::memcpy(&pkt->__guid, &_guid, sizeof(GUID));
    pkt->__seq = ++_seq;
    _packets.push_back(pkt);
    // LogLog("���һ�����ݰ�");
}

void ClientPacketManager::OnRead(ClientSocket* client, unsigned char* data, size_t size)
{
    auto& recv_data = _recv_data;
    recv_data.append(data, size);

    for(BasePacket* bpkt; (bpkt = recv_data.try_cast<BasePacket>()) != nullptr && (int)recv_data.size() >= bpkt->__size;) {
        LogLog("�յ����ݰ� seq=%d, cmd=%d, size=%d", bpkt->__seq, bpkt->__cmd, bpkt->__size);
        auto pkt = new (new char[bpkt->__size]) BasePacket;
        recv_data.get(pkt, bpkt->__size);
        auto handler = _handlers.find(pkt->__cfd);
        assert(handler != _handlers.cend());
        handler->second->OnPacket(pkt);
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
            LogLog("�������ݰ�, seq=%d, cmd=%d, size=%d", pkt->__seq, pkt->__cmd, pkt->__size);
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
    , _seq(0)
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
    pkt->__seq = ++_seq;
    _lock.LockExecute([&] {
        // LogLog("���һ�����ݰ�");
        _packets.push_back(pkt);
    });
}

void ServerPacketManager::AddClient(ClientSocket* client)
{
    client->OnRead([this](ClientSocket* client, unsigned char* data, size_t size) {
        return OnRead(client, data, size);
    });

    client->OnWrite([](ClientSocket* client, size_t size) {
    });

    // �����ǵ��ߡ������������ر�
    // ���߲�������վ���ӣ������ر�Ҫ����
    client->OnClose([this](ClientSocket*, CloseReason::Value reason) {
        if(reason == CloseReason::Actively) {
            LogLog("�����ر����ӣ���վ�ȶϿ�");
            // ������վ�Ͽ������Ƕ�ȥ����
        }
        else if(reason == CloseReason::Passively) {
            LogLog("�����ر�����");

        }
        else if(reason == CloseReason::Reset) {
            LogLog("���ӱ�����");
        }
        else {
            LogWrn("Bad Reason");
        }
    });
}

void ServerPacketManager::RemoveClient(ClientSocket* client)
{

}

void ServerPacketManager::CloseLocal(const GUID & guid, int cfd)
{
}

void ServerPacketManager::OnRead(ClientSocket* client, unsigned char* data, size_t size)
{
    auto& recv_data = _recv_data[client];
    recv_data.append(data, size);

    for(BasePacket* bpkt; (bpkt = recv_data.try_cast<BasePacket>()) != nullptr && (int)recv_data.size() >= bpkt->__size;) {
        if(bpkt->__cmd == PacketCommand::Connect) {
            AddClient(client);
            _clients.emplace(bpkt->__guid, client);
        }

        LogLog("�յ����ݰ�������fd=%d, seq=%d, cmd=%d, size=%d", client->GetDescriptor(), bpkt->__seq, bpkt->__cmd, bpkt->__size);

        auto pkt = new (new char[bpkt->__size]) BasePacket;
        recv_data.get(pkt, bpkt->__size);
        auto handler = _handlers.find(pkt->__sfd);
        if(pkt->__cmd == PacketCommand::Disconnect && handler == _handlers.cend()) {
            LogLog("�յ��Ͽ����Ӱ���������վ���ѶϿ�����");
        }
        else {
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
                LogLog("�������ݰ���fd=%d, seq=%d, cmd=%d, size=%d", client->GetDescriptor(), pkt->__seq, pkt->__cmd, pkt->__size);
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