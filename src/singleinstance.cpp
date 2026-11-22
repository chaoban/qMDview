#include "singleinstance.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

constexpr quint32 kMagic = 0x714D4456; // "qMDV"
constexpr int kTimeoutMs = 2000;

} // namespace

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{
}

SingleInstance::~SingleInstance()
{
    stop();
}

QString SingleInstance::serverName()
{
    QString exe = QCoreApplication::applicationFilePath();
#ifdef Q_OS_WIN
    exe = exe.toLower(); // Windows 路徑不分大小寫
#endif
    QString user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty())
        user = qEnvironmentVariable("USER");
    const QByteArray hash = QCryptographicHash::hash((exe + QLatin1Char('|') + user).toUtf8(),
                                                     QCryptographicHash::Sha1).toHex().left(16);
    return QStringLiteral("SAISAI.Dev.qMDView.") + QString::fromLatin1(hash);
}

SingleInstance::Result SingleInstance::start(const QStringList &paths, bool forward)
{
    if (m_server)
        return Result::Primary;

    const QString name = serverName();

    // 同時雙擊多個檔案時會幾乎同時啟動好幾個程式：用鎖檔讓「找人／自己當第一個」依序進行，
    // 否則可能兩個都以為自己是第一個。拿不到鎖（例如逾時）也照常進行，最壞情況是多開一個視窗。
    QLockFile lock(QDir(QDir::tempPath()).filePath(name + QStringLiteral(".lock")));
    lock.setStaleLockTime(10000);
    lock.tryLock(3000);

    QLocalSocket socket;
    socket.connectToServer(name);
    if (socket.waitForConnected(500)) {
        if (!forward)
            return Result::OtherRunning;
#ifdef Q_OS_WIN
        // 由使用者操作啟動的程式才有權把視窗帶到最前面：先把這個權限交給原本的 qMDView，
        // 它的視窗才能跳到前面，而不是只在工作列閃爍
        AllowSetForegroundWindow(ASFW_ANY);
#endif
        QByteArray payload;
        QDataStream out(&payload, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_2);
        out << kMagic << paths;
        socket.write(payload);
        if (socket.waitForBytesWritten(kTimeoutMs)) {
            // 對方讀完會中斷連線；等它讀完再結束，避免資料還沒送到就關閉
            if (socket.state() != QLocalSocket::UnconnectedState)
                socket.waitForDisconnected(kTimeoutMs);
            return Result::Forwarded;
        }
        // 原本的程式沒有回應（例如當掉）：自己以獨立視窗執行
        return Result::Failed;
    }

    QLocalServer::removeServer(name); // 清掉上次異常結束留下的（Unix）；Windows 上不做事
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(name)) {
        delete m_server;
        m_server = nullptr;
        return Result::Failed;
    }
    connect(m_server, &QLocalServer::newConnection, this, &SingleInstance::onNewConnection);
    return Result::Primary;
}

void SingleInstance::stop()
{
    if (!m_server)
        return;
    m_server->close();
    delete m_server;
    m_server = nullptr;
}

bool SingleInstance::isListening() const
{
    return m_server && m_server->isListening();
}

void SingleInstance::onNewConnection()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            QDataStream in(socket);
            in.setVersion(QDataStream::Qt_6_2);
            in.startTransaction();
            quint32 magic = 0;
            QStringList paths;
            in >> magic >> paths;
            if (!in.commitTransaction())
                return; // 資料還沒收完整，等下一次 readyRead
            socket->disconnectFromServer();
            if (magic != kMagic)
                return;
            // 延後到事件迴圈處理：開檔時可能跳出對話框，不要在 socket 的訊號裡進行
            QMetaObject::invokeMethod(this, [this, paths] { emit pathsReceived(paths); },
                                      Qt::QueuedConnection);
        });
    }
}
