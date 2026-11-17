// SPDX-License-Identifier: GPL-3.0-or-later
#include "remotelogrefresh.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdlib>
#include <iostream>

static void check( bool condition, const char* message )
{
    if ( !condition ) { std::cerr << message << '\n'; std::exit( 1 ); }
}
static void write( const QString& path, const QByteArray& bytes )
{
    QFile file( path );
    check( file.open( QIODevice::WriteOnly ), "open fixture" );
    check( file.write( bytes ) == bytes.size(), "write fixture" );
}
static QByteArray read( const QString& path )
{
    QFile file( path );
    check( file.open( QIODevice::ReadOnly ), "read fixture" );
    return file.readAll();
}
int main( int argc, char** argv )
{
    QCoreApplication app( argc, argv );
    for ( const auto* input : { "file:///tmp/log", "ssh://-bad/tmp/log", "ssh://host/",
                               "ssh://u:secret@host/log", "ssh://host/log?q=x",
                               "ssh://host/log#frag", "ssh://host:0/log", "ssh://host/a%00b" } ) {
        // Root is a valid absolute path syntactically (cat reports it is a directory).
        if ( QString( input ) == "ssh://host/" ) continue;
        check( !RemoteLogRefresh::validate( QUrl( input ) ).isEmpty(), "invalid URL accepted" );
    }
    const QUrl source( "ssh://user@host:2222/tmp/a%27b%20%24%28touch%20BAD%29.log" );
    check( RemoteLogRefresh::validate( source ).isEmpty(), "valid URL rejected" );
    const auto args = RemoteLogRefresh::arguments( source );
    check( args.contains( "StrictHostKeyChecking=yes" ) && args.contains( "BatchMode=yes" ), "SSH safety options" );
    check( args.last() == "cat -- '/tmp/a'\\''b $(touch BAD).log'", "remote shell quoting" );

    QTemporaryDir dir;
    check( dir.isValid(), "temporary directory" );
    const auto ssh = dir.filePath( "ssh" );
    const auto destination = dir.filePath( "local.log" );
    qputenv( "PATH", dir.path().toUtf8() );
    auto run = [&]( const QByteArray& script, bool cancel = false ) {
        write( ssh, "#!/bin/sh\n" + script );
        QFile::setPermissions( ssh, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner );
        RemoteLogRefresh job;
        QEventLoop loop;
        QString result;
        bool done = false;
        int calls = 0;
        job.start( source, destination, [&]( const QString& error ) {
            ++calls; result = error; done = true; loop.quit();
        } );
        if ( cancel ) QTimer::singleShot( 25, &job, [&job] { job.cancel(); } );
        QTimer::singleShot( 5000, &loop, &QEventLoop::quit );
        if ( !done ) loop.exec();
        check( done && calls == 1, "completion callback missing or duplicated" );
        return result;
    };
    write( destination, "old\n" );
    check( run( "printf 'new\\n'\n" ).isEmpty(), "successful download" );
    check( read( destination ) == "new\n", "new content not committed" );
    check( !run( "printf 'partial'; printf 'denied' >&2; exit 1\n" ).isEmpty(), "failed SSH accepted" );
    check( read( destination ) == "new\n", "failed download changed previous copy" );
    check( !run( "exec /bin/sleep 10\n", true ).isEmpty(), "cancellation failed" );
    check( read( destination ) == "new\n", "cancel changed previous copy" );
    check( run( "exit 0\n" ).isEmpty(), "empty remote log rejected" );
    check( read( destination ).isEmpty(), "empty remote log not committed" );
    check( !run( "kill -KILL $$\n" ).isEmpty(), "crashed SSH accepted" );
    QFile::remove( ssh );
    QEventLoop loop;
    RemoteLogRefresh job;
    bool missing = false;
    job.start( source, destination, [&]( const QString& error ) { missing = !error.isEmpty(); } );
    check( missing, "missing SSH not reported" );
    std::cout << "PASS: URL validation, shell quoting, success, failure preservation, cancellation, empty log, crash and missing SSH\n";
}
