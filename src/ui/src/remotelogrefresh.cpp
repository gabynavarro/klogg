// SPDX-License-Identifier: GPL-3.0-or-later
#include "remotelogrefresh.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

RemoteLogRefresh::RemoteLogRefresh( QObject* parent ) : QObject( parent )
{
    timeout_.setSingleShot( true );
    connect( &timeout_, &QTimer::timeout, this, [this] {
        process_.kill();
        finish( tr( "Remote download timed out. The previous copy was kept." ) );
    } );
    connect( &process_, &QProcess::readyReadStandardOutput, this, [this] { drain(); } );
    connect( &process_, &QProcess::readyReadStandardError, this, [this] {
        const auto data = process_.readAllStandardError();
        diagnostics_.append( data.left( 8192 - diagnostics_.size() ) );
    } );
    connect( &process_, &QProcess::errorOccurred, this, [this]( QProcess::ProcessError error ) {
        if ( error == QProcess::FailedToStart )
            finish( tr( "Could not start OpenSSH: %1" ).arg( process_.errorString() ) );
    } );
    connect( &process_, QOverload<int, QProcess::ExitStatus>::of( &QProcess::finished ), this,
             [this]( int code, QProcess::ExitStatus status ) {
        if ( !active_ ) return;
        drain();
        if ( !writeError_.isEmpty() ) { finish( writeError_ ); return; }
        if ( status != QProcess::NormalExit || code != 0 ) {
            finish( tr( "SSH download failed. The previous copy was kept.\n%1" )
                        .arg( QString::fromUtf8( diagnostics_ ) ) );
            return;
        }
        if ( !staging_.flush() || !staging_.seek( 0 ) ) {
            finish( staging_.errorString() );
            return;
        }
        QSaveFile output( destination_ );
        output.setDirectWriteFallback( false );
        if ( !output.open( QIODevice::WriteOnly ) ) { finish( output.errorString() ); return; }
        while ( !staging_.atEnd() ) {
            const auto chunk = staging_.read( 65536 );
            if ( chunk.isEmpty() || output.write( chunk ) != chunk.size() ) {
                output.cancelWriting();
                finish( tr( "Could not save remote log. The previous copy was kept." ) );
                return;
            }
        }
        const bool saved = output.commit();
        finish( saved ? QString{} : output.errorString() );
    } );
}

RemoteLogRefresh::~RemoteLogRefresh()
{
    completed_ = {};
    process_.kill();
    process_.waitForFinished( 1000 );
}

QString RemoteLogRefresh::validate( const QUrl& source )
{
    static const QRegularExpression host( "^[A-Za-z0-9_][A-Za-z0-9_.:-]*$" );
    static const QRegularExpression user( "^[A-Za-z0-9_][A-Za-z0-9_.-]*$" );
    const auto path = source.path();
    if ( !source.isValid() || source.scheme() != "ssh" || !host.match( source.host() ).hasMatch()
         || ( !source.userName().isEmpty() && !user.match( source.userName() ).hasMatch() )
         || !source.password().isEmpty() || source.hasQuery() || source.hasFragment()
         || source.port( 22 ) < 1 || source.port( 22 ) > 65535
         || !path.startsWith( '/' ) || path.contains( QChar( 0 ) )
         || path.contains( '\n' ) || path.contains( '\r' ) )
        return tr( "Use ssh://user@host:22/absolute/path/app.log (no password, query or fragment)." );
    return {};
}

QStringList RemoteLogRefresh::arguments( const QUrl& source )
{
    if ( !validate( source ).isEmpty() ) return {};
    auto path = source.path();
    path.replace( "'", "'\\''" );
    QStringList args{ "-T", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=yes",
                      "-o", "ConnectTimeout=15", "-p", QString::number( source.port( 22 ) ) };
    if ( !source.userName().isEmpty() ) args << "-l" << source.userName();
    args << "--" << source.host() << ( "cat -- '" + path + "'" );
    return args;
}

void RemoteLogRefresh::start( const QUrl& source, const QString& destination,
                             std::function<void( const QString& )> completed )
{
    // One operation per instance. GUI owns it until the callback and then deletes it.
    if ( active_ ) return;
    active_ = true;
    completed_ = std::move( completed );
    destination_ = destination;
    const auto error = validate( source );
    if ( !error.isEmpty() ) { finish( error ); return; }
    const auto ssh = QStandardPaths::findExecutable( "ssh" );
    if ( ssh.isEmpty() ) { finish( tr( "OpenSSH (ssh) was not found in PATH." ) ); return; }
    // Stage on the same volume, with private temporary-file permissions.
    staging_.setFileTemplate( QFileInfo( destination ).absolutePath() + "/.klogg-ssh-XXXXXX" );
    if ( !staging_.open() ) { finish( staging_.errorString() ); return; }
    timeout_.start( 120000 );
    process_.start( ssh, arguments( source ), QIODevice::ReadOnly );
}

void RemoteLogRefresh::drain()
{
    const auto data = process_.readAllStandardOutput();
    if ( active_ && !data.isEmpty() && staging_.write( data ) != data.size() ) {
        writeError_ = tr( "Could not write temporary download. The previous copy was kept." );
        process_.kill();
    }
}

void RemoteLogRefresh::cancel()
{
    process_.kill();
    finish( tr( "Download cancelled. The previous copy was kept." ) );
}

void RemoteLogRefresh::finish( const QString& error )
{
    if ( !active_ ) return;
    active_ = false;
    timeout_.stop();
    staging_.close();
    if ( completed_ ) {
        auto callback = std::move( completed_ );
        callback( error );
    }
}
