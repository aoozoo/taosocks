#include <iostream>

#include <ctime>
#include <process.h>

#include "packet_manager.h"
#include "log.h"


namespace taosocks {
ClientPacketManager::ClientPacketManager(Dispatcher & disp)
    : _disp(disp)
    , _seq(0)
{
    LogLog("�����ͻ��˰�������");
}

void ClientPacketManager::StartActive()
{
    ::UuidCreate(&_guid);

    LogLog("������");

    ::_beginthreadex(nullptr, 0, __ThreadProc, this, 0, nullptr);

    for(int i = 0; i < 10; i++) {
        auto client = new ClientSocket(i, _disp);
        OnCreateClient(client);
        client->OnConnect([this](ClientSocket* c, bool connected) {
            LogLog("�����ӵ������");
            _clients.push_back(c);
        });

        client->OnRead([this](ClientSocket* c, unsigned char* data, size_t size) {
            return OnRead(c, data, size);
        });

        client->OnWrite([](ClientSocket* c, size_t size) {
            // LogLog("�������� size=%d", size);
        });

        client->OnClose([this](ClientSocket* c, CloseReason::Value reason) {
            LogLog("�����˶Ͽ�����");
        });

        in_addr addr;
        addr.S_un.S_addr = ::inet_addr("127.0.0.1");
        client->Connect(addr, 8081);
    }
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
    auto& recv_data = _recv_data[client];
    recv_data.append(data, size);

    for(BasePacket* bpkt; (bpkt = recv_data.try_cast<BasePacket>()) != nullptr && (int)recv_data.size() >= bpkt->__size;) {
        LogLog("�յ����ݰ�(%d) sid=%d, cid=%d, seq=%d, cmd=%d, size=%d", client->GetId(), bpkt->__sid, bpkt->__cid, bpkt->__seq, bpkt->__cmd, bpkt->__size);
        auto pkt = new (new char[bpkt->__size]) BasePacket;
        recv_data.get(pkt, bpkt->__size);
        auto handler = _handlers.find(pkt->__cid);
        if(handler == _handlers.cend()) {
            LogWrn("��û�д�������������");
        }
        else {
            handler->second->OnPacket(pkt);
        }
    }
}

unsigned int ClientPacketManager::PacketThread()
{
    std::srand(std::time(nullptr));

    for(;;) {
        BasePacket* pkt =nullptr;
        ClientSocket* client = nullptr;

        _lock.LockExecute([&] {
            if(!_clients.empty() && !_packets.empty()) {
                pkt = _packets.front();
                _packets.pop_front();
                client = _clients[std::rand() % _clients.size()];
            }
        });

        if(pkt != nullptr) {
            LogLog("�������ݰ�(%d) sid=%d, cid=%d, seq=%d, cmd=%d, size=%d", client->GetId(), pkt->__sid, pkt->__cid, pkt->__seq, pkt->__cmd, pkt->__size);
            client->Write((char*)pkt, pkt->__size, nullptr);
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

void ServerPacketManager::CloseLocal(const GUID & guid, int cid)
{
}

void ServerPacketManager::OnRead(ClientSocket* client, unsigned char* data, size_t size)
{
    auto& recv_data = _recv_data[client];
    recv_data.append(data, size);

    for(BasePacket* bpkt; (bpkt = recv_data.try_cast<BasePacket>()) != nullptr && (int)recv_data.size() >= bpkt->__size;) {
        if(bpkt->__cmd == PacketCommand::Connect) {
            _clients[bpkt->__guid].push_back(client);
        }

        LogLog("�յ����ݰ�(%d)��sid=%d, cid=%d, seq=%d, cmd=%d, size=%d", client->GetId(), bpkt->__sid, bpkt->__cid, bpkt->__seq, bpkt->__cmd, bpkt->__size);

        auto pkt = new (new char[bpkt->__size]) BasePacket;
        recv_data.get(pkt, bpkt->__size);
        auto handler = _handlers.find(pkt->__sid);
        if(handler == _handlers.cend()) {
            LogWrn("��û�д�������������");
        }
        else {
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

            auto index = std::rand() % _clients.count(pkt->__guid);
            auto client = _clients[pkt->__guid][index];

            client->Write((char*)pkt, pkt->__size, nullptr);
            LogLog("�������ݰ�(%d)��sid=%d, cid=%d, seq=%d, cmd=%d, size=%d", client->GetId(), pkt->__sid, pkt->__cid, pkt->__seq, pkt->__cmd, pkt->__size);
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