/*
 * Copyright (C) by Duncan Mac-Vicar P. <duncan@kde.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

// event masks
#include "mirall/folderwatcher.h"
#include "mirall/folder.h"
#include "mirall/inotify.h"
#include "mirall/fileutils.h"

#include <stdint.h>

#include <QFileInfo>
#include <QFlags>
#include <QDebug>
#include <QDir>
#include <QMutexLocker>
#include <QStringList>
#include <QTimer>

/* minimum amount of seconds between two
   events  to consider it a new event */
#define DEFAULT_EVENT_INTERVAL_MSEC 1000

#ifdef NEW_FW_BEHAVIOR
#if defined(Q_OS_WIN)
#include "mirall/folderwatcher_win.cpp"
#elif defined(Q_OS_MAC)
#include "mirall/folderwatcher_mac.cpp"
#endif
#endif

#ifdef USE_INOTIFY
#include "mirall/folderwatcher_inotify.cpp"
#else
#include "mirall/folderwatcher_dummy.cpp"
#endif

namespace Mirall {

FolderWatcher::FolderWatcher(const QString &root, QObject *parent)
    : QObject(parent),
      _d(new FolderWatcherPrivate),
      _eventsEnabled(true),
      _eventInterval(DEFAULT_EVENT_INTERVAL_MSEC),
      _root(root),
      _processTimer(new QTimer(this)),
      _lastMask(0),
      _initialSyncDone(false)
{
    setupBackend();
    // do a first synchronization to get changes while
    // the application was not running
    setProcessTimer();
}

FolderWatcher::~FolderWatcher()
{
    delete _d;
}

QString FolderWatcher::root() const
{
    return _root;
}

void FolderWatcher::setIgnoreListFile( const QString& file )
{
    if( file.isEmpty() ) return;

    QFile infile( file );
    if (!infile.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    while (!infile.atEnd()) {
        QString line = QString::fromLocal8Bit( infile.readLine() ).trimmed();
        if( !line.startsWith( QLatin1Char('#') )) {
            addIgnore(line);
        }
    }
}

void FolderWatcher::addIgnore(const QString &pattern)
{
    if( pattern.isEmpty() ) return;
    _ignores.append(pattern);
}

bool FolderWatcher::eventsEnabled() const
{
    return _eventsEnabled;
}

void FolderWatcher::setEventsEnabled(bool enabled)
{
    qDebug() << "    * event notification " << (enabled ? "enabled" : "disabled");
    _eventsEnabled = enabled;
    if (_eventsEnabled) {
        // schedule a queue cleanup for accumulated events
        if ( _pendingPathes.empty() )
            return;
        setProcessTimer();
    }
    else
    {
        // if we are disabling events, clear any ongoing timer
        if (_processTimer->isActive())
            _processTimer->stop();
    }
}

void FolderWatcher::clearPendingEvents()
{
    if (_processTimer->isActive())
        _processTimer->stop();
    _pendingPathes.clear();
}

int FolderWatcher::eventInterval() const
{
    return _eventInterval;
}

void FolderWatcher::setEventInterval(int seconds)
{
    _eventInterval = seconds;
}

void FolderWatcher::slotProcessTimerTimeout()
{
    qDebug() << "* Processing of event queue for" << root();

    if (!_pendingPathes.empty() || !_initialSyncDone) {
        QStringList notifyPaths = _pendingPathes.keys();
        _pendingPathes.clear();
        //qDebug() << lastEventTime << eventTime;
        qDebug() << "  * Notify" << notifyPaths.size() << "changed items for" << root();
        emit folderChanged(notifyPaths);
        _initialSyncDone = true;
    }
}

void FolderWatcher::setProcessTimer()
{
    if (!_processTimer->isActive()) {
        qDebug() << "* Pending events for" << root()
                 << "will be processed after events stop for"
                 << eventInterval() << "seconds ("
                 << QTime::currentTime().addSecs(eventInterval()).toString(QLatin1String("HH:mm:ss"))
                 << ")." << _pendingPathes.size() << "events until now )";
    }
    _processTimer->start(eventInterval());
}

} // namespace Mirall

