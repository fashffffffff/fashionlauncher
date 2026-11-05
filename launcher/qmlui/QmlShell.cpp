// SPDX-License-Identifier: GPL-3.0-only
#include "QmlShell.h"

#include <QEvent>
#include <QFontDatabase>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QTranslator>

#include "AccountsModel.h"
#include "Application.h"
#include "ImageProviders.h"
#include "InstancesModel.h"
#include "LauncherBridge.h"
#include "UiTheme.h"
#include "settings/SettingsObject.h"

QmlShell::QmlShell(QObject* parent) : QObject(parent)
{
    static bool fontsLoaded = false;
    if (!fontsLoaded) {
        QFontDatabase::addApplicationFont(":/qmlui/fonts/Unbounded.ttf");
        QFontDatabase::addApplicationFont(":/qmlui/fonts/GolosText.ttf");
        // Basic is the only Controls style that is fully restyleable from QML on every platform
        QQuickStyle::setStyle("Basic");
        fontsLoaded = true;

        // Strings of the QML shell ship with it (qmlui/i18n); the rest of the launcher uses Prism's translations.
        const QString lang = APPLICATION->settings()->get("Language").toString().section('_', 0, 0);
        auto* translator = new QTranslator(APPLICATION);
        if (translator->load(QString(":/qmlui/i18n/qmlui_%1.qm").arg(lang))) {
            QCoreApplication::installTranslator(translator);
        } else {
            delete translator;
        }
    }

    m_theme = new UiTheme(this);
    m_instances = new InstancesModel(this);
    m_accounts = new AccountsModel(this);
    m_launcher = new LauncherBridge(this);

    m_engine = new QQmlApplicationEngine(this);
    m_engine->addImageProvider("instanceicon", new InstanceIconProvider);
    m_engine->addImageProvider("accountface", new AccountFaceProvider);
    auto* ctx = m_engine->rootContext();
    ctx->setContextProperty("Theme", m_theme);
    ctx->setContextProperty("Instances", m_instances);
    ctx->setContextProperty("Accounts", m_accounts);
    ctx->setContextProperty("Launcher", m_launcher);
}

QmlShell::~QmlShell()
{
    if (m_window) {
        APPLICATION->settings()->set("QmlShellGeometry", QVariantList{ m_window->x(), m_window->y(), m_window->width(), m_window->height() });
    }
    // Destroy QML before the C++ objects it references.
    delete m_engine;
}

bool QmlShell::load()
{
    m_engine->load(QUrl("qrc:/qmlui/qml/Main.qml"));
    if (m_engine->rootObjects().isEmpty()) {
        qCritical() << "QML shell failed to load";
        return false;
    }
    m_window = qobject_cast<QQuickWindow*>(m_engine->rootObjects().first());
    if (!m_window) {
        return false;
    }
    const auto geo = APPLICATION->settings()->get("QmlShellGeometry").toList();
    if (geo.size() == 4 && geo[2].toInt() > 400 && geo[3].toInt() > 300) {
        m_window->setGeometry(geo[0].toInt(), geo[1].toInt(), geo[2].toInt(), geo[3].toInt());
    }
    m_window->installEventFilter(this);

    // Developer aid: FFPRISM_SNAPSHOT=/path.png saves a picture of this window (and only this window)
    // after it settles; FFPRISM_PAGE picks the page first, FFPRISM_SNAPSHOT_QUIT=1 exits afterwards.
    if (const QString shot = qEnvironmentVariable("FFPRISM_SNAPSHOT"); !shot.isEmpty()) {
        if (const QString page = qEnvironmentVariable("FFPRISM_PAGE"); !page.isEmpty()) {
            QTimer::singleShot(300, m_window, [this, page] { QMetaObject::invokeMethod(m_window, "showPage", Q_ARG(QVariant, page)); });
        }
        QTimer::singleShot(qEnvironmentVariableIntValue("FFPRISM_SNAPSHOT_DELAY") > 0 ? qEnvironmentVariableIntValue("FFPRISM_SNAPSHOT_DELAY") : 2500,
                           m_window, [this, shot] {
                               m_window->grabWindow().save(shot);
                               if (qEnvironmentVariableIntValue("FFPRISM_SNAPSHOT_QUIT")) {
                                   QCoreApplication::exit(0);
                               }
                           });
    }
    return true;
}

void QmlShell::show(bool minimized)
{
    if (!m_window) {
        return;
    }
    if (minimized) {
        m_window->showMinimized();
    } else {
        m_window->show();
    }
}

void QmlShell::raise()
{
    if (!m_window) {
        return;
    }
    m_window->setWindowStates(m_window->windowStates() & ~Qt::WindowMinimized);
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
}

bool QmlShell::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_window) {
        if (event->type() == QEvent::Close) {
            emit isClosing();
        } else if (event->type() == QEvent::FocusIn) {
            // Pick up new screenshots and edits made in the classic instance editor.
            m_instances->refreshMeta();
        }
    }
    return QObject::eventFilter(watched, event);
}
