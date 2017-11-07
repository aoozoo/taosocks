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

    for(int i = 0; i < 1; i++) {
        auto client = new ClientSocket(i, _disp);
        OnCreateClient(client);
        client->OnConnect([this](ClientSocket* c, bool connected) {
            LogLog("�����ӵ������");
            _clients.push_back(c);
            c->Read();
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
        // addr.S_un.S_addr = ::inet_addr("47.52.128.226");
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
    Schedule();
}

void ClientPacketManager::Schedule()
{
    if(_packets.empty()) {
        return;
    }

    assert(!_clients.empty());

    auto pkt = _packets.front();
    _packets.pop_front();

    auto index = std::rand() % _clients.size();
    auto client = _clients[index];

    client->Write((char*)pkt, pkt->__size, nullptr);
    LogLog("�������ݰ�(%d)��sid=%d, cid=%d, seq=%d, cmd=%d, size=%d", client->GetId(), pkt->__sid, pkt->__cid, pkt->__seq, pkt->__cmd, pkt->__size);
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

    client->Read();
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
}

void ServerPacketManager::Send(BasePacket* pkt)
{
    assert(pkt != nullptr);
    pkt->__seq = ++_seq;
    _packets.push_back(pkt);
    Schedule();
}


void ServerPacketManager::AddClient(ClientSocket* client)
{
    client->Read();

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

void ServerPacketManager::Schedule()
{
    if(_packets.empty()) {
        return;
    }

    assert(!_clients.empty());

    auto pkt = _packets.front();
    _packets.pop_front();

    auto range = _clients.equal_range(pkt->__guid);
    assert(range.first != range.second);

    auto index = std::rand() % _clients.count(pkt->__guid);
    auto client = _clients[pkt->__guid][index];

    client->Write((char*)pkt, pkt->__size, nullptr);
    LogLog("�������ݰ�(%d)��sid=%d, cid=%d, seq=%d, cmd=%d, size=%d", client->GetId(), pkt->__sid, pkt->__cid, pkt->__seq, pkt->__cmd, pkt->__size);
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

    client->Read();
}

}