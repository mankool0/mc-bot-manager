#pragma once
#include <QColor>
#include <QSettings>

namespace AppColors {

struct Cache {
    // Console widget
    QColor consoleReady   {0,   102, 204};
    QColor consoleInput   {0,   102,   0};
    QColor consoleSuccess {0,   102,   0};
    QColor consoleError   {204,   0,   0};
    QColor consoleDropped = Qt::darkYellow;

    // Script output
    QColor scriptSuccess  = Qt::darkGreen;
    QColor scriptStopped  = Qt::darkYellow;
    QColor scriptError    = Qt::red;
    QColor scriptLog      = Qt::darkGreen;

    // Bot status indicator
    QColor statusOnline   {76,  175,  80};
    QColor statusOffline  {158, 158, 158};
    QColor statusError    {244,  67,  54};
    QColor statusOther    {255, 152,   0};

    // Log levels
    QColor logTimestamp   {128, 128, 128};
    QColor logDebug       {128, 128, 128};
    QColor logInfo        = Qt::black;
    QColor logWarning     {0xFF, 0x8C, 0x00};
    QColor logError       {0xDC, 0x14, 0x3C};
    QColor logSuccess     {0x22, 0x8B, 0x22};
};

inline const Cache& defaults()
{
    static const Cache d;
    return d;
}

inline Cache& _cache()
{
    static Cache instance;
    return instance;
}

inline void reload()
{
    const Cache &defaults = AppColors::defaults();
    QSettings s("MCBotManager", "MCBotManager");
    Cache &c = _cache();
    c.consoleReady   = s.value("Colors/Console/ready",   defaults.consoleReady).value<QColor>();
    c.consoleInput   = s.value("Colors/Console/input",   defaults.consoleInput).value<QColor>();
    c.consoleSuccess = s.value("Colors/Console/success", defaults.consoleSuccess).value<QColor>();
    c.consoleError   = s.value("Colors/Console/error",   defaults.consoleError).value<QColor>();
    c.consoleDropped = s.value("Colors/Console/dropped", defaults.consoleDropped).value<QColor>();

    c.scriptSuccess  = s.value("Colors/Script/success",  defaults.scriptSuccess).value<QColor>();
    c.scriptStopped  = s.value("Colors/Script/stopped",  defaults.scriptStopped).value<QColor>();
    c.scriptError    = s.value("Colors/Script/error",    defaults.scriptError).value<QColor>();
    c.scriptLog      = s.value("Colors/Script/log",      defaults.scriptLog).value<QColor>();

    c.statusOnline   = s.value("Colors/Status/online",   defaults.statusOnline).value<QColor>();
    c.statusOffline  = s.value("Colors/Status/offline",  defaults.statusOffline).value<QColor>();
    c.statusError    = s.value("Colors/Status/error",    defaults.statusError).value<QColor>();
    c.statusOther    = s.value("Colors/Status/other",    defaults.statusOther).value<QColor>();

    c.logTimestamp   = s.value("Colors/Log/timestamp",   defaults.logTimestamp).value<QColor>();
    c.logDebug       = s.value("Colors/Log/debug",       defaults.logDebug).value<QColor>();
    c.logInfo        = s.value("Colors/Log/info",        defaults.logInfo).value<QColor>();
    c.logWarning     = s.value("Colors/Log/warning",     defaults.logWarning).value<QColor>();
    c.logError       = s.value("Colors/Log/error",       defaults.logError).value<QColor>();
    c.logSuccess     = s.value("Colors/Log/success",     defaults.logSuccess).value<QColor>();
}

inline const QColor& consoleReady()   { return _cache().consoleReady; }
inline const QColor& consoleInput()   { return _cache().consoleInput; }
inline const QColor& consoleSuccess() { return _cache().consoleSuccess; }
inline const QColor& consoleError()   { return _cache().consoleError; }
inline const QColor& consoleDropped() { return _cache().consoleDropped; }

inline const QColor& scriptSuccess()  { return _cache().scriptSuccess; }
inline const QColor& scriptStopped()  { return _cache().scriptStopped; }
inline const QColor& scriptError()    { return _cache().scriptError; }
inline const QColor& scriptLog()      { return _cache().scriptLog; }

inline const QColor& statusOnline()   { return _cache().statusOnline; }
inline const QColor& statusOffline()  { return _cache().statusOffline; }
inline const QColor& statusError()    { return _cache().statusError; }
inline const QColor& statusOther()    { return _cache().statusOther; }

inline const QColor& logTimestamp()   { return _cache().logTimestamp; }
inline const QColor& logDebug()       { return _cache().logDebug; }
inline const QColor& logInfo()        { return _cache().logInfo; }
inline const QColor& logWarning()     { return _cache().logWarning; }
inline const QColor& logError()       { return _cache().logError; }
inline const QColor& logSuccess()     { return _cache().logSuccess; }

} // namespace AppColors
