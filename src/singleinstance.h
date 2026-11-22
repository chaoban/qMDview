#pragma once

#include <QObject>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QLocalServer;
QT_END_NAMESPACE

// 只執行一個 qMDView：
//   第一個啟動的程式建立 QLocalServer（Windows 上是具名管道）等待連線；
//   之後再啟動的程式（例如在檔案總管雙擊另一個 .md）連上它、把檔案路徑交過去後就結束，
//   檔案改在原本的視窗以新分頁開啟。
// 管道名稱含執行檔路徑與使用者名稱：Store 版與一般安裝版、不同使用者之間互不干擾。
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    enum class Result {
        Primary,     // 自己是第一個，已開始等待其他程式轉交
        Forwarded,   // 已經有 qMDView 在執行，路徑已交給它（呼叫端應結束）
        OtherRunning,// 已經有 qMDView 在執行，但 forward=false 所以沒有轉交
        Failed       // 無法建立等待連線（呼叫端照常以獨立視窗執行）
    };

    explicit SingleInstance(QObject *parent = nullptr);
    ~SingleInstance() override;

    // forward=false：只在沒有其他 qMDView 時開始等待連線（切換回「新分頁」模式時使用）
    Result start(const QStringList &paths, bool forward = true);
    void stop();
    bool isListening() const;

signals:
    void pathsReceived(const QStringList &paths);   // 空清單表示只要把視窗帶到最前面

private:
    void onNewConnection();
    static QString serverName();

    QLocalServer *m_server = nullptr;
};
