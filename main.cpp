#include <QQmlApplicationEngine>
#include <QtWebEngine>
#include <QSysInfo>

#include <clocale>

#define APP_TITLE "Stremio - Freedom to Stream"

#define DESKTOP true

#ifdef DESKTOP
#include <QtWidgets/QApplication>
typedef QApplication Application;

#include <QQmlEngine>

#include <QStandardPaths>

#include <QSystemTrayIcon>
#include "systemtray.h"

#include "mainapplication.h"
#include "stremioprocess.h"
#include "mpv.h"
#include "screensaver.h"
#include "razerchroma.h"
#include "qclipboardproxy.h"

#include <QObject>
#include <QFile>
#include <QDebug>

#else
#include <QGuiApplication>
#endif


class CssLoader : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString cssContent READ cssContent NOTIFY cssContentChanged)

public:
    CssLoader(QObject *parent = nullptr) : QObject(parent) {
        reload();
    }

    QString cssContent() const {
        return m_cssContent;
    }

    Q_INVOKABLE void reload() {
        QFile file("/home/ras/ephemeral/mods/theme.css");
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString newContent = file.readAll();
            file.close();
            if (m_cssContent != newContent) {
                m_cssContent = newContent;
                emit cssContentChanged();
            }
        } else {
            // If can't open file, set empty content
            if (!m_cssContent.isEmpty()) {
                m_cssContent = "";
                emit cssContentChanged();
            }
            qWarning() << "Could not open CSS file";
        }
    }

signals:
    void cssContentChanged();

private:
    QString m_cssContent;
};

class JsLoader : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString jsContent READ jsContent NOTIFY jsContentChanged)

public:
    JsLoader(QObject *parent = nullptr) : QObject(parent) {
        reload();
    }

    QString jsContent() const {
        return m_jsContent;
    }

    Q_INVOKABLE void reload() {
        QFile file("/home/ras/ephemeral/mods/mod.js");
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString newContent = file.readAll();
            file.close();
            if (m_jsContent != newContent) {
                m_jsContent = newContent;
                emit jsContentChanged();
            }
        } else {
            // If can't open file, set empty content
            if (!m_jsContent.isEmpty()) {
                m_jsContent = "";
                emit jsContentChanged();
            }
            qWarning() << "Could not open JS file";
        }
    }

signals:
    void jsContentChanged();

private:
    QString m_jsContent;
};

void InitializeParameters(QQmlApplicationEngine *engine, MainApp& app) {
    QQmlContext *ctx = engine->rootContext();
    SystemTray * systemTray = new SystemTray();

    ctx->setContextProperty("applicationDirPath", QGuiApplication::applicationDirPath());
    ctx->setContextProperty("appTitle", QString(APP_TITLE));
    ctx->setContextProperty("autoUpdater", app.autoupdater);

    // Set access to an object of class properties in QML context
    ctx->setContextProperty("systemTray", systemTray);

    #ifdef QT_DEBUG
        ctx->setContextProperty("debug", true);
    #else
        ctx->setContextProperty("debug", false);
    #endif

    // Add the CssLoader instance
    CssLoader *cssLoader = new CssLoader();
    ctx->setContextProperty("cssLoader", cssLoader);

    // Add the JsLoader instance
    JsLoader *jsLoader = new JsLoader();
    ctx->setContextProperty("jsLoader", jsLoader);
}

int main(int argc, char **argv)
{
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--autoplay-policy=no-user-gesture-required");
    #ifdef _WIN32
    Application::setAttribute(Qt::AA_UseOpenGLES);
    auto winVer = QSysInfo::windowsVersion();
    if(winVer <= QSysInfo::WV_WINDOWS8 && winVer != QSysInfo::WV_None) {
        qputenv("NODE_SKIP_PLATFORM_CHECK", "1");
    }
    if(winVer <= QSysInfo::WV_WINDOWS7 && winVer != QSysInfo::WV_None) {
        qputenv("QT_ANGLE_PLATFORM", "d3d9");
    }
    #endif

    #ifndef Q_OS_LINUX
    Application::setAttribute(Qt::AA_EnableHighDpiScaling);
    #endif

    Application::setApplicationName("Stremio");
    Application::setApplicationVersion(STREMIO_SHELL_VERSION);
    Application::setOrganizationName("Smart Code ltd");
    Application::setOrganizationDomain("stremio.com");

    MainApp app(argc, argv, true);
    #ifndef Q_OS_MACOS
    if( app.isSecondary() ) {
        if( app.arguments().count() > 1)
            app.sendMessage( app.arguments().at(1).toUtf8() );
        else
            app.sendMessage( "SHOW" );
        return 0;
    }
    #endif

    app.setWindowIcon(QIcon(":/images/stremio_window.png"));

    // Qt sets the locale in the QGuiApplication constructor, but libmpv
    // requires the LC_NUMERIC category to be set to "C".
    std::setlocale(LC_NUMERIC, "C");

    static QQmlApplicationEngine* engine = new QQmlApplicationEngine();

    qmlRegisterType<Process>("com.stremio.process", 1, 0, "Process");
    qmlRegisterType<ScreenSaver>("com.stremio.screensaver", 1, 0, "ScreenSaver");
    qmlRegisterType<MpvObject>("com.stremio.libmpv", 1, 0, "MpvObject");
    qmlRegisterType<RazerChroma>("com.stremio.razerchroma", 1, 0, "RazerChroma");
    qmlRegisterType<ClipboardProxy>("com.stremio.clipboard", 1, 0, "Clipboard");

    InitializeParameters(engine, app);

    engine->load(QUrl(QStringLiteral("qrc:/main.qml")));

    #ifndef Q_OS_MACOS
    QObject::connect( &app, &SingleApplication::receivedMessage, &app, &MainApp::processMessage );
    #endif
    QObject::connect( &app, SIGNAL(receivedMessage(QVariant, QVariant)), engine->rootObjects().value(0),
                      SLOT(onAppMessageReceived(QVariant, QVariant)) );

    int ret = app.exec();
    delete engine;
    engine = nullptr;
    return ret;
}

#include "main.moc"
