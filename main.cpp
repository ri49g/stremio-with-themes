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
#include <QDir>
#include <QDebug>

#else
#include <QGuiApplication>
#endif

class ModManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList themes READ themes NOTIFY themesChanged)
    Q_PROPERTY(QStringList mods READ mods NOTIFY modsChanged)

public:
    ModManager(QObject *parent = nullptr) : QObject(parent) {
        updateLists();
    }

    QStringList themes() const { return m_themes; }
    QStringList mods() const { return m_mods; }

    Q_INVOKABLE void refresh() {
        updateLists();
        emit themesChanged();
        emit modsChanged();
    }

signals:
    void themesChanged();
    void modsChanged();

private:
    void updateLists() {
        m_themes.clear();
        m_mods.clear();

        QDir themeDir("/home/ras/ephemeral/mods/themes");
        if (themeDir.exists()) {
            QStringList themeFiles = themeDir.entryList(QStringList() << "*.css", QDir::Files);
            foreach (const QString &f, themeFiles) {
                m_themes << f;
            }
        }

        QDir modsDir("/home/ras/ephemeral/mods/mods");
        if (modsDir.exists()) {
            QStringList modFiles = modsDir.entryList(QStringList() << "*.js", QDir::Files);
            foreach (const QString &f, modFiles) {
                m_mods << f;
            }
        }
    }

    QStringList m_themes;
    QStringList m_mods;
};

class CssLoader : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString cssContent READ cssContent NOTIFY cssContentChanged)

public:
    CssLoader(QObject *parent = nullptr) : QObject(parent) {
        m_currentTheme = "theme.css"; // default
        loadTheme();
    }

    QString cssContent() const {
        return m_cssContent;
    }

    Q_INVOKABLE void setTheme(const QString &themeName) {
        m_currentTheme = themeName;
        loadTheme();
        emit cssContentChanged();
    }

signals:
    void cssContentChanged();

private:
    void loadTheme() {
        QFile file("/home/ras/ephemeral/mods/themes/" + m_currentTheme);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_cssContent = file.readAll();
            file.close();
        } else {
            m_cssContent = "";
            qWarning() << "Could not open CSS file for theme:" << m_currentTheme;
        }
    }

    QString m_cssContent;
    QString m_currentTheme;
};

class JsLoader : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString jsContent READ jsContent NOTIFY jsContentChanged)

public:
    JsLoader(QObject *parent = nullptr) : QObject(parent) {
        // by default, no mods
    }

    QString jsContent() const {
        return m_jsContent;
    }

    Q_INVOKABLE void setMods(const QStringList &enabledMods) {
        // Combine all selected mods into one JS string
        QString combined;
        foreach (const QString &m, enabledMods) {
            QFile file("/home/ras/ephemeral/mods/mods/" + m);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                combined += file.readAll();
                combined += "\n";
                file.close();
            } else {
                qWarning() << "Could not open mod file:" << m;
            }
        }
        m_jsContent = combined;
        emit jsContentChanged();
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

    CssLoader *cssLoader = new CssLoader();
    JsLoader *jsLoader = new JsLoader();
    ModManager *modManager = new ModManager();
    ctx->setContextProperty("cssLoader", cssLoader);
    ctx->setContextProperty("jsLoader", jsLoader);
    ctx->setContextProperty("modManager", modManager);
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
