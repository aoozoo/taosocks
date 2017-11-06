#include "client_socket.h"

#include "log.h"

namespace taosocks {
void ClientSocket::Close()
{
    if(!(_flags & Flags::Closed)) {
        _flags |= Flags::Closed;
        WSAIntRet ret = closesocket(_fd);
        LogLog("�ر�client, id=%d,ret=%d", GetId(), ret.Code());
        assert(ret.Succ());
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
WSARet ClientSocket::Write(const char * data, size_t size, void * tag)
{
    return Write((const unsigned char*)data, size, tag);
}
WSARet ClientSocket::Write(const char * data, void * tag)
{
    return Write(data, std::strlen(data), tag);
}
WSARet ClientSocket::Write(const unsigned char * data, size_t size, void * tag)
{
    auto writeio = new WriteIOContext();
    auto ret = writeio->Write(_fd, data, size);
    if(ret.Succ()) {
        // DWORD dwBytes;
        // auto r = writeio->GetResult(_fd, &dwBytes);
        // assert(r && dwBytes == size);
        // LogLog("д�����ɹ���fd=%d,size=%d", _fd, size);
    }
    else if(ret.Fail()) {
        LogFat("д����id=%d,code=%d", GetId(), ret.Code());
    }
    else if(ret.Async()) {
        // LogLog("д�첽��fd=%d", _fd);
    }
    return ret;
}
WSARet ClientSocket::Read()
{
    auto readio = new ReadIOContext();
    auto ret = readio->Read(_fd);
    if(ret.Succ()) {
        // LogLog("_Read �����ɹ�, fd:%d", _fd);
    }
    else if(ret.Fail()) {
        LogFat("������id=%d,code=%d", GetId(), ret.Code());
    }
    else if(ret.Async()) {
        // LogLog("���첽 fd:%d", _fd);
    }
    return ret;
}
void ClientSocket::_OnRead(ReadIOContext* rio)
{
    DWORD dwBytes = 0;
    WSARet ret = rio->GetResult(_fd, &dwBytes);
    if(ret.Succ() && dwBytes > 0) {

    }
    else {
        if(_flags & Flags::Closed) {
            LogWrn("�������ر����ӣ�id=%d", GetId());
            data->reason = CloseReason::Actively;
            Dispatch(data);
        }
        else if(ret.Succ() && dwBytes == 0) {
            LogWrn("�ѱ����ر����ӣ�id:%d", GetId());
            data->reason = CloseReason::Passively;
            Dispatch(data);
        }
        else if(ret.Fail()) {
            LogFat("��ʧ�ܣ�id=%d,code:%d", GetId(), ret.Code());
            data->reason = CloseReason::Reset;
            Dispatch(data);
        }
    }
}

WSARet ClientSocket::_OnWrite(WriteIOContext* io)
{
    DWORD dwBytes = 0;
    WSARet ret = io.GetResult(_fd, &dwBytes);
    if(ret.Succ() && dwBytes > 0) {
        assert(dwBytes == io.wsabuf.len);
        WriteDispatchData* data = new WriteDispatchData;
        data->size = dwBytes;
        Dispatch(data);
    }
    else {
        LogFat("дʧ�� id=%d, code=%d", GetId(), ret.Code());
    }

    return ret;
}

WSARet ClientSocket::_OnConnect(ConnectIOContext* io)
{
    WSARet ret = io.GetResult(_fd);
    if(ret.Succ()) {
        ConnectDispatchData* data = new ConnectDispatchData;
        data->connected = true;
        Dispatch(data);
    }
    else {
        LogFat("����ʧ��");
        ConnectDispatchData* data = new ConnectDispatchData;
        data->connected = false;
        Dispatch(data);
    }

    return ret;
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
        auto wio = static_cast<WriteIOContext*>(bio);
        _OnWrite(wio);
        break;
    }
    case OpType::Close:
    {
        // �������߳�ǰ����û�ر�
        // �������Ͳ�һ����
        // �������ڴ���Զ�˹رյ�ʱ�������ر�
        // �������Ѿ��Ǳ��ر�״̬
        auto closed = IsClosed();
        auto d = static_cast<CloseDispatchData*>(data);
        if(d->reason != CloseReason::Actively) {
            if(!closed) {
                Close();
            }
        }
        // ����Ҳͬ��
        if(!closed) {
            _onClose(this, d->reason);
        }
        break;
    }
    case OpType::Connect:
    {
        auto cio = static_cast<ConnectIOContext*>(bio);
        _OnConnect(cio);
        break;
    }
    }
}

}
