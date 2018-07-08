/*
 * Copyright (C) 2017 ~ 2018 Deepin Technology Co., Ltd.
 *
 * Author:     sbw <sbw@sbw.so>
 *
 * Maintainer: sbw <sbw@sbw.so>
 *             kirigaya <kirigaya@mkacg.com>
 *             Hualet <mr.asianwang@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "appsmanager.h"
#include "src/global_util/util.h"
#include "src/global_util/constants.h"
#include "src/global_util/calculate_util.h"

#include <QDebug>
#include <QX11Info>
#include <QSvgRenderer>
#include <QPainter>
#include <QDataStream>
#include <QIODevice>
#include <QIcon>

#include <QGSettings>

#include <DApplication>

DWIDGET_USE_NAMESPACE

QPointer<AppsManager> AppsManager::INSTANCE = nullptr;

QGSettings AppsManager::LAUNCHER_SETTINGS("com.deepin.dde.launcher", "", nullptr);
QSettings AppsManager::APP_AUTOSTART_CACHE("deepin", "dde-launcher-app-autostart", nullptr);
QSettings AppsManager::APP_USER_SORTED_LIST("deepin", "dde-launcher-app-sorted-list", nullptr);
QSettings AppsManager::APP_USED_SORTED_LIST("deepin", "dde-launcher-app-used-sorted-list");

// A separate definition, in the category of ItemInfo
static const QMap<uint, QString> categoryTs {
    {0, QObject::tr("Internet")},
    {1, QObject::tr("Chat")},
    {2, QObject::tr("Music")},
    {3, QObject::tr("Video")},
    {4, QObject::tr("Graphics")},
    {5, QObject::tr("Game")},
    {6, QObject::tr("Office")},
    {7, QObject::tr("Reading")},
    {8, QObject::tr("Development")},
    {9, QObject::tr("System")},
    {10, QObject::tr("Others")},
};

static const QMap<uint, QString> categoryIcon {
    {0, QString(":/icons/skin/icons/internet_normal_16px.svg")},
    {1, QString(":/icons/skin/icons/chat_normal_16px.svg")},
    {2, QString(":/icons/skin/icons/music_normal_16px.svg")},
    {3, QString(":/icons/skin/icons/multimedia_normal_16px.svg")},
    {4, QString(":/icons/skin/icons/graphics_normal_16px.svg")},
    {5, QString(":/icons/skin/icons/game_normal_16px.svg")},
    {6, QString(":/icons/skin/icons/office_normal_16px.svg")},
    {7, QString(":/icons/skin/icons/reading_normal_16px.svg")},
    {8, QString(":/icons/skin/icons/development_normal_16px.svg")},
    {9, QString(":/icons/skin/icons/system_normal_16px.svg")},
    {10, QString(":/icons/skin/icons/others_normal_16px.svg")},
};

static const ItemInfo createOfCategory(uint category) {
    ItemInfo info;
    info.m_name = categoryTs[category];
    info.m_categoryId = category;
    info.m_iconKey = categoryIcon[category];
    return std::move(info);
}

int perfectIconSize(const int size)
{
    const int s = 8;
    const int l[s] = { 16, 24, 32, 48, 64, 96, 128, 256 };

    for (int i(0); i != s; ++i)
        if (size < l[i])
            return l[i];

    return 256;
}

const QPixmap getThemeIcon(const QString &iconName, const int size)
{
    const auto ratio = qApp->devicePixelRatio();
    const int s = perfectIconSize(size);

    QPixmap pixmap;
    do {
        if (iconName.startsWith("data:image/"))
        {
            const QStringList strs = iconName.split("base64,");
            if (strs.size() == 2)
                pixmap.loadFromData(QByteArray::fromBase64(strs.at(1).toLatin1()));

            if (!pixmap.isNull())
                break;
        }

        if (QFile::exists(iconName))
        {
            if (iconName.endsWith(".svg"))
                pixmap = loadSvg(iconName, s * ratio);
            else
                pixmap = QPixmap(iconName);

            if (!pixmap.isNull())
                break;
        }

        const QIcon icon = QIcon::fromTheme(iconName, QIcon::fromTheme("application-x-desktop"));
        pixmap = icon.pixmap(QSize(s, s));
        if (!pixmap.isNull())
            break;

        pixmap = loadSvg(":/skin/images/application-default-icon.svg", s * ratio);
        Q_ASSERT(!pixmap.isNull());
    } while (false);

    if (qFuzzyCompare(pixmap.devicePixelRatioF(), 1.))
    {
        pixmap = pixmap.scaled(QSize(s, s) * ratio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        pixmap.setDevicePixelRatio(ratio);
    }

    return pixmap;
}

AppsManager::AppsManager(QObject *parent) :
    QObject(parent),
    m_launcherInter(new DBusLauncher(this)),
    m_startManagerInter(new DBusStartManager(this)),
    m_dockInter(new DBusDock(this)),
    m_calUtil(CalculateUtil::instance()),
    m_searchTimer(new QTimer(this)),
    m_delayRefreshTimer(new QTimer(this))
{
    for (auto it = categoryTs.begin(); it != categoryTs.end(); ++it) {
        m_categoryList << std::move(createOfCategory(it.key()));
    }

    m_newInstalledAppsList = m_launcherInter->GetAllNewInstalledApps().value();

    refreshCategoryInfoList();
    refreshUsedInfoList();

    if (APP_AUTOSTART_CACHE.value("version").toString() != qApp->applicationVersion())
        refreshAppAutoStartCache();

    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(150);
    m_delayRefreshTimer->setSingleShot(true);
    m_delayRefreshTimer->setInterval(500);

    connect(qApp, &DApplication::iconThemeChanged, this, &AppsManager::onIconThemeChanged, Qt::QueuedConnection);
    connect(m_launcherInter, &DBusLauncher::NewAppLaunched, this, &AppsManager::markLaunched);
    connect(m_launcherInter, &DBusLauncher::SearchDone, this, &AppsManager::searchDone);
    connect(m_launcherInter, &DBusLauncher::UninstallSuccess, this, &AppsManager::abandonStashedItem);
    connect(m_launcherInter, &DBusLauncher::UninstallFailed, [this] (const QString &appKey) { restoreItem(appKey); emit dataChanged(AppsListModel::All); });
    connect(m_launcherInter, &DBusLauncher::ItemChanged, this, &AppsManager::handleItemChanged);
    connect(m_dockInter, &DBusDock::PositionChanged, this, &AppsManager::dockGeometryChanged);
    connect(m_dockInter, &DBusDock::IconSizeChanged, this, &AppsManager::dockGeometryChanged);
    connect(m_startManagerInter, &DBusStartManager::AutostartChanged, this, &AppsManager::refreshAppAutoStartCache);
    connect(m_delayRefreshTimer, &QTimer::timeout, this, &AppsManager::delayRefreshData);
    connect(m_searchTimer, &QTimer::timeout, this, &AppsManager::onSearchTimeOut);
}

void AppsManager::appendSearchResult(const QString &appKey)
{
    for (const ItemInfo &info : m_allAppInfoList)
        if (info.m_key == appKey)
            return m_appSearchResultList.append(info);
}

void AppsManager::sortCategory(const AppsListModel::AppCategory category)
{
    switch (category)
    {
    case AppsListModel::Search:     sortByPresetOrder(m_appSearchResultList);      break;
//    case AppsListModel::All:        sortByName(m_appInfoList);              break;
    // disable sort other category
    default: Q_ASSERT(false) ;
    }
}

void AppsManager::sortByPresetOrder(ItemInfoList &processList)
{
    const QString system_lang = QLocale::system().name();

    QString key = "appsOrder";
    for (const auto &item : system_lang.split('_'))
    {
        Q_ASSERT(!item.isEmpty());

        QString k = item.toLower();
        k[0] = k[0].toUpper();

        key.append(k);
    }

//    qDebug() << "preset order: " << key << APP_PRESET_SORTED_LIST.keys();
    QStringList preset;
    if (LAUNCHER_SETTINGS.keys().contains(key))
        preset = LAUNCHER_SETTINGS.get(key).toStringList();
    if (preset.isEmpty())
        preset = LAUNCHER_SETTINGS.get("apps-order").toStringList();

    qSort(processList.begin(), processList.end(), [&preset] (const ItemInfo &i1, const ItemInfo &i2) {
        int index1 = preset.indexOf(i1.m_key.toLower());
        int index2 = preset.indexOf(i2.m_key.toLower());

        if (index1 == index2) {
            // If both of them don't exist in the preset list,
            // fallback to comparing their name.
            return i1.m_name < i2.m_name;
        }

        // If one of them doesn't exist in the preset list,
        // the one exists go first.
        if (index1 == -1) {
            return false;
        }
        if (index2 == -1) {
            return true;
        }

        // If both of them exist, then obey the preset order.
        return index1 < index2;
    });
}

AppsManager *AppsManager::instance()
{
    if (INSTANCE.isNull())
        INSTANCE = new AppsManager;

    return INSTANCE;
}

void AppsManager::stashItem(const QModelIndex &index)
{
    const QString key = index.data(AppsListModel::AppKeyRole).toString();

    return stashItem(key);
}

void AppsManager::stashItem(const QString &appKey)
{
    for (int i(0); i != m_allAppInfoList.size(); ++i)
    {
        if (m_allAppInfoList[i].m_key == appKey)
        {
            m_stashList.append(m_allAppInfoList[i]);
            m_allAppInfoList.removeAt(i);

            generateCategoryMap();
            refreshUsedInfoList();

            return;
        }
    }
}

void AppsManager::abandonStashedItem(const QString &appKey)
{
    //qDebug() << "bana" << appKey;
    for (int i(0); i != m_stashList.size(); ++i)
        if (m_stashList[i].m_key == appKey)
            return m_stashList.removeAt(i);
}

void AppsManager::restoreItem(const QString &appKey, const int pos)
{
    for (int i(0); i != m_stashList.size(); ++i)
    {
        if (m_stashList[i].m_key == appKey)
        {
            // if pos is valid
            if (pos != -1)
                m_usedSortedList.insert(pos, m_stashList[i]);
            m_allAppInfoList.append(m_stashList[i]);
            m_stashList.removeAt(i);

            generateCategoryMap();

            return saveUserSortedList();
        }
    }
}

int AppsManager::dockPosition() const
{
    return m_dockInter->position();
}

int AppsManager::dockWidth() const
{
    return QRect(m_dockInter->frontendRect()).width();
}

void AppsManager::saveUserSortedList()
{
    // save cache
    QByteArray writeBuf;
    QDataStream out(&writeBuf, QIODevice::WriteOnly);
    out << m_usedSortedList;

    APP_USER_SORTED_LIST.setValue("list", writeBuf);
}

void AppsManager::saveUsedSortedList()
{
    QByteArray writeBuf;
    QDataStream out(&writeBuf, QIODevice::WriteOnly);
    out << m_usedSortedList;

    APP_USED_SORTED_LIST.setValue("list", writeBuf);
}

void AppsManager::searchApp(const QString &keywords)
{
    m_searchTimer->start();
    m_searchText = keywords;
}

void AppsManager::launchApp(const QModelIndex &index)
{
    const QString appDesktop = index.data(AppsListModel::AppDesktopRole).toString();
    QString appKey = index.data(AppsListModel::AppKeyRole).toString();
    markLaunched(appKey);

    for (ItemInfo &info : m_usedSortedList) {
        if (info.m_key == appKey) {
            const int idx = m_usedSortedList.indexOf(info);

            if (idx != -1) {
                m_usedSortedList[idx].m_openCount++;
            }

            break;
        }
    }

    refreshUsedInfoList();

    if (!appDesktop.isEmpty())
        m_startManagerInter->LaunchWithTimestamp(appDesktop, QX11Info::getTimestamp());
}

void AppsManager::uninstallApp(const QString &appKey)
{
    // refersh auto start cache
    for (const ItemInfo &info : m_allAppInfoList)
    {
        if (info.m_key == appKey)
        {
            APP_AUTOSTART_CACHE.remove(info.m_desktop);
            break;
        }
    }

    // begin uninstall, remove icon first.
    stashItem(appKey);

    // request backend
    m_launcherInter->RequestUninstall(appKey, false);

    emit dataChanged(AppsListModel::All);

    // refersh search result
    m_searchTimer->start();
}

void AppsManager::markLaunched(QString appKey)
{
    if (appKey.isEmpty() || !m_newInstalledAppsList.contains(appKey))
        return;

    m_newInstalledAppsList.removeOne(appKey);
    m_launcherInter->MarkLaunched(appKey);

    emit newInstallListChanged();
}

void AppsManager::delayRefreshData()
{
    // refresh new installed apps
    m_newInstalledAppsList = m_launcherInter->GetAllNewInstalledApps().value();

    generateCategoryMap();
    saveUserSortedList();

    emit newInstallListChanged();

    emit dataChanged(AppsListModel::All);
}

const ItemInfoList AppsManager::appsInfoList(const AppsListModel::AppCategory &category) const
{
    switch (category)
    {
    case AppsListModel::Custom:
    case AppsListModel::All:       return m_usedSortedList;        break;
    case AppsListModel::Search:     return m_appSearchResultList;   break;
    case AppsListModel::Category:   return m_categoryList;          break;
    default:;
    }

    return m_appInfos[category];
}

bool AppsManager::appIsNewInstall(const QString &key)
{
    return m_newInstalledAppsList.contains(key);
}

bool AppsManager::appIsAutoStart(const QString &desktop)
{
    if (APP_AUTOSTART_CACHE.contains(desktop))
        return APP_AUTOSTART_CACHE.value(desktop).toBool();

    const bool isAutoStart = m_startManagerInter->IsAutostart(desktop).value();

    APP_AUTOSTART_CACHE.setValue(desktop, isAutoStart);

    return isAutoStart;
}

bool AppsManager::appIsOnDock(const QString &desktop)
{
    return m_dockInter->IsDocked(desktop);
}

bool AppsManager::appIsOnDesktop(const QString &desktop)
{
    return m_launcherInter->IsItemOnDesktop(desktop).value();
}

bool AppsManager::appIsProxy(const QString &desktop)
{
    return m_launcherInter->GetUseProxy(desktop).value();
}

bool AppsManager::appIsEnableScaling(const QString &desktop)
{
    return !m_launcherInter->GetDisableScaling(desktop);
}

const QPixmap AppsManager::appIcon(const QString &iconKey, const int size)
{
    QPair<QString, int> tmpKey { iconKey, size };

    if (m_iconCache.contains(tmpKey) && !m_iconCache[tmpKey].isNull()) {
        return m_iconCache[tmpKey];
    }

    const QPixmap &pixmap = getThemeIcon(iconKey, size / qApp->devicePixelRatio());

    m_iconCache[tmpKey] = pixmap;

    return pixmap;
}

void AppsManager::refreshCategoryInfoList()
{
    QByteArray readBuf = APP_USER_SORTED_LIST.value("list").toByteArray();
    QDataStream in(&readBuf, QIODevice::ReadOnly);
    in >> m_usedSortedList;

    const ItemInfoList &datas = m_launcherInter->GetAllItemInfos().value();
    m_allAppInfoList.clear();
    m_allAppInfoList.reserve(datas.size());
    for (const auto &it : datas) {
        if (!m_stashList.contains(it)) {
            m_allAppInfoList.append(it);
        }
    }

    generateCategoryMap();
    saveUserSortedList();
}

void AppsManager::refreshUsedInfoList()
{
    // init data if used sorted list is empty.
    if (m_usedSortedList.isEmpty()) {
        // first reads the config file.
        QByteArray readBuffer = APP_USED_SORTED_LIST.value("list").toByteArray();
        QDataStream in(&readBuffer, QIODevice::ReadOnly);
        in >> m_usedSortedList;

        // if data cache file is empty.
        if (m_usedSortedList.isEmpty()) {
            m_usedSortedList = m_allAppInfoList;
        }

        // add new additions
        for (QList<ItemInfo>::ConstIterator it = m_allAppInfoList.constBegin(); it != m_allAppInfoList.constEnd(); ++it) {
            if (!m_usedSortedList.contains(*it)) {
                m_usedSortedList.append(*it);
            }
        }

        // check used list isvaild
        for (QList<ItemInfo>::iterator it = m_usedSortedList.begin(); it != m_usedSortedList.end();) {
            if (m_allAppInfoList.contains(*it)) {
                it++;
            }
            else {
                it = m_usedSortedList.erase(it);
            }
        }

        updateUsedListInfo();
    }

    std::stable_sort(m_usedSortedList.begin(), m_usedSortedList.end(),
                     [] (const ItemInfo &a, const ItemInfo &b) {
                         return a.m_openCount > b.m_openCount;
                     });

    saveUsedSortedList();
}

void AppsManager::updateUsedListInfo()
{
    for (const ItemInfo &info : m_allAppInfoList) {
        const int idx = m_usedSortedList.indexOf(info);

        if (idx != -1) {
            const int openCount = m_usedSortedList[idx].m_openCount;
            m_usedSortedList[idx].updateInfo(info);
            m_usedSortedList[idx].m_openCount = openCount;
        }
    }
}

void AppsManager::generateCategoryMap()
{
    m_appInfos.clear();
    sortByPresetOrder(m_allAppInfoList);

    for (const ItemInfo &info : m_allAppInfoList) {
        const int userIdx = m_usedSortedList.indexOf(info);
        // append new installed app to user sorted list
        if (userIdx == -1) {
            m_usedSortedList.append(info);
        } else {
            const int openCount = m_usedSortedList[userIdx].m_openCount;
            m_usedSortedList[userIdx].updateInfo(info);
            m_usedSortedList[userIdx].m_openCount = openCount;
        }

        const AppsListModel::AppCategory category = info.category();
        if (!m_appInfos.contains(category))
            m_appInfos.insert(category, ItemInfoList());

        m_appInfos[category].append(info);
    }

    // remove uninstalled app item
    for (auto it(m_usedSortedList.begin()); it != m_usedSortedList.end();) {
        const int idx = m_allAppInfoList.indexOf(*it);

        if (idx == -1)
            it = m_usedSortedList.erase(it);
        else
            ++it;
    }

    emit categoryListChanged();
}

int AppsManager::appNums(const AppsListModel::AppCategory &category) const
{
    return appsInfoList(category).size();
}

void AppsManager::refreshAppAutoStartCache()
{
    APP_AUTOSTART_CACHE.setValue("version", qApp->applicationVersion());

    for (const ItemInfo &info : m_allAppInfoList)
    {
        const bool isAutoStart = m_startManagerInter->IsAutostart(info.m_desktop).value();
        APP_AUTOSTART_CACHE.setValue(info.m_desktop, isAutoStart);
    }

    emit dataChanged(AppsListModel::All);
}

void AppsManager::onSearchTimeOut()
{
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(m_launcherInter->Search(m_searchText), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [=] (QDBusPendingCallWatcher * w) {
        if (w->isError()) qDebug() << w->error();

        w->deleteLater();
    });
}

void AppsManager::onIconThemeChanged()
{
    m_iconCache.clear();

    emit dataChanged(AppsListModel::All);
}

void AppsManager::searchDone(const QStringList &resultList)
{
    m_appSearchResultList.clear();

    for (const QString &key : resultList)
        appendSearchResult(key);

    emit dataChanged(AppsListModel::Search);

    if (m_appSearchResultList.isEmpty())
        emit requestTips(tr("No search results"));
    else
        emit requestHideTips();
}

void AppsManager::handleItemChanged(const QString &operation, const ItemInfo &appInfo, qlonglong categoryNumber)
{
    qDebug() << operation << appInfo << categoryNumber;

    if (operation == "created") {
        m_allAppInfoList.append(appInfo);
        m_usedSortedList.append(appInfo);
    } else if (operation == "deleted") {

        m_allAppInfoList.removeOne(appInfo);
        m_usedSortedList.removeOne(appInfo);
    } else if (operation == "updated") {

        Q_ASSERT(m_allAppInfoList.contains(appInfo));

        // update item info
        for (auto &item : m_allAppInfoList)
        {
            if (item == appInfo)
            {
                item.updateInfo(appInfo);
                break;
            }
        }
    }

    m_delayRefreshTimer->start();
}
