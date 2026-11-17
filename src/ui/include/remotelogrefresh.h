// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef KLOGG_REMOTELOGREFRESH_H
#define KLOGG_REMOTELOGREFRESH_H

#include <QObject>
#include <QProcess>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>
#include <functional>

// Read-only SSH download. Authentication and known hosts are managed by OpenSSH.
class RemoteLogRefresh : public QObject {
  public:
    explicit RemoteLogRefresh( QObject* parent = nullptr );
    ~RemoteLogRefresh() override;
    static QString validate( const QUrl& source );
    static QStringList arguments( const QUrl& source );
    void start( const QUrl& source, const QString& destination,
                std::function<void( const QString& )> completed );
    void cancel();

  private:
    void drain();
    void finish( const QString& error );
    QProcess process_;
    QTemporaryFile staging_;
    QTimer timeout_;
    QString destination_;
    QByteArray diagnostics_;
    QString writeError_;
    std::function<void( const QString& )> completed_;
    bool active_ = false;
};
#endif
