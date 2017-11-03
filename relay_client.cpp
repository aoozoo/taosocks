#include "relay_client.h"
#include "packet_manager.h"

#include "log.h"

namespace taosocks {

ClientRelayClient::ClientRelayClient(ClientPacketManager* pktmgr, ClientSocket* local, int sid)
    : _pktmgr(pktmgr)
    , _local(local)
    , _sid(sid)
{
    _pktmgr->AddHandler(this);

    _local->OnRead([this](ClientSocket*, unsigned char* data, size_t size) {
        // LogLog("��ȡ�� %d �ֽ�", size);
        auto p = RelayPacket::Create(_sid, _local->GetId(), data, size);
        _pktmgr->Send(p);
    });

    _local->OnWrite([this](ClientSocket*, size_t size) {
        // LogLog("д���� %d �ֽ�", size);
    });

    _local->OnClose([this](ClientSocket*, CloseReason::Value reason) {
        LogLog("������Ͽ����ӣ����ɣ�%d", reason);
        if(reason == CloseReason::Actively) {

        }
        else if(reason == CloseReason::Passively || reason == CloseReason::Reset) {
            _pktmgr->RemoveHandler(this);
            auto pkt = new DisconnectPacket;
            pkt->__size = sizeof(DisconnectPacket);
            pkt->__cmd = PacketCommand::Disconnect;
            pkt->__sid = _sid;
            pkt->__cid = -1;
            _pktmgr->Send(pkt);
        }
    });
}

// ��վ�����ر�����
void ClientRelayClient::_OnRemoteDisconnect(DisconnectPacket * pkt)
{
    LogLog("�հ�����վ�Ͽ����ӣ������id=%d���������ǰ״̬��%s", _local->GetId(), _local->IsClosed() ? "�ѶϿ�" : "δ�Ͽ�");
    if(!_local->IsClosed()) {
        _local->Close();
        _pktmgr->RemoveHandler(this);
    }
    else {
        LogLog("�ѹر�");
    }
}

int ClientRelayClient::GetId()
{
    return _local->GetId();
}

void ClientRelayClient::OnPacket(BasePacket * packet)
{
    if(packet->__cmd == PacketCommand::Relay) {
        auto pkt = static_cast<RelayPacket*>(packet);
        _local->Write(pkt->data, pkt->__size - sizeof(BasePacket), nullptr);
    }
    else if(packet->__cmd == PacketCommand::Disconnect) {
        auto pkt = static_cast<DisconnectPacket*>(packet);
        _OnRemoteDisconnect(pkt);
    }
}

//////////////////////////////////////////////////////////////////////////

ServerRelayClient::ServerRelayClient(ServerPacketManager* pktmgr, ClientSocket* remote, int cid, GUID guid)
    : _pktmgr(pktmgr)
    , _remote(remote)
    , _cid(cid)
    , _guid(guid)
{
    _remote->OnRead([this](ClientSocket*, unsigned char* data, size_t size) {
        // LogLog("��ȡ�� %d �ֽ�", size);
        auto p = RelayPacket::Create(_remote->GetId(), _cid, data, size);
        p->__guid = _guid;
        _pktmgr->Send(p);
    });

    _remote->OnWrite([this](ClientSocket*, size_t size) {
        // LogLog("д���� %d �ֽ�", size);
    });

    _remote->OnClose([this](ClientSocket*, CloseReason::Value reason) {
        LogLog("��վ�Ͽ�����");
        _OnRemoteClose(reason);
    });
}

void ServerRelayClient::_OnRemoteClose(CloseReason::Value reason)
{
    if(reason == CloseReason::Actively) {
        LogLog("���������Ͽ�����");
    }
    else if(reason == CloseReason::Passively || reason == CloseReason::Reset) {
        LogLog("��վ�ر�/�쳣�Ͽ�����");
        _pktmgr->RemoveHandler(this);
        _pktmgr->CloseLocal(_guid, _cid);
        _remote->Close();

        auto pkt = new DisconnectPacket;
        pkt->__size = sizeof(DisconnectPacket);
        pkt->__cmd = PacketCommand::Disconnect;
        pkt->__guid = _guid;
        pkt->__cid = _cid;
        pkt->__sid = -1;
        _pktmgr->Send(pkt);
   }
}

int ServerRelayClient::GetId()
{
    return _remote->GetId();
}

void ServerRelayClient::OnPacket(BasePacket * packet)
{
    if(packet->__cmd == PacketCommand::Relay) {
        auto pkt = static_cast<RelayPacket*>(packet);
        _remote->Write(pkt->data, pkt->__size - sizeof(BasePacket), nullptr);
    }
    else if(packet->__cmd == PacketCommand::Disconnect) {
        LogLog("�հ���������Ͽ�����");
        _remote->Close();
        _pktmgr->RemoveHandler(this);
    }
}

void ConnectionHandler::_Respond(int code, int sid, int cid, GUID guid, unsigned int addr, unsigned short port)
{
    auto p = new ConnectRespondPacket;

    p->__size = sizeof(ConnectRespondPacket);
    p->__cmd = PacketCommand::Connect;
    p->__sid = sid;
    p->__cid = cid;
    p->__guid = guid;
    p->addr = addr;
    p->port = port;
    p->code = code;

    _pktmgr->Send(p);
}

void ConnectionHandler::_OnConnectPacket(ConnectPacket* pkt)
{
    assert(pkt->__sid == -1);

    resolver rsv;
    rsv.resolve(pkt->host, pkt->service);

    if(rsv.size() > 0) {
        unsigned int addr;
        unsigned short port;
        rsv.get(0, &addr, &port);
        _OnResolve(pkt->__cid, pkt->__guid, addr, port);
    }
    else {
        _Respond(1, GetId(), pkt->__cid, pkt->__guid, 0, 0);
    }
}

void ConnectionHandler::_OnResolve(int cid, GUID guid, unsigned int addr, unsigned short port)
{
    auto c = OnCreateClient();

    _contexts[c->GetId()] = {cid, guid};

    c->OnConnect([this, c, addr, port](ClientSocket*, bool connected) {
        auto ctx = _contexts[c->GetId()];
        _contexts.erase(c->GetId());

        if(connected) {
            _Respond(0, c->GetId(), ctx.cid, ctx.guid, addr, port);
            OnSucceed(c, ctx.cid, ctx.guid);
        }
        else {
            _Respond(1, GetId(), ctx.cid, ctx.guid, 0, 0);
            OnError(c);
        }
    });

    in_addr a;
    a.s_addr = addr;
    c->Connect(a, port);
}

int ConnectionHandler::GetId()
{
    return -1;
}

void ConnectionHandler::OnPacket(BasePacket* packet)
{
    if(packet->__cmd == PacketCommand::Connect) {
        _OnConnectPacket(static_cast<ConnectPacket*>(packet));
    }
    else {
        assert(0 && "invalid packet");
    }
}

}

