#include "relay_client.h"
#include "packet_manager.h"

#include "log.h"

namespace taosocks {

ClientRelayClient::ClientRelayClient(IBasePacketManager* pktmgr, ClientSocket* client, int sfd)
    : _pktmgr(pktmgr)
    , _client(client)
    , _sfd(sfd)
{
    _pktmgr->AddHandler(this);

    _client->OnRead([this](ClientSocket*, unsigned char* data, size_t size) {
        LogLog("��ȡ�� %d �ֽ�", size);
        auto p = RelayPacket::Create(_sfd, _client->GetDescriptor(), data, size);
        _pktmgr->Send(p);
    });

    _client->OnWritten([this](ClientSocket*, size_t size) {
        LogLog("д���� %d �ֽ�", size);
    });

    _client->OnClosed([this](ClientSocket*) {
        LogLog("���������վ�Ͽ�����");
    });
}

int ClientRelayClient::GetDescriptor()
{
    return _client->GetDescriptor();
}

void ClientRelayClient::OnPacket(BasePacket * packet)
{
    if(packet->__cmd == PacketCommand::Relay) {
        auto pkt = static_cast<RelayPacket*>(packet);
        _client->Write(pkt->data, pkt->__size - sizeof(BasePacket), nullptr);
    }
}

//////////////////////////////////////////////////////////////////////////

ServerRelayClient::ServerRelayClient(IBasePacketManager* pktmgr, ClientSocket* client, int cfd, GUID guid)
    : _pktmgr(pktmgr)
    , _client(client)
    , _cfd(cfd)
    , _guid(guid)
{
    _client->OnRead([this](ClientSocket*, unsigned char* data, size_t size) {
        LogLog("��ȡ�� %d �ֽ�", size);
        auto p = RelayPacket::Create(_client->GetDescriptor(), _cfd, data, size);
        p->__guid = _guid;
        _pktmgr->Send(p);
    });

    _client->OnWritten([this](ClientSocket*, size_t size) {
        LogLog("д���� %d �ֽ�", size);
    });

    _client->OnClosed([this](ClientSocket*) {
        LogLog("���������վ�Ͽ�����");
    });
}

int ServerRelayClient::GetDescriptor()
{
    return _client->GetDescriptor();
}

void ServerRelayClient::OnPacket(BasePacket * packet)
{
    if(packet->__cmd == PacketCommand::Relay) {
        auto pkt = static_cast<RelayPacket*>(packet);
        _client->Write(pkt->data, pkt->__size - sizeof(BasePacket), nullptr);
    }
}

int NewRelayHandler::GetDescriptor()
{
    return (int)INVALID_SOCKET;
}

void NewRelayHandler::OnPacket(BasePacket * packet)
{
    if(packet->__cmd == PacketCommand::ResolveAndConnect) {
        auto pkt = static_cast<ResolveAndConnectPacket*>(packet);
        resolver rsv;
        rsv.resolve(pkt->host, pkt->service);
        assert(rsv.size() > 0);

        auto c = OnCreateClient();
        auto ad = rsv[0];
        auto pt = std::atoi(pkt->service);

        c->OnConnected([&,c, pkt, ad, pt](ClientSocket*) {
            auto p = new ResolveAndConnectRespondPacket;
            p->__size = sizeof(ResolveAndConnectRespondPacket);
            p->__cmd = PacketCommand::ResolveAndConnectRespond;
            p->__sfd = c->GetDescriptor();
            p->__cfd = pkt->__cfd;
            p->__guid = pkt->__guid;
            p->addr = ad;
            p->port = pt;
            p->status = true;
            _pktmgr->Send(p);
            assert(OnSucceeded);
            OnSucceeded(c, pkt->__cfd, pkt->__guid);
        });

        in_addr addr;
        addr.S_un.S_addr = rsv[0];
        c->Connect(addr, pt);
    }
}

}

