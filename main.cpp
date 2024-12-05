#include <QQmlApplicationEngine>
#include <QtWebEngine>
#include <QSysInfo>
#include <clocale>

#define APP_TITLE "Stremio Modshell"

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
#include <QDir>

// MODIFIED: Updated CssLoader and JsLoader to load multiple files from directories
class CssLoader : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString cssContent READ cssContent NOTIFY cssContentChanged)

public:
    CssLoader(QObject *parent = nullptr) : QObject(parent) {
        loadCssFiles();
    }

    Q_INVOKABLE void loadCssFiles() {
        m_cssContent.clear();
        QDir dir("/home/ras/ephemeral/inject/themes");
        QStringList files = dir.entryList(QStringList() << "*.css", QDir::Files);
        foreach(QString file, files) {
            QFile f(dir.filePath(file));
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                m_cssContent += f.readAll();
                m_cssContent += "\n";
                f.close();
            } else {
                qWarning() << "Could not open CSS file:" << dir.filePath(file);
            }
        }
        emit cssContentChanged();
    }

    QString cssContent() const {
        return m_cssContent;
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
        loadJsFiles();
    }

    Q_INVOKABLE void loadJsFiles() {
        m_jsContent.clear();
        QDir dir("/home/ras/ephemeral/inject/mods");
        QStringList files = dir.entryList(QStringList() << "*.js", QDir::Files);
        foreach(QString file, files) {
            QFile f(dir.filePath(file));
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                m_jsContent += f.readAll();
                m_jsContent += "\n";
                f.close();
            } else {
                qWarning() << "Could not open JS file:" << dir.filePath(file);
            }
        }
        emit jsContentChanged();
    }

    QString jsContent() const {
        return m_jsContent;
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

    ctx->setContextProperty("systemTray", systemTray);

    #ifdef QT_DEBUG
        ctx->setContextProperty("debug", true);
    #else
        ctx->setContextProperty("debug", false);
    #endif

    // MODIFIED: Instantiate CssLoader and JsLoader for multiple files
    CssLoader *cssLoader = new CssLoader();
    ctx->setContextProperty("cssLoader", cssLoader);

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
