#include "client_socket.h"

#include "log.h"

namespace taosocks {
void ClientSocket::Close(bool force)
{
    if(!_flags.test(Flags::Closed)) {
        if(!force && (_nPendingWrite > 0)) {
            LogWrn("�й����д���������������رգ��ж�����%d", _flags.test(Flags::PendingRead));
            _flags.set(Flags::MarkClose);
        }
        else {
            _flags.set(Flags::Closed);
            _flags.clear(Flags::MarkClose, Flags::PendingRead);
            WSAIntRet ret = closesocket(_fd);
            _fd = INVALID_SOCKET;
            LogLog("�ر�client, ret=%d", ret.Code());
            assert(ret.Succ());
            if(_onClose) {
                _onClose(this, _close_reason);
            }
        }
    }
    else {
        LogWrn("���ѹر�");
    }
}
void ClientSocket::OnRead(OnReadT onRead)
{
    _onRead = onRead;
}
void ClientSocket::OnWrite(OnWriteT onWrite)
{
    _onWrite = onWrite;
}
void ClientSocket::OnClose(OnCloseT onClose)
{
    _onClose = onClose;
}
void ClientSocket::OnConnect(OnConnectT onConnect)
{
    _onConnect = onConnect;
}
WSARet ClientSocket::Connect(in_addr& addr, unsigned short port)
{
    assert(_fd == INVALID_SOCKET);

    CreateSocket();

    sockaddr_in sai = {0};
    sai.sin_family = PF_INET;
    sai.sin_addr.S_un.S_addr = INADDR_ANY;
    sai.sin_port = 0;
    WSAIntRet r = ::bind(_fd, (sockaddr*)&sai, sizeof(sai));
    assert(r.Succ());
    sai.sin_addr = addr;
    sai.sin_port = ::htons(port);

    auto connio = new ConnectIOContext();
    auto ret = connio->Connect(_fd, sai);
    if(ret.Succ()) {
        // LogLog("���������ɹ�");
    }
    else if(ret.Fail()) {
        LogFat("���ӵ���ʧ�ܣ�%d", ret.Code());
    }
    else if(ret.Async()) {
        // LogLog("�����첽");
    }
    return ret;
}

WSARet ClientSocket::Write(const void* data, size_t size)
{
    _nPendingWrite++;

    auto writeio = new WriteIOContext();
    auto ret = writeio->Write(_fd, (const unsigned char*)data, size);
    if(ret.Succ()) {
        // DWORD dwBytes;
        // auto r = writeio->GetResult(_fd, &dwBytes);
        // assert(r && dwBytes == size);
        // LogLog("д�����ɹ���fd=%d,size=%d", _fd, size);
    }
    else if(ret.Fail()) {
        _nPendingWrite--;

        if(!_flags.test(Flags::MarkClose)) {
            LogFat("д����code=%d", ret.Code());
        }
        _flags.set(Flags::MarkClose);
        _CloseIfNeeded();
    }
    else if(ret.Async()) {
        // LogLog("д�첽��fd=%d", _fd);
    }
    return ret;
}
WSARet ClientSocket::Read()
{
    assert(!IsClosed());
    if(_flags.test(Flags::PendingRead)) {
        LogFat("���ܶ�ζ���");
        return WSABoolRet(FALSE);
    }

    _flags.set(Flags::PendingRead);

    auto readio = new ReadIOContext();
    auto ret = readio->Read(_fd);
    if(ret.Succ()) {
        // LogLog("_Read �����ɹ�, fd:%d", _fd);
    }
    else if(ret.Fail()) {
        if(!_flags.test(Flags::MarkClose)) {
            LogFat("������code=%d", ret.Code());
        }
        _flags.clear(Flags::PendingRead);
        _flags.set(Flags::MarkClose);
        _CloseIfNeeded();
    }
    else if(ret.Async()) {
        // LogLog("���첽 fd:%d", _fd);
    }
    return ret;
}

void ClientSocket::_CloseIfNeeded()
{
    if(
        (_flags.test(Flags::MarkClose))     // �ѱ����Ϊ�ر�
        && _nPendingWrite <= 0              // д����
        && !_flags.test(Flags::PendingRead) // ������
        && (!IsClosed())                    // ��δ�ر�
    )
    {
        Close(true);
    }
}

void ClientSocket::_OnRead(ReadIOContext* rio)
{
    _flags.clear(Flags::PendingRead);
    DWORD dwBytes = 0;
    WSARet ret = rio->GetResult(_fd, &dwBytes);
    if(ret.Succ() && dwBytes > 0) {
        _onRead(this, rio->buf, dwBytes);
    }
    else {
        if(_flags.test(Flags::Closed)) {
            LogWrn("�������ر�����");
            _close_reason = CloseReason::Actively;
        }
        else if(ret.Succ() && dwBytes == 0) {
            LogWrn("�ѱ����ر�����");
            _close_reason = CloseReason::Passively;
        }
        else if(ret.Fail()) {
            if(!_flags.test(Flags::MarkClose)) {
                LogFat("��ʧ�ܣ�code:%d", ret.Code());
            }
            _close_reason = CloseReason::Reset;
        }

        _flags.set(Flags::MarkClose);
        _CloseIfNeeded();
    }
    delete rio;
}

void ClientSocket::_OnWrite(WriteIOContext* io)
{
    _nPendingWrite--;

    DWORD dwBytes = 0;
    WSARet ret = io->GetResult(_fd, &dwBytes);
    if(ret.Succ() && dwBytes > 0) {
        assert(dwBytes == io->wsabuf.len);
        _onWrite(this, dwBytes);
    }
    else {
        if(!_flags.test(Flags::MarkClose)) {
            LogFat("дʧ�� code=%d", ret.Code());
        }
        _flags.set(Flags::MarkClose);
    }
    _CloseIfNeeded();
    delete io;
}

void ClientSocket::_OnConnect(ConnectIOContext* io)
{
    WSARet ret = io->GetResult(_fd);
    delete io;
    auto connected = ret.Succ();
    _flags.clear(Flags::Closed, Flags::MarkClose, Flags::PendingRead);
    _onConnect(this, connected);
}

void ClientSocket::OnTask(BaseIOContext* bio)
{
    switch(bio->optype) {
    case OpType::Read:
    {
        auto rio = static_cast<ReadIOContext*>(bio);
        if(_onRead == nullptr) {
            _read_queue.push_back(rio);
        }
        else {
            _OnRead(rio);
        }
        break;
    }
    case OpType::Write:
    {
        _OnWrite(static_cast<WriteIOContext*>(bio));
        break;
    }
    case OpType::Connect:
    {
        _OnConnect(static_cast<ConnectIOContext*>(bio));
        break;
    }
    }
}

}
