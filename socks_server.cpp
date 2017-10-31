#include <algorithm>

#include "socks_server.h"

#include "log.h"

namespace taosocks {

SocksServer::SocksServer(ClientPacketManager& pktmgr, ClientSocket * client)
    : _client(client)
    , _pktmgr(pktmgr)
    , _phrase(Phrase::Init)
{
    assert(_client != nullptr);

    _pktmgr.AddHandler(this);

    _client->OnRead([this](ClientSocket*, unsigned char* data, size_t size) { return _OnClientRead(data, size); });
    _client->OnWrite([this](ClientSocket*, size_t) {});
    _client->OnClose([this](ClientSocket*, CloseReason::Value reason) {return _OnClientClose(reason); });
}
void SocksServer::feed(const unsigned char * data, size_t size)
{
    _recv.append(data, size);

    while(_recv.size() > 0) {
        switch(_phrase) {
        case Phrase::Init:
        {
            _ver = (SocksVersion::Value)_recv.get_byte();

            if(_ver != SocksVersion::v4) {
                throw "Bad socks version.";
            }

            _phrase = Phrase::Command;
            break;
        }
        case Phrase::Command:
        {
            auto cmd = (SocksCommand::Value)_recv.get_byte();

            if(cmd != SocksCommand::Stream) {
                throw "Not supported socks command.";
            }

            _phrase = Phrase::Port;
            break;
        }
        case Phrase::Port:
        {
            if(_recv.size() < 2) {
                return;
            }

            _port= ::ntohs(_recv.get_short());

            _phrase = Phrase::Addr;
            break;
        }
        case Phrase::Addr:
        {
            if(_recv.size() < 4) {
                return;
            }

            _addr.s_addr = _recv.get_int();

            _phrase = Phrase::User;
            break;
        }
        case Phrase::User:
        {
            auto index = _recv.index_char('\0');

            if(index == -1) {
                return;
            }

            _recv.get_string(index + 1);

            _is_v4a = _addr.S_un.S_un_b.s_b1 == 0
                && _addr.S_un.S_un_b.s_b2 == 0
                && _addr.S_un.S_un_b.s_b3 == 0
                && _addr.S_un.S_un_b.s_b4 != 0;

            _phrase = _is_v4a ? Phrase::Domain : Phrase::Finish;

            break;
        }
        case Phrase::Domain:
        {
            auto index = _recv.index_char('\0');

            if(index == -1) {
                return;
            }

            _domain = _recv.get_string(index + 1);

            _phrase = Phrase::Finish;

            break;
        }
        }
    }
}

void SocksServer::finish()
{
    auto p = new ConnectPacket;
    p->__cmd = PacketCommand::Connect;
    p->__size = sizeof(ConnectPacket);
    p->__sfd = (int)INVALID_SOCKET;
    p->__cfd = (int)_client->GetDescriptor();

    auto& host = _domain;
    auto service = std::to_string(_port);

    assert(host.size() > 0 && host.size() < _countof(p->host));
    assert(service.size() > 0 && service.size() < _countof(p->service));

    std::strcpy(p->host, host.c_str());
    std::strcpy(p->service, service.c_str());

    _pktmgr.Send(p);
}

void SocksServer::_OnClientClose(CloseReason::Value reason)
{
    if(reason = CloseReason::Actively) {
        LogLog("�����ر�����");
    }
    else if(reason == CloseReason::Passively) {
        LogLog("������Ͽ�������");
    }
    else if(reason == CloseReason::Reset) {
        LogLog("������쳣�Ͽ�");
    }
}

void SocksServer::_OnClientRead(unsigned char * data, size_t size)
{
    try {
        feed(data, size);
    }
    catch(const std::string& e) {

    }

    if(_phrase == Phrase::Finish) {
        assert(_recv.size() == 0);
        LogLog("�������");
        finish();
    }
}

void SocksServer::OnPacket(BasePacket* packet)
{
    if(packet->__cmd == PacketCommand::Connect) {
        OnConnectPacket(static_cast<ConnectRespondPacket*>(packet));
    }
    else {
        assert(0 && "invalid packet");
    }
}

void SocksServer::OnConnectPacket(ConnectRespondPacket* pkt)
{
    DataWindow data;
    data.append(0x00);
    data.append(pkt->code == 0 ? ConnectionStatus::Success : ConnectionStatus::Fail);

    if(_is_v4a) {
        data.append(_port >> 8);
        data.append(_port & 0xff);

        auto addr = _addr.S_un.S_addr;
        char* a = (char*)&addr;
        data.append(a[0]);
        data.append(a[1]);
        data.append(a[2]);
        data.append(a[3]);
    }
    else {
        data.append(0);
        data.append(0);
        data.append(0);
        data.append(0);
        data.append(0);
        data.append(0);
    }

    auto ret = _client->Write((char*)data.data(), data.size(), nullptr);
    LogLog("SocksӦ��״̬��%d,%d", ret.Succ(), ret.Code());
    assert(ret.Succ());

    if(pkt->code == 0) {

        ConnectionInfo info;
        info.sfd = pkt->__sfd;
        info.cfd = pkt->__cfd;
        info.addr = pkt->addr;
        info.port = pkt->port;
        info.client = _client;
        assert(OnSucceed);
        _pktmgr.RemoveHandler(this);
        OnSucceed(info);
    }
    else {
        // Ӧ�õȵ�socksӦ������д���ٹر�
        // �������д�ɹ�����ô�Է��������ر�
        // Read �ᱨ�汻���ر�
        _client->Close();
        _pktmgr.RemoveHandler(this);
        assert(OnError);
        OnError("����ʧ��");
    }
}

}

